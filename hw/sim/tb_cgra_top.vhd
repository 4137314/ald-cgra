-- tb_cgra_top.vhd
-- End-to-end test of the full design through the UART protocol (v3):
-- extended ID handshake, configuration + checksum, input write + checksum,
-- run, checksum-verified register readback, MAC with datapath reset, signed
-- result, diagonal element-wise mode, a rejected (bad-checksum) CFG, and the
-- v3 fused EXEC transaction (masked read-back, fused reset, empty mask, and
-- the guarantee that a corrupted EXEC payload does not advance the datapath).
--
-- The DUT runs at 10 MHz / 1 Mbaud in simulation to keep runtimes short;
-- on the board the generics default to 100 MHz / 115200.

library ieee;
use ieee.std_logic_1164.all;
use ieee.numeric_std.all;
use work.cgra_pkg.all;

entity tb_cgra_top is
end entity tb_cgra_top;

architecture sim of tb_cgra_top is

  constant CLK_HZ     : natural := 10_000_000;
  constant BAUD       : natural := 1_000_000;
  constant CLK_PERIOD : time := 100 ns;
  constant BIT_T      : time := 1 us;   -- 1 / BAUD

  signal clk     : std_logic := '0';
  signal rst_btn : std_logic := '1';
  signal rx_line : std_logic := '1';    -- host -> FPGA
  signal tx_line : std_logic;           -- FPGA -> host
  signal led     : std_logic_vector(3 downto 0);
  signal done    : boolean := false;

  procedure uart_send(constant b : in std_logic_vector(7 downto 0);
                      signal   l : out std_logic) is
  begin
    l <= '0';                       -- start
    wait for BIT_T;
    for i in 0 to 7 loop            -- LSB first
      l <= b(i);
      wait for BIT_T;
    end loop;
    l <= '1';                       -- stop
    wait for BIT_T;
  end procedure;

  procedure uart_recv(signal   l : in  std_logic;
                      variable b : out std_logic_vector(7 downto 0)) is
  begin
    if l /= '0' then
      wait until l = '0' for 2 ms;
    end if;
    assert l = '0'
      report "uart_recv: timeout waiting for start bit" severity failure;
    wait for BIT_T / 2;             -- middle of start bit
    for i in 0 to 7 loop
      wait for BIT_T;
      b(i) := l;
    end loop;
    wait for BIT_T;                 -- middle of stop bit
    assert l = '1'
      report "uart_recv: framing error" severity failure;
  end procedure;

  -- Send CMD_CFG + 16 config words (LE) + checksum. If corrupt, flips one
  -- checksum bit so the device should NACK.
  procedure send_cfg(signal   l       : out std_logic;
                     constant c       : in  cfg_vec_t;
                     constant corrupt : in  boolean := false) is
    variable ck : unsigned(7 downto 0) := (others => '0');
    variable w  : std_logic_vector(31 downto 0);
  begin
    uart_send(CMD_CFG, l);
    for i in c'range loop
      w := c(i);
      uart_send(w(7 downto 0), l);
      uart_send(w(15 downto 8), l);
      uart_send(w(23 downto 16), l);
      uart_send(w(31 downto 24), l);
      ck := ck + unsigned(w(7 downto 0)) + unsigned(w(15 downto 8))
               + unsigned(w(23 downto 16)) + unsigned(w(31 downto 24));
    end loop;
    if corrupt then
      ck := ck + 1;
    end if;
    uart_send(std_logic_vector(ck), l);
  end procedure;

  -- Send CMD_WR + west + north (LE) + checksum.
  procedure send_wr(signal   l     : out std_logic;
                    constant west  : in  data_vec_t;
                    constant north : in  data_vec_t) is
    variable ck : unsigned(7 downto 0) := (others => '0');
    variable s  : std_logic_vector(15 downto 0);
  begin
    uart_send(CMD_WR, l);
    for i in west'range loop
      s := std_logic_vector(west(i));
      uart_send(s(7 downto 0), l);
      uart_send(s(15 downto 8), l);
      ck := ck + unsigned(s(7 downto 0)) + unsigned(s(15 downto 8));
    end loop;
    for i in north'range loop
      s := std_logic_vector(north(i));
      uart_send(s(7 downto 0), l);
      uart_send(s(15 downto 8), l);
      ck := ck + unsigned(s(7 downto 0)) + unsigned(s(15 downto 8));
    end loop;
    uart_send(std_logic_vector(ck), l);
  end procedure;

  -- Send CMD_EXEC + steps + flags + 16-bit tap mask + west/north (LE) +
  -- checksum (protocol v3). 'corrupt' flips the checksum so the device must
  -- refuse to step the array.
  procedure send_exec(signal   l       : out std_logic;
                      constant steps   : in  natural;
                      constant flags   : in  std_logic_vector(7 downto 0);
                      constant mask    : in  std_logic_vector(NUM_PE - 1 downto 0);
                      constant west    : in  data_vec_t;
                      constant north   : in  data_vec_t;
                      constant corrupt : in  boolean := false) is
    variable ck : unsigned(7 downto 0) := (others => '0');
    variable s  : std_logic_vector(15 downto 0);
    variable hb : std_logic_vector(7 downto 0);

    procedure put(constant x : in std_logic_vector(7 downto 0)) is
    begin
      uart_send(x, l);
      ck := ck + unsigned(x);
    end procedure;
  begin
    uart_send(CMD_EXEC, l);
    hb := std_logic_vector(to_unsigned(steps, 8));
    put(hb);
    put(flags);
    put(mask(7 downto 0));
    put(mask(15 downto 8));
    for i in west'range loop
      s := std_logic_vector(west(i));
      put(s(7 downto 0));
      put(s(15 downto 8));
    end loop;
    for i in north'range loop
      s := std_logic_vector(north(i));
      put(s(7 downto 0));
      put(s(15 downto 8));
    end loop;
    if corrupt then
      ck := ck + 1;
    end if;
    uart_send(std_logic_vector(ck), l);
  end procedure;

