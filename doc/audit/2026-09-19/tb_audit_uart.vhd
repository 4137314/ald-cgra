library ieee;
use ieee.std_logic_1164.all;
use std.env.all;
entity tb_audit_uart is end;
architecture sim of tb_audit_uart is
 signal clk : std_logic := '0'; signal rst : std_logic := '1';
 signal line_rx : std_logic := '1';signal valid : std_logic;signal data : std_logic_vector(7 downto 0);
begin
 clk <= not clk after 50 ns;
 dut : entity work.uart_rx generic map(CLKS_PER_BIT=>10) port map(clk,rst,line_rx,valid,data);
 process begin
  wait for 2 us;rst <= '0';wait for 2 us;
  line_rx <= '0'; -- start + zero payload + INVALID zero stop bit
  wait for 12 us;
  finish;
 end process;
 process(clk) begin
  if rising_edge(clk) and valid='1' then
   assert false report "AUDIT: UART accepted a byte with stop bit held LOW" severity failure;
  end if;
 end process;
end;
