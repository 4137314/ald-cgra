-- Replay C-emulator byte transcripts against the real controller and array.
-- UART framing has its own tests; here a bounded, variable tx_busy models
-- backpressure so every response byte can be compared cheaply.
library ieee;
use ieee.std_logic_1164.all;
use ieee.numeric_std.all;
use std.textio.all;
use std.env.all;
use work.cgra_pkg.all;

entity tb_audit_mcp is
  generic (G_STEP_DIV : positive := 2;
           G_ROWS : positive := ROWS; G_COLS : positive := COLS;
           G_VECTOR_FILE : string := "build/protocol_diff.vec");
end entity;

architecture sim of tb_audit_mcp is
  signal clk : std_logic := '0';
  signal rst : std_logic := '1';
  signal rx_data, tx_data : std_logic_vector(7 downto 0) := (others => '0');
  signal rx_valid, tx_start, tx_busy, busy, step, dp_rst : std_logic := '0';
  signal cfg : cfg_vec_t(0 to G_ROWS * G_COLS - 1);
  signal west : data_vec_t(0 to G_ROWS - 1);
  signal north : data_vec_t(0 to G_COLS - 1);
  signal regs : data_vec_t(0 to G_ROWS * G_COLS - 1);
  type bytes_t is array (0 to 255) of natural range 0 to 255;
  signal replies : bytes_t := (others => 0);
  signal reply_count : natural range 0 to 256 := 0;
  signal clear_replies : boolean := false;
