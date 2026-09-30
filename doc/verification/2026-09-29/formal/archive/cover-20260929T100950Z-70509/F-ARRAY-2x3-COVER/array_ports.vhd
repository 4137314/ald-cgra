-- Flattened port adapter only; row-major element i occupies the low-first
-- word i in each packed bus. The DUT remains production cgra_array + pe.
library ieee;
use ieee.std_logic_1164.all;
use ieee.numeric_std.all;
use work.cgra_pkg.all;

entity array_ports is
  generic (G_ROWS : positive := 4; G_COLS : positive := 4);
  port (clk, rst, step : in std_logic;
        cfg : in std_logic_vector(32*G_ROWS*G_COLS-1 downto 0);
        west : in std_logic_vector(16*G_ROWS-1 downto 0);
        north : in std_logic_vector(16*G_COLS-1 downto 0);
        q : out std_logic_vector(16*G_ROWS*G_COLS-1 downto 0));
end entity;

architecture formal_adapter of array_ports is
  signal configs : cfg_vec_t(0 to G_ROWS*G_COLS-1);
  signal west_in : data_vec_t(0 to G_ROWS-1);
  signal north_in : data_vec_t(0 to G_COLS-1);
  signal values : data_vec_t(0 to G_ROWS*G_COLS-1);
begin
  gen_pe : for i in 0 to G_ROWS*G_COLS-1 generate
    configs(i) <= cfg(32*i+31 downto 32*i);
    q(16*i+15 downto 16*i) <= std_logic_vector(values(i));
  end generate;
  gen_w : for i in 0 to G_ROWS-1 generate
    west_in(i) <= signed(west(16*i+15 downto 16*i));
  end generate;
  gen_n : for i in 0 to G_COLS-1 generate
    north_in(i) <= signed(north(16*i+15 downto 16*i));
  end generate;
  dut : entity work.cgra_array
    generic map (G_ROWS => G_ROWS, G_COLS => G_COLS)
    port map (clk, rst, step, configs, west_in, north_in, values);
end architecture;
