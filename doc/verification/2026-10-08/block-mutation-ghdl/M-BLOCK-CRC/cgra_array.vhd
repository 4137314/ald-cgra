-- cgra_array.vhd
-- G_ROWS x G_COLS mesh of processing elements.
-- Each PE reads the registered outputs of its 4 neighbours, bundled into a
-- pe_neigh_t record. Edge connections:
--   * north edge (row 0)      : north_in(col)  -- operand injection per column
--   * west edge  (column 0)   : west_in(row)   -- operand injection per row
--   * south / east edges      : constant zero
-- All PE registers are exposed row-major on pe_out for readback.
--
-- The generics default to the package geometry (4x4); the array ports are
-- sized from the generics, with row-major PE indexing starting at zero.

library ieee;
use ieee.std_logic_1164.all;
use ieee.numeric_std.all;
use work.cgra_pkg.all;
use work.cgra_comp_pkg.all;

entity cgra_array is
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
end entity cgra_array;

architecture rtl of cgra_array is
  constant N_PE : natural := G_ROWS * G_COLS;
  signal   q    : data_vec_t(0 to N_PE - 1);
  constant ZERO : data_t := (others => '0');
begin

  gen_rows : for r in 0 to G_ROWS - 1 generate
    gen_cols : for c in 0 to G_COLS - 1 generate
      signal nb : pe_neigh_t;
    begin

      -- nearest-neighbour wiring, with edge injection / zero on the borders.
      -- Separate if-generates (not "when/else") so the out-of-range neighbour
      -- index is never elaborated on the edge cells.
      g_n_edge : if r = 0          generate nb.n <= north_in(c);                end generate;
      g_n_int  : if r > 0          generate nb.n <= q((r - 1) * G_COLS + c);     end generate;

      g_s_edge : if r = G_ROWS - 1 generate nb.s <= ZERO;                        end generate;
      g_s_int  : if r < G_ROWS - 1 generate nb.s <= q((r + 1) * G_COLS + c);     end generate;

      g_w_edge : if c = 0          generate nb.w <= west_in(r);                  end generate;
      g_w_int  : if c > 0          generate nb.w <= q(r * G_COLS + c - 1);       end generate;

      g_e_edge : if c = G_COLS - 1 generate nb.e <= ZERO;                        end generate;
      g_e_int  : if c < G_COLS - 1 generate nb.e <= q(r * G_COLS + c + 1);       end generate;

      u_pe : pe
        port map (
          clk   => clk,
          rst   => rst,
          step  => step,
          cfg   => cfg(r * G_COLS + c),
          neigh => nb,
          dout  => q(r * G_COLS + c)
        );

    end generate;
  end generate;

  pe_out <= q;

end architecture rtl;