begin

  clk_p : process
  begin
    while not done loop
      clk <= '0'; wait for CLK_PERIOD / 2;
      clk <= '1'; wait for CLK_PERIOD / 2;
    end loop;
    wait;
  end process;

  dut : entity work.cgra_top
    generic map (
      G_CLK_FREQ_HZ    => CLK_HZ,
      G_BAUD           => BAUD,
      G_TIMEOUT_CYCLES => 200_000
    )
    port map (
      clk       => clk,
      btn_rst   => rst_btn,
      uart_rx_i => rx_line,
      uart_tx_o => tx_line,
      led       => led
    );

  stim_p : process
    variable b       : std_logic_vector(7 downto 0);
    variable v16     : std_logic_vector(15 downto 0);
    variable cfg_arr : cfg_vec_t(0 to NUM_PE - 1);
    variable west    : data_vec_t(0 to ROWS - 1);
    variable north   : data_vec_t(0 to COLS - 1);
    type int_arr_t is array (0 to NUM_PE - 1) of integer;
    variable regs    : int_arr_t;

    procedure recv_ack is
      variable a : std_logic_vector(7 downto 0);
    begin
      uart_recv(tx_line, a);
      assert a = RSP_ACK
        report "expected ACK, got 0x" & to_hstring(a) severity failure;
    end procedure;

    procedure recv_nack is
      variable a : std_logic_vector(7 downto 0);
    begin
      uart_recv(tx_line, a);
      assert a = RSP_NACK
        report "expected NACK, got 0x" & to_hstring(a) severity failure;
    end procedure;

    -- Read 16 registers + verify the trailing checksum.
    procedure read_regs is
      variable ck : unsigned(7 downto 0) := (others => '0');
    begin
      uart_send(CMD_RD, rx_line);
      for i in 0 to NUM_PE - 1 loop
        uart_recv(tx_line, b);
        v16(7 downto 0) := b;
        ck := ck + unsigned(b);
        uart_recv(tx_line, b);
        v16(15 downto 8) := b;
        ck := ck + unsigned(b);
        regs(i) := to_integer(signed(v16));
      end loop;
      uart_recv(tx_line, b);
      assert b = std_logic_vector(ck)
        report "RD checksum mismatch: got 0x" & to_hstring(b) severity failure;
    end procedure;

    -- Read an EXEC reply: 'ntap' masked registers (LE), the data checksum and
    -- the status byte. Taps land in regs(0 .. ntap-1) in ascending PE order.
    procedure read_exec(constant ntap : in  integer;
                        variable st   : out std_logic_vector(7 downto 0)) is
      variable ck : unsigned(7 downto 0) := (others => '0');
    begin
      for i in 0 to ntap - 1 loop
        uart_recv(tx_line, b);
        v16(7 downto 0) := b;
        ck := ck + unsigned(b);
        uart_recv(tx_line, b);
        v16(15 downto 8) := b;
        ck := ck + unsigned(b);
        regs(i) := to_integer(signed(v16));
      end loop;
      uart_recv(tx_line, b);
      assert b = std_logic_vector(ck)
        report "EXEC checksum mismatch: got 0x" & to_hstring(b) severity failure;
      uart_recv(tx_line, st);
    end procedure;

    procedure set_inputs(constant w0, w1, w2, w3, n0, n1, n2, n3 : in integer) is
    begin
      west(0)  := to_signed(w0, DATA_W); west(1)  := to_signed(w1, DATA_W);
      west(2)  := to_signed(w2, DATA_W); west(3)  := to_signed(w3, DATA_W);
      north(0) := to_signed(n0, DATA_W); north(1) := to_signed(n1, DATA_W);
      north(2) := to_signed(n2, DATA_W); north(3) := to_signed(n3, DATA_W);
    end procedure;
  begin
    rst_btn <= '1';
    wait for 10 * CLK_PERIOD;
    rst_btn <= '0';
    wait for 10 * CLK_PERIOD;

    ---------------------------------------------------------------
    -- 1. Extended ID handshake: ID_BYTE0, PROTO_VER, ROWS, COLS, DATA_W
    ---------------------------------------------------------------
    uart_send(CMD_ID, rx_line);
    uart_recv(tx_line, b);
    assert b = ID_BYTE0  report "ID signature mismatch" severity failure;
    uart_recv(tx_line, b);
    assert b = PROTO_VER report "ID version mismatch" severity failure;
    uart_recv(tx_line, b);
    assert b = std_logic_vector(to_unsigned(ROWS, 8)) report "ID rows mismatch" severity failure;
    uart_recv(tx_line, b);
    assert b = std_logic_vector(to_unsigned(COLS, 8)) report "ID cols mismatch" severity failure;
    uart_recv(tx_line, b);
    assert b = std_logic_vector(to_unsigned(DATA_W, 8)) report "ID width mismatch" severity failure;
    report "tb_cgra_top: extended ID handshake ok";

    ---------------------------------------------------------------
    -- 2. Unknown command is NACKed
    ---------------------------------------------------------------
    uart_send(x"EE", rx_line);
    recv_nack;

    ---------------------------------------------------------------
    -- 3. Configure: PE(0,0) = ADD(north input, west input), rest NOP
    ---------------------------------------------------------------
    cfg_arr := (others => cfg_word(OP_NOP, SEL_ZERO, SEL_ZERO, 0));
    cfg_arr(0) := cfg_word(OP_ADD, SEL_N, SEL_W, 0);
    send_cfg(rx_line, cfg_arr);
    recv_ack;

    set_inputs(7, 0, 0, 0, 5, 0, 0, 0);         -- west0=7, north0=5
    send_wr(rx_line, west, north);
    recv_ack;

    uart_send(CMD_RUN, rx_line);
    uart_send(x"01", rx_line);
    recv_ack;

    read_regs;
    assert regs(0) = 12
      report "ADD result wrong: " & integer'image(regs(0)) severity failure;
    report "tb_cgra_top: ADD via UART ok";

    ---------------------------------------------------------------
    -- 4. Corrupted CFG payload is rejected (NACK), state stays usable
    ---------------------------------------------------------------
    send_cfg(rx_line, cfg_arr, corrupt => true);
    recv_nack;
    report "tb_cgra_top: bad-checksum CFG rejected ok";

    ---------------------------------------------------------------
    -- 5. MAC with datapath reset: 3*4 + 2*10 = 32
    ---------------------------------------------------------------
    cfg_arr(0) := cfg_word(OP_MAC, SEL_N, SEL_W, 0);
    send_cfg(rx_line, cfg_arr);
    recv_ack;

    uart_send(CMD_RST, rx_line);                -- clear stale register values
    recv_ack;

    set_inputs(4, 0, 0, 0, 3, 0, 0, 0);
    send_wr(rx_line, west, north);
    recv_ack;
    uart_send(CMD_RUN, rx_line);
    uart_send(x"01", rx_line);
    recv_ack;

    set_inputs(10, 0, 0, 0, 2, 0, 0, 0);
    send_wr(rx_line, west, north);
    recv_ack;
    uart_send(CMD_RUN, rx_line);
    uart_send(x"01", rx_line);
    recv_ack;

    read_regs;
    assert regs(0) = 32
      report "MAC result wrong: " & integer'image(regs(0)) severity failure;
    report "tb_cgra_top: MAC + datapath reset ok";

    ---------------------------------------------------------------
    -- 6. Signed result: SUB 5 - 7 = -2
    ---------------------------------------------------------------
    cfg_arr(0) := cfg_word(OP_SUB, SEL_N, SEL_W, 0);
    send_cfg(rx_line, cfg_arr);
    recv_ack;

    set_inputs(7, 0, 0, 0, 5, 0, 0, 0);
    send_wr(rx_line, west, north);
    recv_ack;
    uart_send(CMD_RUN, rx_line);
    uart_send(x"01", rx_line);
    recv_ack;

    read_regs;
    assert regs(0) = -2
      report "SUB result wrong: " & integer'image(regs(0)) severity failure;
    report "tb_cgra_top: signed SUB ok";

    ---------------------------------------------------------------
    -- 7. Diagonal element-wise mode (as used by the C library):
    --    north = a[], west = b[], PASS chains, ADD on the diagonal,
    --    ROWS steps, results in PE(k,k).
    ---------------------------------------------------------------
    for r in 0 to ROWS - 1 loop
      for c in 0 to COLS - 1 loop
        if r = c then
          cfg_arr(r * COLS + c) := cfg_word(OP_ADD, SEL_N, SEL_W, 0);
        elsif r < c then
          cfg_arr(r * COLS + c) := cfg_word(OP_PASS, SEL_N, SEL_ZERO, 0);
        else
          cfg_arr(r * COLS + c) := cfg_word(OP_PASS, SEL_W, SEL_ZERO, 0);
        end if;
      end loop;
    end loop;
    send_cfg(rx_line, cfg_arr);
    recv_ack;

    -- a = {10, 20, 30, 40} on north, b = {1, 2, 3, -4} on west
    set_inputs(1, 2, 3, -4, 10, 20, 30, 40);
    send_wr(rx_line, west, north);
    recv_ack;

    uart_send(CMD_RUN, rx_line);
    uart_send(x"04", rx_line);                  -- ROWS steps
    recv_ack;

    read_regs;
    assert regs(0) = 11
      report "diag[0] wrong: " & integer'image(regs(0)) severity failure;
    assert regs(5) = 22
      report "diag[1] wrong: " & integer'image(regs(5)) severity failure;
    assert regs(10) = 33
      report "diag[2] wrong: " & integer'image(regs(10)) severity failure;
    assert regs(15) = 36
      report "diag[3] wrong: " & integer'image(regs(15)) severity failure;
    report "tb_cgra_top: diagonal element-wise mode ok";

    ---------------------------------------------------------------
    -- 8. Protocol v3: the same diagonal chunk in ONE fused EXEC
    --    transaction (reset + inputs + 4 steps + masked read-back).
    --    Mask 0x8421 taps the four diagonal PEs 0, 5, 10, 15.
    ---------------------------------------------------------------
    set_inputs(1, 2, 3, -4, 10, 20, 30, 40);
    send_exec(rx_line, 4, x"01", x"8421", west, north);   -- flags: fused reset
    read_exec(4, b);
    assert b = RSP_ACK
      report "EXEC status not ACK: 0x" & to_hstring(b) severity failure;
    assert regs(0) = 11 and regs(1) = 22 and regs(2) = 33 and regs(3) = 36
      report "EXEC diagonal taps wrong: " & integer'image(regs(0)) & " "
             & integer'image(regs(1)) & " " & integer'image(regs(2)) & " "
             & integer'image(regs(3)) severity failure;
    report "tb_cgra_top: fused EXEC (masked read-back) ok";

    ---------------------------------------------------------------
    -- 9. A corrupted EXEC payload must NACK *and* leave the datapath
    --    untouched: the reply still has its fixed length, and the taps
    --    still hold the previous results.
    ---------------------------------------------------------------
    set_inputs(100, 100, 100, 100, 100, 100, 100, 100);
    send_exec(rx_line, 4, x"01", x"8421", west, north, corrupt => true);
    read_exec(4, b);
    assert b = RSP_NACK
      report "corrupt EXEC status not NACK: 0x" & to_hstring(b) severity failure;
    assert regs(0) = 11 and regs(3) = 36
      report "corrupt EXEC advanced the datapath" severity failure;
    report "tb_cgra_top: corrupt EXEC rejected without stepping ok";

    ---------------------------------------------------------------
    -- 10. EXEC with an empty tap mask: pure fused write+run (the shape
    --     the reduction/systolic drivers use), verified with a plain RD.
    ---------------------------------------------------------------
    cfg_arr := (others => cfg_word(OP_NOP, SEL_ZERO, SEL_ZERO, 0));
    cfg_arr(0) := cfg_word(OP_ADD, SEL_N, SEL_W, 0);
    send_cfg(rx_line, cfg_arr);
    recv_ack;

    set_inputs(7, 0, 0, 0, 5, 0, 0, 0);
    send_exec(rx_line, 1, x"01", x"0000", west, north);
    read_exec(0, b);
    assert b = RSP_ACK
      report "empty-mask EXEC status not ACK" severity failure;
    read_regs;
    assert regs(0) = 12
      report "empty-mask EXEC did not step: " & integer'image(regs(0)) severity failure;
    report "tb_cgra_top: EXEC with empty tap mask ok";

    report "tb_cgra_top PASSED";
    done <= true;
    wait;
  end process;

  watchdog_p : process
  begin
    wait for 50 ms;
    if not done then
      report "tb_cgra_top: global timeout" severity failure;
    end if;
    wait;
  end process;

end architecture sim;
