-- tb_pe.vhd
-- Unit test for the processing element ALU and operand muxes.

library ieee;
use ieee.std_logic_1164.all;
use ieee.numeric_std.all;
use work.cgra_pkg.all;

entity tb_pe is
end entity tb_pe;

architecture sim of tb_pe is
  constant CLK_PERIOD : time := 10 ns;

  signal clk  : std_logic := '0';
  signal rst  : std_logic := '1';
  signal step : std_logic := '0';
  signal cfg  : cfg_t := (others => '0');
  signal n, s, e, w : data_t := (others => '0');
  signal dout : data_t;
  signal done : boolean := false;
begin

  clk_p : process
  begin
    while not done loop
      clk <= '0'; wait for CLK_PERIOD / 2;
      clk <= '1'; wait for CLK_PERIOD / 2;
    end loop;
    wait;
  end process;

  dut : entity work.pe
    port map (
      clk  => clk,
      rst  => rst,
      step => step,
      cfg  => cfg,
      in_n => n,
      in_s => s,
      in_e => e,
      in_w => w,
      dout => dout
    );

  stim_p : process
    procedure do_step is
    begin
      wait until falling_edge(clk);
      step <= '1';
      wait until falling_edge(clk);
      step <= '0';
    end procedure;

    procedure do_reset is
    begin
      wait until falling_edge(clk);
      rst <= '1';
      wait until falling_edge(clk);
      rst <= '0';
    end procedure;

    procedure check(constant expected : in integer;
                    constant msg      : in string) is
    begin
      assert to_integer(dout) = expected
        report "tb_pe: " & msg & " expected " & integer'image(expected)
             & " got " & integer'image(to_integer(dout))
        severity failure;
    end procedure;
  begin
    do_reset;

    -- ADD: 10 + (-3) = 7
    n <= to_signed(10, DATA_W);
    s <= to_signed(-3, DATA_W);
    cfg <= cfg_word(OP_ADD, SEL_N, SEL_S, 0);
    do_step;
    check(7, "ADD");

    -- SUB: 10 - (-3) = 13
    cfg <= cfg_word(OP_SUB, SEL_N, SEL_S, 0);
    do_step;
    check(13, "SUB");

    -- MUL: -4 * 5 = -20
    n <= to_signed(-4, DATA_W);
    s <= to_signed(5, DATA_W);
    cfg <= cfg_word(OP_MUL, SEL_N, SEL_S, 0);
    do_step;
    check(-20, "MUL");

    -- MAC: reset, then 2*3 + 4*5 = 26
    do_reset;
    cfg <= cfg_word(OP_MAC, SEL_N, SEL_S, 0);
    n <= to_signed(2, DATA_W);
    s <= to_signed(3, DATA_W);
    do_step;
    check(6, "MAC step 1");
    n <= to_signed(4, DATA_W);
    s <= to_signed(5, DATA_W);
    do_step;
    check(26, "MAC step 2");

    -- SHL: 3 << 2 = 12
    n <= to_signed(3, DATA_W);
    s <= to_signed(2, DATA_W);
    cfg <= cfg_word(OP_SHL, SEL_N, SEL_S, 0);
    do_step;
    check(12, "SHL");

    -- MAX(-5, 3) = 3 with immediate operand
    n <= to_signed(-5, DATA_W);
    cfg <= cfg_word(OP_MAX, SEL_N, SEL_CONST, 3);
    do_step;
    check(3, "MAX imm");

    -- ABS(-9) = 9
    n <= to_signed(-9, DATA_W);
    cfg <= cfg_word(OP_ABS, SEL_N, SEL_ZERO, 0);
    do_step;
    check(9, "ABS");

    -- CONST: load immediate 1234
    cfg <= cfg_word(OP_CONST, SEL_ZERO, SEL_ZERO, 1234);
    do_step;
    check(1234, "CONST");

    -- NOP keeps the register
    cfg <= cfg_word(OP_NOP, SEL_N, SEL_S, 0);
    do_step;
    check(1234, "NOP");

    -- PASS from the SELF feedback path
    cfg <= cfg_word(OP_ACC, SEL_CONST, SEL_ZERO, 1);
    do_step;
    check(1235, "ACC imm");

    report "tb_pe PASSED";
    done <= true;
    wait;
  end process;

end architecture sim;
