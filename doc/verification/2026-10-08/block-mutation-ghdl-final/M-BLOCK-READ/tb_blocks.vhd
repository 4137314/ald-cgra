-- Replay independently generated C frames against the production RTL. The
-- UART variant also exercises the actual top-level dispatcher/mux/reset path.
library ieee;
use ieee.std_logic_1164.all;
use ieee.numeric_std.all;
use std.textio.all;
use std.env.all;
use work.cgra_pkg.all;
entity tb_blocks is
  generic (G_STEP_DIV : positive := 2; G_ROWS : positive := 4; G_COLS : positive := 4;
           G_UART : boolean := false; G_VECTOR_FILE : string := "build/blocks.vec");
end entity;
architecture sim of tb_blocks is
  signal clk : std_logic := '0';
  signal rst : std_logic := '1';
  signal rx_data, tx_data : std_logic_vector(7 downto 0) := (others => '0');
  signal rx_valid, tx_start, tx_busy, selected, busy, owns, step, dp_rst : std_logic := '0';
  signal a, b : data_t;
  signal cfg : cfg_vec_t(0 to G_ROWS*G_COLS-1);
  signal west : data_vec_t(0 to G_ROWS-1);
  signal north : data_vec_t(0 to G_COLS-1);
  signal regs : data_vec_t(0 to G_ROWS*G_COLS-1);
  type bytes_t is array (0 to 511) of natural range 0 to 255;
  signal replies : bytes_t := (others => 0);
  signal reply_count, steps, resets : natural := 0;
  signal clear : boolean := false;
  signal serial_rx, serial_tx : std_logic := '1';
  constant BIT_TIME : time := 80 ns;
