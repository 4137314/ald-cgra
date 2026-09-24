-- Replay the real host kernel workload through the complete UART top-level.
library ieee;
use ieee.std_logic_1164.all;
use ieee.numeric_std.all;
use std.textio.all;
use std.env.all;

entity tb_kernel_uart is
  generic (G_ROWS : positive := 2; G_COLS : positive := 3;
           G_VECTOR_FILE : string := "build/kernels_2x3.vec");
end entity;

architecture sim of tb_kernel_uart is
  constant BIT_T : time := 1 us;
  signal clk : std_logic := '0';
  signal rst : std_logic := '1';
  signal rx_line : std_logic := '1';
  signal tx_line : std_logic;
  signal led : std_logic_vector(3 downto 0);
begin
  clk <= not clk after 50 ns;
  dut : entity work.cgra_top
    generic map (G_ROWS => G_ROWS, G_COLS => G_COLS,
                 G_CLK_FREQ_HZ => 10_000_000, G_BAUD => 1_000_000,
                 G_TIMEOUT_CYCLES => 200_000)
    port map (clk => clk, btn_rst => rst, uart_rx_i => rx_line,
              uart_tx_o => tx_line, led => led);

  stimulus : process
    file vectors : text open read_mode is G_VECTOR_FILE;
    variable ln : line;
    variable nrequest, nexpected, value, transaction_no : natural := 0;
    variable b : std_logic_vector(7 downto 0);
    variable terminated : boolean := false;
    procedure send_byte(constant value : natural) is
      variable bits : std_logic_vector(7 downto 0);
    begin
      bits := std_logic_vector(to_unsigned(value, 8));
      rx_line <= '0'; wait for BIT_T;
      for i in 0 to 7 loop rx_line <= bits(i); wait for BIT_T; end loop;
      rx_line <= '1'; wait for BIT_T;
    end procedure;
    procedure recv_byte(variable bits : out std_logic_vector(7 downto 0)) is
    begin
      if tx_line /= '0' then wait until tx_line = '0' for 2 ms; end if;
      assert tx_line = '0' report "kernel UART start timeout" severity failure;
      wait for BIT_T / 2;
      for i in 0 to 7 loop wait for BIT_T; bits(i) := tx_line; end loop;
      wait for BIT_T;
      assert tx_line = '1' report "kernel UART framing error" severity failure;
    end procedure;
  begin
    wait for 1 us; rst <= '0'; wait for 1 us;
    while not endfile(vectors) loop
      readline(vectors, ln); read(ln, nrequest); read(ln, nexpected);
      if nrequest = 0 then
        assert nexpected = 0 and endfile(vectors) report "invalid kernel terminator" severity failure;
        terminated := true; exit;
      end if;
      for i in 1 to nrequest loop read(ln, value); send_byte(value); end loop;
      for i in 1 to nexpected loop
        read(ln, value); recv_byte(b);
        assert to_integer(unsigned(b)) = value
          report "kernel UART mismatch transaction=" & natural'image(transaction_no) &
                 " byte=" & natural'image(i - 1) & " expected=" & natural'image(value) &
                 " got=" & natural'image(to_integer(unsigned(b))) severity failure;
      end loop;
      wait for BIT_T;
      transaction_no := transaction_no + 1;
    end loop;
    assert terminated and transaction_no > 0 report "truncated/empty kernel transcript" severity failure;
    report "tb_kernel_uart PASSED (" & positive'image(G_ROWS) & "x" & positive'image(G_COLS) &
           ", " & natural'image(transaction_no) & " host kernel transactions)";
    finish;
  end process;
  watchdog : process
  begin
    wait for 1 sec;
    assert false report "kernel UART global timeout" severity failure;
  end process;
end architecture;
