-- Independent digital line driver: no DUT transmitter or synchronous loopback.
-- Baud/phase offsets, consecutive frames, glitches, bad stop, break and reset.
-- This tests sampled digital behavior, not analog metastability probabilities.
library ieee;
use ieee.std_logic_1164.all;
use ieee.numeric_std.all;
use std.env.all;

entity tb_uart_async is
  generic (G_CLKS_PER_BIT : positive := 16);
end entity;
architecture sim of tb_uart_async is
  constant BIT_TIME : time := G_CLKS_PER_BIT * 10 ns;
  signal clk : std_logic := '0';
  signal rst : std_logic := '1';
  signal serial : std_logic := '1';
  signal valid : std_logic;
  signal data : std_logic_vector(7 downto 0);
  type bytes_t is array(natural range <>) of std_logic_vector(7 downto 0);
  constant PATTERNS : bytes_t := (x"00", x"FF", x"55", x"AA", x"01", x"C3");
  signal received : bytes_t(0 to 255) := (others => (others => '0'));
  signal count : natural range 0 to 256 := 0;
begin
  assert G_CLKS_PER_BIT >= 8 report "async test requires at least 8 clocks/bit" severity failure;
  clk <= not clk after 5 ns;
  dut : entity work.uart_rx generic map (CLKS_PER_BIT => G_CLKS_PER_BIT)
    port map(clk, rst, serial, valid, data);
  observe : process(clk)
    variable previous_valid : std_logic := '0';
  begin
    if rising_edge(clk) then
      if rst = '0' then
        assert not (valid = '1' and previous_valid = '1') report "RX valid wider than one clock" severity failure;
        if valid = '1' then
          assert count < received'length report "scoreboard overflow" severity failure;
          received(count) <= data;
          count <= count + 1;
        end if;
      end if;
      previous_valid := valid;
    end if;
  end process;
  watchdog : process
  begin
    wait for 100 ms;
    report "tb_uart_async TIMEOUT" severity failure;
  end process;
  stimulus : process
    variable prior, checks : natural := 0;
    variable period : time;
    procedure frame(value : std_logic_vector(7 downto 0); duration : time;
                    stop_bit : std_logic := '1') is
    begin
      serial <= '0'; wait for duration;
      for bit in 0 to 7 loop serial <= value(bit); wait for duration; end loop;
      serial <= stop_bit; wait for duration;
    end procedure;
    procedure expect_byte(index : natural; value : std_logic_vector(7 downto 0)) is
    begin
      assert received(index) = value report "independent source byte/order mismatch" severity failure;
      checks := checks + 1;
    end procedure;
  begin
    wait for 80 ns;
    wait until falling_edge(clk); rst <= '0';
    wait for 2 * BIT_TIME;
    for baud_case in -1 to 1 loop
      period := BIT_TIME + baud_case * (BIT_TIME / 50); -- -2%, nominal, +2% bit time
      for phase in 0 to 3 loop
        wait until falling_edge(clk); wait for (1 + 2 * phase) * 1 ns;
        prior := count;
        for i in PATTERNS'range loop frame(PATTERNS(i), period); end loop;
        wait for 2 * BIT_TIME;
        assert count = prior + PATTERNS'length report "dropped/extra byte in adjacent frames" severity failure;
        for i in PATTERNS'range loop expect_byte(prior + i, PATTERNS(i)); end loop;
      end loop;
    end loop;
    prior := count;
    serial <= '0'; wait for BIT_TIME / 5;
    serial <= '1'; wait for 3 * BIT_TIME;
    assert count = prior report "short start glitch accepted" severity failure;
    checks := checks + 1;

    frame(x"A5", BIT_TIME, '0');
    wait for 20 * BIT_TIME; -- keep line low: invalid stop followed by break
    assert count = prior report "bad stop/break produced a byte" severity failure;
    checks := checks + 1;
    serial <= '1'; wait for 3 * BIT_TIME;
    frame(x"69", BIT_TIME); wait for 2 * BIT_TIME;
    assert count = prior + 1 report "receiver did not recover from break" severity failure;
    expect_byte(prior, x"69");

    prior := count;
    serial <= '0'; wait for 4 * BIT_TIME;
    rst <= '1'; serial <= '1'; wait for 80 ns;
    wait until falling_edge(clk); rst <= '0'; wait for 2 * BIT_TIME;
    assert count = prior report "reset did not discard incomplete frame" severity failure;
    checks := checks + 1;
    frame(x"3C", BIT_TIME); wait for 2 * BIT_TIME;
    assert count = prior + 1 report "receiver did not recover after reset" severity failure;
    expect_byte(prior, x"3C");
    report "tb_uart_async PASSED (CLKS_PER_BIT=" & positive'image(G_CLKS_PER_BIT) &
      ", " & natural'image(checks) & " data/fault checks; 12 phase/baud bursts)";
    finish;
  end process;
end architecture;