begin
  clk <= not clk after 5 ns;
  direct : if not G_UART generate
    ctrl : entity work.block_ctrl
      generic map (G_TIMEOUT_CYCLES => 512, G_STEP_DIV => G_STEP_DIV)
      port map (clk, rst, '1', rx_valid, rx_data, tx_busy, tx_start, tx_data,
                selected, busy, regs(0), owns, step, dp_rst, a, b);
    cfg(0) <= cfg_word(OP_MAC, SEL_W, SEL_N, 0);
    other_cfg : for i in 1 to G_ROWS*G_COLS-1 generate
      cfg(i) <= cfg_word(OP_NOP, SEL_ZERO, SEL_ZERO, 0);
    end generate;
    west(0) <= a; north(0) <= b;
    other_west : for i in 1 to G_ROWS-1 generate west(i) <= (others => '0'); end generate;
    other_north : for i in 1 to G_COLS-1 generate north(i) <= (others => '0'); end generate;
    mesh : entity work.cgra_array generic map (G_ROWS, G_COLS)
      port map (clk, rst or dp_rst, step, cfg, west, north, regs);
    transmitter : process(clk)
      variable remaining : natural := 0;
    begin
      if rising_edge(clk) then
        if clear then reply_count <= 0; end if;
        if rst = '1' then tx_busy <= '0'; remaining := 0;
        else
          if remaining > 0 then
            remaining := remaining-1;
            if remaining = 0 then tx_busy <= '0'; end if;
          end if;
          if tx_start = '1' then
            assert tx_busy = '0' report "duplicate/busy TX launch" severity failure;
            assert reply_count < 512 severity failure;
            replies(reply_count) <= to_integer(unsigned(tx_data)); reply_count <= reply_count+1;
            remaining := 2 + reply_count mod 13; tx_busy <= '1';
          end if;
        end if;
      end if;
    end process;
    cadence : process(clk)
      variable since_step, since_reset, since_data : natural := 10000;
      variable previous_a, previous_b : data_t;
    begin
      if rising_edge(clk) then
        if clear then steps <= 0; resets <= 0; end if;
        since_step := since_step+1; since_reset := since_reset+1; since_data := since_data+1;
        if std_logic_vector(a) /= std_logic_vector(previous_a) or
           std_logic_vector(b) /= std_logic_vector(previous_b) then since_data := 0; end if;
        previous_a := a; previous_b := b;
        if rst = '1' then since_step := 10000; since_reset := 0;
        elsif dp_rst = '1' then
          assert step = '0' severity failure;
          since_reset := 0; resets <= resets+1;
        elsif step = '1' then
          assert since_step >= G_STEP_DIV and since_reset >= G_STEP_DIV and since_data >= G_STEP_DIV
            report "block capture violates STEP_DIV/data/reset guard" severity failure;
          assert owns = '1' report "step outside array ownership" severity failure;
          since_step := 0; steps <= steps+1;
        end if;
      end if;
    end process;
  end generate;
  uart_path : if G_UART generate
    top : entity work.cgra_top
      generic map (G_CLK_FREQ_HZ => 100_000_000, G_BAUD => 12_500_000,
                   G_TIMEOUT_CYCLES => 512, G_STEP_DIV => G_STEP_DIV,
                   G_ROWS => G_ROWS, G_COLS => G_COLS, G_BLOCK_ENABLE => true)
      port map (clk, rst, serial_rx, serial_tx, open);
    collector : process
      variable byte : std_logic_vector(7 downto 0);
    begin
      wait until falling_edge(serial_tx) or clear;
      if clear then reply_count <= 0;
      else
        wait for BIT_TIME * 3 / 2;
        for i in 0 to 7 loop byte(i) := serial_tx; wait for BIT_TIME; end loop;
        assert serial_tx = '1' report "invalid UART stop bit" severity failure;
        assert reply_count < 512 severity failure;
        replies(reply_count) <= to_integer(unsigned(byte)); reply_count <= reply_count+1;
      end if;
    end process;
  end generate;
  stimulus : process
    file vectors : text open read_mode is G_VECTOR_FILE;
    variable line_in : line;
    variable nq, nr, ns, nd, value, transaction : natural := 0;
    variable expected : bytes_t;
    variable terminated : boolean := false;
    procedure tick is begin wait until falling_edge(clk); end procedure;
    procedure send_byte(byte_value : natural) is
      variable bits : std_logic_vector(7 downto 0);
    begin
      if G_UART then
        bits := std_logic_vector(to_unsigned(byte_value, 8)); serial_rx <= '0'; wait for BIT_TIME;
        for i in 0 to 7 loop serial_rx <= bits(i); wait for BIT_TIME; end loop;
        serial_rx <= '1'; wait for BIT_TIME; tick;
      else
        rx_data <= std_logic_vector(to_unsigned(byte_value, 8)); rx_valid <= '1'; tick;
        rx_valid <= '0'; tick; tick;
      end if;
    end procedure;
    procedure clear_counts is
    begin clear <= true; tick; clear <= false; tick; end procedure;
  begin
    tick; tick; rst <= '0';
    for i in 1 to 10 loop tick; end loop;
    while not endfile(vectors) loop
      readline(vectors, line_in); read(line_in, nq); read(line_in, nr); read(line_in, ns); read(line_in, nd);
      clear_counts;
      if nq = 0 then
        if nr = 0 then terminated := true; exit;
        elsif nr = 1 then
          rst <= '1'; for i in 1 to 5 loop tick; end loop; rst <= '0';
          for i in 1 to 10 loop tick; end loop;
          -- Legacy ID remains usable before explicit preflight, also after reset.
          if G_UART then
            send_byte(1); for i in 1 to 600 loop tick; end loop;
            assert reply_count = 5 and replies(0) = 202 and replies(1) = 3 and
              replies(2) = G_ROWS and replies(3) = G_COLS and replies(4) = 16
              report "legacy ID/top dispatcher regression" severity failure;
          end if;
        elsif nr = 2 then for i in 1 to 520 loop tick; end loop;
        else assert false report "bad event" severity failure;
        end if;
      else
        for i in 1 to nq loop read(line_in, value); send_byte(value); end loop;
        for i in 1 to nr loop read(line_in, expected(i-1)); end loop;
        for i in 1 to 12000 loop
          tick;
          exit when (nr > 0 and reply_count >= nr) or (nr = 0 and i = 600);
        end loop;
        for i in 1 to 100 loop tick; end loop;
        assert reply_count = nr report "block reply length at event " & natural'image(transaction) &
          " expected " & natural'image(nr) & " got " & natural'image(reply_count) severity failure;
        for i in 1 to nr loop
          assert replies(i-1) = expected(i-1) report "block reply byte at event " & natural'image(transaction) &
            " byte " & natural'image(i-1) & " expected " & natural'image(expected(i-1)) &
            " got " & natural'image(replies(i-1)) severity failure;
        end loop;
        if not G_UART then
          assert steps = ns and resets = nd report "effects at event " & natural'image(transaction) severity failure;
        end if;
      end if;
      transaction := transaction+1;
    end loop;
    assert terminated report "missing transcript terminator" severity failure;
    report "tb_blocks PASS: " & natural'image(transaction) & " events, DIV=" & positive'image(G_STEP_DIV) &
      " UART=" & boolean'image(G_UART) severity note;
    stop; wait;
  end process;
end architecture;
