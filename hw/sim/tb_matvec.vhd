-- tb_matvec.vhd
-- Proves the RTL performs the weight-stationary systolic matrix-vector product
-- y = A*x for a single 4x4 tile, driven through the UART protocol (v2):
--   * x[c] stays in the immediate of every PE in column c,
--   * row 0 multiplies the streamed matrix element by that weight,
--   * rows 1..3 form a PASS delay line shifting products south,
--   * after ROWS streamed rows, PE(3-i, c) = A[i][c]*x[c],
--   * y[i] = sum_c PE(3-i, c) (summed here on the "host" side).
-- This mirrors matvec_run() in sw/compile.c.

library ieee;
use ieee.std_logic_1164.all;
use ieee.numeric_std.all;
use work.cgra_pkg.all;

entity tb_matvec is
end entity tb_matvec;

architecture sim of tb_matvec is

  constant CLK_HZ     : natural := 10_000_000;
  constant BAUD       : natural := 1_000_000;
  constant CLK_PERIOD : time := 100 ns;
  constant BIT_T      : time := 1 us;

  signal clk     : std_logic := '0';
  signal rst_btn : std_logic := '1';
  signal rx_line : std_logic := '1';
  signal tx_line : std_logic;
  signal led     : std_logic_vector(3 downto 0);
  signal done    : boolean := false;

  type mat_t is array (0 to ROWS - 1, 0 to COLS - 1) of integer;
  constant A : mat_t := ((1, 2, 3, 4),
                         (2, 3, 4, 5),
                         (3, 4, 5, 6),
                         (4, 5, 6, 7));
  type vec_t is array (0 to COLS - 1) of integer;
  constant X : vec_t := (1, 2, 3, 4);

  procedure uart_send(constant b : in std_logic_vector(7 downto 0);
                      signal   l : out std_logic) is
  begin
    l <= '0'; wait for BIT_T;
    for i in 0 to 7 loop
      l <= b(i); wait for BIT_T;
    end loop;
    l <= '1'; wait for BIT_T;
  end procedure;

  procedure uart_recv(signal   l : in  std_logic;
                      variable b : out std_logic_vector(7 downto 0)) is
  begin
    if l /= '0' then
      wait until l = '0' for 2 ms;
    end if;
    assert l = '0' report "uart_recv: start-bit timeout" severity failure;
    wait for BIT_T / 2;
    for i in 0 to 7 loop
      wait for BIT_T;
      b(i) := l;
    end loop;
    wait for BIT_T;
  end procedure;

  procedure send_cfg(signal l : out std_logic; constant c : in cfg_vec_t) is
    variable ck : unsigned(7 downto 0) := (others => '0');
    variable w  : std_logic_vector(31 downto 0);
  begin
    uart_send(CMD_CFG, l);
    for i in c'range loop
      w := c(i);
      uart_send(w(7 downto 0), l);   uart_send(w(15 downto 8), l);
      uart_send(w(23 downto 16), l); uart_send(w(31 downto 24), l);
      ck := ck + unsigned(w(7 downto 0)) + unsigned(w(15 downto 8))
               + unsigned(w(23 downto 16)) + unsigned(w(31 downto 24));
    end loop;
    uart_send(std_logic_vector(ck), l);
  end procedure;

  procedure send_wr(signal l : out std_logic;
                    constant west, north : in data_vec_t) is
    variable ck : unsigned(7 downto 0) := (others => '0');
    variable s  : std_logic_vector(15 downto 0);
  begin
    uart_send(CMD_WR, l);
    for i in west'range loop
      s := std_logic_vector(west(i));
      uart_send(s(7 downto 0), l); uart_send(s(15 downto 8), l);
      ck := ck + unsigned(s(7 downto 0)) + unsigned(s(15 downto 8));
    end loop;
    for i in north'range loop
      s := std_logic_vector(north(i));
      uart_send(s(7 downto 0), l); uart_send(s(15 downto 8), l);
      ck := ck + unsigned(s(7 downto 0)) + unsigned(s(15 downto 8));
    end loop;
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
    generic map (G_CLK_FREQ_HZ => CLK_HZ, G_BAUD => BAUD, G_TIMEOUT_CYCLES => 200_000)
    port map (clk => clk, btn_rst => rst_btn,
              uart_rx_i => rx_line, uart_tx_o => tx_line, led => led);

  stim_p : process
    variable b       : std_logic_vector(7 downto 0);
    variable v16     : std_logic_vector(15 downto 0);
    variable cfg_arr : cfg_vec_t(0 to NUM_PE - 1);
    variable west    : data_vec_t(0 to ROWS - 1);
    variable north   : data_vec_t(0 to COLS - 1);
    type int_arr_t is array (0 to NUM_PE - 1) of integer;
    variable regs    : int_arr_t;
    variable y, yref : integer;

    procedure recv_ack is
      variable a : std_logic_vector(7 downto 0);
    begin
      uart_recv(tx_line, a);
      assert a = RSP_ACK report "expected ACK, got 0x" & to_hstring(a) severity failure;
    end procedure;

    procedure read_regs is
      variable ck : unsigned(7 downto 0) := (others => '0');
    begin
      uart_send(CMD_RD, rx_line);
      for i in 0 to NUM_PE - 1 loop
        uart_recv(tx_line, b); v16(7 downto 0) := b;  ck := ck + unsigned(b);
        uart_recv(tx_line, b); v16(15 downto 8) := b; ck := ck + unsigned(b);
        regs(i) := to_integer(signed(v16));
      end loop;
      uart_recv(tx_line, b);
      assert b = std_logic_vector(ck) report "RD checksum mismatch" severity failure;
    end procedure;
  begin
    rst_btn <= '1'; wait for 10 * CLK_PERIOD;
    rst_btn <= '0'; wait for 10 * CLK_PERIOD;

    -- ID handshake (drain the 5-byte reply)
    uart_send(CMD_ID, rx_line);
    for i in 0 to 4 loop uart_recv(tx_line, b); end loop;

    -- Weight-stationary systolic config: row 0 MUL by x[c], rows 1..3 PASS.
    for r in 0 to ROWS - 1 loop
      for c in 0 to COLS - 1 loop
        if r = 0 then
          cfg_arr(r * COLS + c) := cfg_word(OP_MUL, SEL_N, SEL_CONST, X(c));
        else
          cfg_arr(r * COLS + c) := cfg_word(OP_PASS, SEL_N, SEL_ZERO, 0);
        end if;
      end loop;
    end loop;
    send_cfg(rx_line, cfg_arr);
    recv_ack;

    uart_send(CMD_RST, rx_line);
    recv_ack;

    -- Stream the matrix, one row per RUN step; west unused.
    west := (others => (others => '0'));
    for k in 0 to ROWS - 1 loop
      for c in 0 to COLS - 1 loop
        north(c) := to_signed(A(k, c), DATA_W);
      end loop;
      send_wr(rx_line, west, north);
      recv_ack;
      uart_send(CMD_RUN, rx_line);
      uart_send(x"01", rx_line);
      recv_ack;
    end loop;

    read_regs;

    for i in 0 to ROWS - 1 loop
      y    := 0;
      yref := 0;
      for c in 0 to COLS - 1 loop
        y    := y + regs((ROWS - 1 - i) * COLS + c);
        yref := yref + A(i, c) * X(c);
      end loop;
      assert y = yref
        report "matvec y[" & integer'image(i) & "] = " & integer'image(y)
             & " expected " & integer'image(yref) severity failure;
    end loop;

    report "tb_matvec PASSED (y = 30 40 50 60)";
    done <= true;
    wait;
  end process;

  watchdog_p : process
  begin
    wait for 50 ms;
    if not done then
      report "tb_matvec: global timeout" severity failure;
    end if;
    wait;
  end process;

end architecture sim;
