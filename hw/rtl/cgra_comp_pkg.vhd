-- cgra_comp_pkg.vhd
-- Component declarations for the CGRA building blocks, so the structural
-- parents (cgra_array, cgra_top) instantiate by component rather than binding
-- to an entity directly. Keeps the netlist wiring in one place and lets a
-- module be swapped for a compatible one via a configuration if ever needed.
--
-- Every component here MUST mirror its entity in rtl/*.vhd exactly.

library ieee;
use ieee.std_logic_1164.all;
use ieee.numeric_std.all;
use work.cgra_pkg.all;

package cgra_comp_pkg is

  component uart_rx is
    generic (CLKS_PER_BIT : natural := 868);
    port (
      clk       : in  std_logic;
      rst       : in  std_logic;
      rx_serial : in  std_logic;
      rx_valid  : out std_logic;
      rx_data   : out std_logic_vector(7 downto 0)
    );
  end component uart_rx;

  component uart_tx is
    generic (CLKS_PER_BIT : natural := 868);
    port (
      clk       : in  std_logic;
      rst       : in  std_logic;
      tx_start  : in  std_logic;
      tx_data   : in  std_logic_vector(7 downto 0);
      tx_serial : out std_logic;
      tx_busy   : out std_logic
    );
  end component uart_tx;

  component pe is
    port (
      clk   : in  std_logic;
      rst   : in  std_logic;
      step  : in  std_logic;
      cfg   : in  cfg_t;
      neigh : in  pe_neigh_t;
      dout  : out data_t
    );
  end component pe;

  component cgra_array is
    generic (
      G_ROWS : positive := ROWS;
      G_COLS : positive := COLS
    );
    port (
      clk      : in  std_logic;
      rst      : in  std_logic;
      step     : in  std_logic;
      cfg      : in  cfg_vec_t(0 to G_ROWS * G_COLS - 1);
      west_in  : in  data_vec_t(0 to G_ROWS - 1);
      north_in : in  data_vec_t(0 to G_COLS - 1);
      pe_out   : out data_vec_t(0 to G_ROWS * G_COLS - 1)
    );
  end component cgra_array;

  component cgra_ctrl is
    generic (
      G_TIMEOUT_CYCLES : natural := 100_000_000;
      G_STEP_DIV       : positive := 2;
      G_ROWS           : positive := ROWS;
      G_COLS           : positive := COLS
    );
    port (
      clk      : in  std_logic;
      rst      : in  std_logic;
      rx_data  : in  std_logic_vector(7 downto 0);
      rx_valid : in  std_logic;
      tx_data  : out std_logic_vector(7 downto 0);
      tx_start : out std_logic;
      tx_busy  : in  std_logic;
      cfg      : out cfg_vec_t(0 to G_ROWS * G_COLS - 1);
      west_o   : out data_vec_t(0 to G_ROWS - 1);
      north_o  : out data_vec_t(0 to G_COLS - 1);
      step     : out std_logic;
      dp_rst   : out std_logic;
      pe_regs  : in  data_vec_t(0 to G_ROWS * G_COLS - 1);
      busy     : out std_logic
    );
  end component cgra_ctrl;

end package cgra_comp_pkg;
