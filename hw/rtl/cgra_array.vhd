-- cgra_array.vhd
-- ROWS x COLS mesh of processing elements.
-- Each PE reads the registered outputs of its 4 neighbours.
-- Edge connections:
--   * north edge (row 0)      : north_in(col)  -- operand injection per column
--   * west edge  (column 0)   : west_in(row)   -- operand injection per row
--   * south / east edges      : constant zero
-- All PE registers are exposed row-major on pe_out for readback.

library ieee;
use ieee.std_logic_1164.all;
use ieee.numeric_std.all;
use work.cgra_pkg.all;

entity cgra_array is
  port (
    clk      : in  std_logic;
    rst      : in  std_logic;
    step     : in  std_logic;
    cfg      : in  cfg_vec_t(0 to NUM_PE - 1);
    west_in  : in  data_vec_t(0 to ROWS - 1);
    north_in : in  data_vec_t(0 to COLS - 1);
    pe_out   : out data_vec_t(0 to NUM_PE - 1)
  );
end entity cgra_array;

architecture rtl of cgra_array is
  signal q : data_vec_t(0 to NUM_PE - 1);
  constant ZERO : data_t := (others => '0');
begin

  gen_rows : for r in 0 to ROWS - 1 generate
    gen_cols : for c in 0 to COLS - 1 generate
      signal n_i, s_i, e_i, w_i : data_t;
    begin

      g_n_edge : if r = 0 generate
        n_i <= north_in(c);
      end generate;
      g_n_int : if r > 0 generate
        n_i <= q((r - 1) * COLS + c);
      end generate;

      g_s_edge : if r = ROWS - 1 generate
        s_i <= ZERO;
      end generate;
      g_s_int : if r < ROWS - 1 generate
        s_i <= q((r + 1) * COLS + c);
      end generate;

      g_w_edge : if c = 0 generate
        w_i <= west_in(r);
      end generate;
      g_w_int : if c > 0 generate
        w_i <= q(r * COLS + c - 1);
      end generate;

      g_e_edge : if c = COLS - 1 generate
        e_i <= ZERO;
      end generate;
      g_e_int : if c < COLS - 1 generate
        e_i <= q(r * COLS + c + 1);
      end generate;

      u_pe : entity work.pe
        port map (
          clk  => clk,
          rst  => rst,
          step => step,
          cfg  => cfg(r * COLS + c),
          in_n => n_i,
          in_s => s_i,
          in_e => e_i,
          in_w => w_i,
          dout => q(r * COLS + c)
        );

    end generate;
  end generate;

  pe_out <= q;

end architecture rtl;
