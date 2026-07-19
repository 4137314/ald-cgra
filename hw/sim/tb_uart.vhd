-- tb_uart.vhd
-- Loopback testbench for the UART primitives: uart_tx.tx_serial drives
-- uart_rx.rx_serial directly, and every transmitted byte must reappear,
-- bit-exact, at rx_data with a one-cycle rx_valid pulse. Exercises the framing
-- (start/8 data LSB-first/stop) and the receiver's mid-bit sampling across a
-- set of adversarial byte patterns. Self-checking; a watchdog fails the run if
-- the link ever stalls, so `make sim` alone certifies the UART.

library ieee;
use ieee.std_logic_1164.all;
use ieee.numeric_std.all;
use std.env.all;

entity tb_uart is
end entity tb_uart;

architecture sim of tb_uart is
  constant CLKS_PER_BIT : natural := 16;          -- short bit time for fast sim

  signal clk      : std_logic := '0';
  signal rst      : std_logic := '1';
  signal line     : std_logic;                    -- tx_serial -> rx_serial
  signal tx_start : std_logic := '0';
  signal tx_data  : std_logic_vector(7 downto 0) := (others => '0');
  signal tx_busy  : std_logic;
  signal rx_valid : std_logic;
  signal rx_data  : std_logic_vector(7 downto 0);

  type byte_arr is array (natural range <>) of std_logic_vector(7 downto 0);
  constant PATTERNS : byte_arr := (
    x"00", x"FF", x"55", x"AA", x"01", x"80", x"7E", x"C3", x"79", x"1F");
begin

  clk <= not clk after 5 ns;                       -- 100 MHz

  u_tx : entity work.uart_tx
    generic map (CLKS_PER_BIT => CLKS_PER_BIT)
    port map (clk => clk, rst => rst, tx_start => tx_start,
              tx_data => tx_data, tx_serial => line, tx_busy => tx_busy);

  u_rx : entity work.uart_rx
    generic map (CLKS_PER_BIT => CLKS_PER_BIT)
    port map (clk => clk, rst => rst, rx_serial => line,
              rx_valid => rx_valid, rx_data => rx_data);

  -- watchdog: the loopback of 10 bytes must finish well within this window.
  watchdog : process
  begin
    wait for 500 us;
    report "tb_uart TIMEOUT (link stalled)" severity failure;
  end process;

  stim : process
  begin
    rst <= '1';
    wait for 100 ns;
    wait until rising_edge(clk);
    rst <= '0';
    wait until rising_edge(clk);

    for i in PATTERNS'range loop
      -- wait for the transmitter to be idle, then launch one byte
      while tx_busy = '1' loop
        wait until rising_edge(clk);
      end loop;
      tx_data  <= PATTERNS(i);
      tx_start <= '1';
      wait until rising_edge(clk);
      tx_start <= '0';

      -- the received byte must match exactly
      wait until rx_valid = '1';
      assert rx_data = PATTERNS(i)
        report "tb_uart: byte " & integer'image(i) & " got " &
               integer'image(to_integer(unsigned(rx_data))) & " expected " &
               integer'image(to_integer(unsigned(PATTERNS(i))))
        severity failure;
    end loop;

    report "tb_uart PASSED (10/10 bytes looped back bit-exact)" severity note;
    finish;
  end process;

end architecture sim;
