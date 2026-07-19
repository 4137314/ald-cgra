-- pe.vhd
-- CGRA processing element: two operand muxes (N/S/E/W neighbour, immediate,
-- own register, zero), a 16-bit signed ALU and one output register.
-- The register updates only when 'step' is asserted, so the host controls
-- the array cycle by cycle through the UART protocol.

library ieee;
use ieee.std_logic_1164.all;
use ieee.numeric_std.all;
use work.cgra_pkg.all;

entity pe is
  port (
    clk  : in  std_logic;
    rst  : in  std_logic;   -- clears the output register (config lives outside)
    step : in  std_logic;
    cfg  : in  cfg_t;
    in_n : in  data_t;
    in_s : in  data_t;
    in_e : in  data_t;
    in_w : in  data_t;
    dout : out data_t
  );
end entity pe;

architecture rtl of pe is

  signal r : data_t := (others => '0');

  function pick(sel              : std_logic_vector(2 downto 0);
                n, s, e, w, k, m : data_t) return data_t is
  begin
    case sel is
      when SEL_N     => return n;
      when SEL_S     => return s;
      when SEL_E     => return e;
      when SEL_W     => return w;
      when SEL_CONST => return k;
      when SEL_SELF  => return m;
      when others    => return to_signed(0, DATA_W);
    end case;
  end function;

begin

  alu_p : process (clk)
    variable opcode : std_logic_vector(3 downto 0);
    variable imm    : data_t;
    variable a, b   : data_t;
    variable res    : data_t;
    variable prod   : signed(2 * DATA_W - 1 downto 0);
  begin
    if rising_edge(clk) then
      if rst = '1' then
        r <= (others => '0');
      elsif step = '1' then
        opcode := cfg(19 downto 16);
        imm    := signed(cfg(15 downto 0));
        a      := pick(cfg(22 downto 20), in_n, in_s, in_e, in_w, imm, r);
        b      := pick(cfg(25 downto 23), in_n, in_s, in_e, in_w, imm, r);

        case opcode is
          when OP_NOP   => res := r;
          when OP_PASS  => res := a;
          when OP_ADD   => res := a + b;
          when OP_SUB   => res := a - b;
          when OP_MUL   => prod := a * b;
                           res  := prod(DATA_W - 1 downto 0);
          when OP_MAC   => prod := a * b;
                           res  := r + prod(DATA_W - 1 downto 0);
          when OP_AND   => res := a and b;
          when OP_OR    => res := a or b;
          when OP_XOR   => res := a xor b;
          when OP_SHL   => res := shift_left(a, to_integer(unsigned(b(3 downto 0))));
          when OP_SHR   => res := shift_right(a, to_integer(unsigned(b(3 downto 0))));
          when OP_MAX   => if a >= b then res := a; else res := b; end if;
          when OP_MIN   => if a <= b then res := a; else res := b; end if;
          when OP_ABS   => res := abs a;
          when OP_ACC   => res := r + a;
          when OP_CONST => res := imm;
          when others   => res := r;
        end case;

        r <= res;
      end if;
    end if;
  end process;

  dout <= r;

end architecture rtl;
