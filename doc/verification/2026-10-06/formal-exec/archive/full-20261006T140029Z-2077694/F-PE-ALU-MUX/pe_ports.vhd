-- Port adapter only. The DUT is the production pe, with its full 16-bit ALU.
library ieee;
use ieee.std_logic_1164.all;
use ieee.numeric_std.all;
use work.cgra_pkg.all;

entity pe_ports is
  port (clk, rst, step : in std_logic;
        cfg : in std_logic_vector(31 downto 0);
        n, s, e, w : in std_logic_vector(15 downto 0);
        q : out std_logic_vector(15 downto 0));
end entity;

architecture formal_adapter of pe_ports is
  signal neighbours : pe_neigh_t;
  signal value : data_t;
begin
  neighbours <= (signed(n), signed(s), signed(e), signed(w));
  dut : entity work.pe port map(clk, rst, step, cfg, neighbours, value);
  q <= std_logic_vector(value);
end architecture;
