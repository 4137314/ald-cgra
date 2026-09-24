-- Geometry contract: neighbour wiring, both operand muxes, edge injection,
-- simultaneous propagation, step hold, and reset priority for rectangular arrays.
library ieee;
use ieee.std_logic_1164.all;
use ieee.numeric_std.all;
use std.env.all;
use work.cgra_pkg.all;

entity tb_array_geometry is
  generic (G_ROWS : positive := 2; G_COLS : positive := 3);
end entity;

architecture sim of tb_array_geometry is
  constant N : positive := G_ROWS * G_COLS;
  signal clk : std_logic := '0';
  signal rst, step : std_logic := '0';
  signal cfg : cfg_vec_t(0 to N - 1) := (others => (others => '0'));
  signal west : data_vec_t(0 to G_ROWS - 1) := (others => (others => '0'));
  signal north : data_vec_t(0 to G_COLS - 1) := (others => (others => '0'));
  signal regs : data_vec_t(0 to N - 1);
begin
  clk <= not clk after 5 ns;
  dut : entity work.cgra_array
    generic map (G_ROWS => G_ROWS, G_COLS => G_COLS)
    port map (clk => clk, rst => rst, step => step, cfg => cfg,
              west_in => west, north_in => north, pe_out => regs);

  stimulus : process
    variable expected : integer;
    variable checks : natural := 0;
    procedure tick is
    begin
      wait until falling_edge(clk);
    end procedure;
    procedure check(constant i, value : integer; constant message_text : string) is
    begin
      assert to_integer(regs(i)) = value
        report message_text & " PE=" & integer'image(i) & " expected=" & integer'image(value) &
               " got=" & integer'image(to_integer(regs(i))) severity failure;
      checks := checks + 1;
    end procedure;
    procedure reset is
    begin
      rst <= '1'; tick; rst <= '0';
    end procedure;
  begin
    tick;
    for r in 0 to G_ROWS - 1 loop west(r) <= to_signed(-100 - r, DATA_W); end loop;
    for c in 0 to G_COLS - 1 loop north(c) <= to_signed(200 + c, DATA_W); end loop;

    -- Seed every PE differently, then sample each direction through A and B.
    -- This exposes stride errors and accidental propagation within one step.
    for operand in 0 to 1 loop
      for sel in 0 to 7 loop
        for i in 0 to N - 1 loop cfg(i) <= cfg_word(OP_CONST, SEL_ZERO, SEL_ZERO, 1000 + i); end loop;
        step <= '1'; tick;
        for i in 0 to N - 1 loop
          if operand = 0 then
            cfg(i) <= cfg_word(OP_PASS, std_logic_vector(to_unsigned(sel, 3)), SEL_ZERO, -7);
          else
            cfg(i) <= cfg_word(OP_ADD, SEL_ZERO, std_logic_vector(to_unsigned(sel, 3)), -7);
          end if;
        end loop;
        tick;
        for r in 0 to G_ROWS - 1 loop
          for c in 0 to G_COLS - 1 loop
            case sel is
              when 0 =>
                if r = 0 then expected := 200 + c; else expected := 1000 + (r - 1) * G_COLS + c; end if;
              when 1 =>
                if r = G_ROWS - 1 then expected := 0; else expected := 1000 + (r + 1) * G_COLS + c; end if;
              when 2 =>
                if c = G_COLS - 1 then expected := 0; else expected := 1000 + r * G_COLS + c + 1; end if;
              when 3 =>
                if c = 0 then expected := -100 - r; else expected := 1000 + r * G_COLS + c - 1; end if;
              when 4 => expected := -7;
              when 5 => expected := 1000 + r * G_COLS + c;
              when others => expected := 0;
            end case;
            check(r * G_COLS + c, expected, "operand wiring");
          end loop;
        end loop;
      end loop;
    end loop;

    -- A wave must travel one hop per logical step, including degenerate 1xN/Nx1.
    reset;
    cfg <= (others => cfg_word(OP_PASS, SEL_W, SEL_ZERO, 0));
    for k in 1 to G_COLS loop
      tick;
      for r in 0 to G_ROWS - 1 loop
        for c in 0 to G_COLS - 1 loop
          expected := 0;
          if c < k then expected := -100 - r; end if;
          check(r * G_COLS + c, expected, "west propagation");
        end loop;
      end loop;
    end loop;
    reset;
    cfg <= (others => cfg_word(OP_PASS, SEL_N, SEL_ZERO, 0));
    for k in 1 to G_ROWS loop
      tick;
      for r in 0 to G_ROWS - 1 loop
        for c in 0 to G_COLS - 1 loop
          expected := 0;
          if r < k then expected := 200 + c; end if;
          check(r * G_COLS + c, expected, "north propagation");
        end loop;
      end loop;
    end loop;

    step <= '0';
    cfg <= (others => cfg_word(OP_CONST, SEL_ZERO, SEL_ZERO, -999));
    for idle in 1 to 3 loop
      tick;
      for i in 0 to N - 1 loop check(i, 200 + i mod G_COLS, "step hold"); end loop;
    end loop;
    step <= '1'; reset;
    for i in 0 to N - 1 loop check(i, 0, "reset priority"); end loop;
    tick;
    for i in 0 to N - 1 loop check(i, -999, "configuration survives reset"); end loop;
    report "tb_array_geometry PASSED (" & positive'image(G_ROWS) & "x" &
           positive'image(G_COLS) & ", " & natural'image(checks) & " checks)";
    finish;
  end process;
end architecture;