begin
  clk <= not clk after 5 ns;
  ctrl : entity work.cgra_ctrl
    generic map (G_TIMEOUT_CYCLES => 64, G_STEP_DIV => G_STEP_DIV, G_ROWS => G_ROWS, G_COLS => G_COLS)
    port map (clk => clk, rst => rst, rx_data => rx_data, rx_valid => rx_valid,
              tx_data => tx_data, tx_start => tx_start, tx_busy => tx_busy,
              cfg => cfg, west_o => west, north_o => north,
              step => step, dp_rst => dp_rst, pe_regs => regs, busy => busy);
  mesh : entity work.cgra_array
    generic map (G_ROWS => G_ROWS, G_COLS => G_COLS)
    port map (clk => clk, rst => rst or dp_rst, step => step,
              cfg => cfg, west_in => west, north_in => north, pe_out => regs);

  transmitter : process (clk)
    variable remaining : natural := 0;
    variable byte_count : natural := 0;
  begin
    if rising_edge(clk) then
      if clear_replies then reply_count <= 0; end if;
      if remaining > 0 then
        remaining := remaining - 1;
        if remaining = 0 then tx_busy <= '0'; end if;
      end if;
      if tx_start = '1' then
        assert tx_busy = '0' report "TX started while busy" severity failure;
        assert reply_count < replies'length report "extra reply bytes" severity failure;
        replies(reply_count) <= to_integer(unsigned(tx_data));
        reply_count <= reply_count + 1;
        byte_count := byte_count + 1;
        remaining := 2 + byte_count mod 7;
        tx_busy <= '1';
      end if;
    end if;
  end process;

  -- Correct outputs alone cannot prove the multicycle constraint is justified:
  -- observe the enable cadence as well as the protocol-visible results.
  cadence : process (clk)
    variable have_step : boolean := false;
    variable last_step : time := 0 ns;
  begin
    if rising_edge(clk) then
      if rst = '1' then
        have_step := false;
      elsif step = '1' then
        assert dp_rst = '0' report "step coincides with datapath reset" severity failure;
        if have_step then
          assert now - last_step >= G_STEP_DIV * 10 ns
            report "array steps violate the multicycle cadence" severity failure;
        end if;
        have_step := true;
        last_step := now;
      end if;
    end if;
  end process;

  reset_spacing_audit : process(clk)
    variable reset_seen : boolean := false;
    variable last_reset : time := 0 ns;
  begin
    if rising_edge(clk) then
      if dp_rst = '1' then reset_seen := true; last_reset := now; end if;
      if step = '1' and reset_seen then
        assert now - last_reset >= G_STEP_DIV * 10 ns
          report "AUDIT: first capture after datapath reset has only " & time'image(now - last_reset) &
                 "; multicycle exception grants " & positive'image(G_STEP_DIV) & " clocks"
          severity failure;
        reset_seen := false;
      end if;
    end if;
  end process;

  stimulus : process
    file vectors : text open read_mode is G_VECTOR_FILE;
    variable line_in : line;
    variable nrequest, nexpected, value : natural;
    variable expected : bytes_t;
    variable transaction_no : natural := 0;
    variable terminated : boolean := false;
    procedure tick is
    begin
      wait until falling_edge(clk);
    end procedure;
    procedure send_byte(constant b : natural) is
    begin
      rx_data <= std_logic_vector(to_unsigned(b, 8)); rx_valid <= '1';
      tick;
      rx_valid <= '0';
      for i in 1 to 3 loop tick; end loop;
    end procedure;
    procedure clear is
    begin
      clear_replies <= true; tick; clear_replies <= false;
    end procedure;
    procedure wait_reply is
    begin
      for i in 1 to 4096 loop
        tick;
        exit when busy = '0' and tx_busy = '0';
      end loop;
      assert busy = '0' and tx_busy = '0' report "controller reply timeout" severity failure;
    end procedure;
  begin
    tick; tick; rst <= '0'; tick;
    while not endfile(vectors) loop
      readline(vectors, line_in);
      read(line_in, nrequest); read(line_in, nexpected);
      if nrequest = 0 then
        assert nexpected = 0 and endfile(vectors)
          report "invalid transcript terminator" severity failure;
        terminated := true;
        exit;
      end if;
      clear;
      for i in 1 to nrequest loop
        read(line_in, value); send_byte(value);
      end loop;
      for i in 1 to nexpected loop read(line_in, expected(i - 1)); end loop;
      if nexpected > 0 then wait_reply; else tick; end if;
      assert reply_count = nexpected
        report "reply length mismatch at transaction " & natural'image(transaction_no)
        severity failure;
      for i in 1 to nexpected loop
        assert replies(i - 1) = expected(i - 1)
          report "protocol mismatch transaction=" & natural'image(transaction_no) &
                 " byte=" & natural'image(i - 1) & " expected=" & natural'image(expected(i - 1)) &
                 " got=" & natural'image(replies(i - 1)) severity failure;
      end loop;
      transaction_no := transaction_no + 1;
    end loop;
    assert terminated and transaction_no > 0 report "truncated/empty transcript" severity failure;

    -- Timed parser recovery is an RTL property: the synchronous C model has no
    -- clock. Abort each kind of partial request, then require a complete ID.
    for cmd in 2 to 7 loop
      if cmd = 2 or cmd = 3 or cmd = 4 or cmd = 7 then
        clear;
        send_byte(cmd);
        if cmd /= 4 then send_byte(0); end if;
        for i in 1 to 70 loop tick; end loop;
        assert busy = '0' and reply_count = 0
          report "watchdog did not abort partial command " & integer'image(cmd) severity failure;
        send_byte(1); wait_reply;
        assert reply_count = 5 and replies(0) = 16#CA# and
               replies(1) = to_integer(unsigned(PROTO_VER)) and
               replies(2) = G_ROWS and replies(3) = G_COLS and replies(4) = DATA_W
          report "ID failed after watchdog recovery" severity failure;
      end if;
    end loop;
    report "tb_audit_mcp PASSED (" & natural'image(transaction_no) &
           " transactions, " & positive'image(G_ROWS) & "x" & positive'image(G_COLS) & ", STEP_DIV=" & positive'image(G_STEP_DIV) & ", watchdog recovery)";
    finish;
  end process;
  watchdog : process
  begin
    wait for 10 ms;
    assert false report "tb_audit_mcp global timeout" severity failure;
  end process;
end architecture;
