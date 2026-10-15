-- Reachability-only driver around the UNMODIFIED production controller.
-- No such driver is used by universal proofs. Four fixed protocol transcripts:
-- HELLO then upload, empty DOT, exact duplicate, or conflicting duplicate.
-- CRC constants independently generated using Python binascii.crc_hqx(., 65535).
library ieee;
use ieee.std_logic_1164.all;
use ieee.numeric_std.all;
use work.cgra_pkg.all;
use work.block_pkg.all;
entity block_ctrl_witness is
  generic (G_STEP_DIV : positive := 2; G_TIMEOUT_CYCLES : positive := 3);
  port (clk : in std_logic; pe_result : in data_t);
end entity;
architecture harness of block_ctrl_witness is
  signal cycle : natural range 0 to 220 := 0;
  signal choice : unsigned(1 downto 0);
  attribute anyconst : boolean;
  attribute anyconst of choice : signal is true;
  signal rst, rx_valid, tx_busy, tx_start : std_logic;
  signal rx_data : byte_t;
  constant f_hello : bytes_t(0 to 13) := (x"7E", x"01", x"00", x"44", x"33", x"22", x"11", x"00", x"00", x"00", x"00", x"8D", x"E6", x"7E");
  constant f_case_0 : bytes_t(0 to 19) := (x"7E", x"01", x"01", x"44", x"33", x"22", x"11", x"01", x"00", x"06", x"00", x"00", x"00", x"00", x"01", x"07", x"00", x"A6", x"EA", x"7E");
  constant f_case_1 : bytes_t(0 to 15) := (x"7E", x"01", x"02", x"44", x"33", x"22", x"11", x"01", x"00", x"02", x"00", x"00", x"00", x"4A", x"F2", x"7E");
  constant f_case_2 : bytes_t(0 to 13) := (x"7E", x"01", x"00", x"44", x"33", x"22", x"11", x"00", x"00", x"00", x"00", x"8D", x"E6", x"7E");
  constant f_case_3 : bytes_t(0 to 13) := (x"7E", x"01", x"04", x"44", x"33", x"22", x"11", x"00", x"00", x"00", x"00", x"62", x"7A", x"7E");
  function stimulus(cycle, choice : natural) return byte_t is
  begin
    if cycle = 1 then return x"08";
    elsif cycle >= 20 and cycle < 34 then return f_hello(cycle-20);
    else
      case choice is
        when 0 => if cycle >= 150 and cycle < 170 then return f_case_0(cycle-150); end if;
        when 1 => if cycle >= 150 and cycle < 166 then return f_case_1(cycle-150); end if;
        when 2 => if cycle >= 150 and cycle < 164 then return f_case_2(cycle-150); end if;
        when 3 => if cycle >= 150 and cycle < 164 then return f_case_3(cycle-150); end if;
        when others => null;
      end case;
    end if;
    return x"00";
  end function;

begin
  stimulus_clock : process(clk)
  begin
    if rising_edge(clk) then
      if cycle < 220 then cycle <= cycle+1; end if;
      tx_busy <= tx_start;
    end if;
  end process;
  rst <= '1' when cycle = 0 else '0';
  rx_valid <= '1' when cycle = 1 or (cycle >= 20 and cycle < 34) or
     (choice = 0 and cycle >= 150 and cycle < 170) or
     (choice = 1 and cycle >= 150 and cycle < 166) or
     (choice = 2 and cycle >= 150 and cycle < 164) or
     (choice = 3 and cycle >= 150 and cycle < 164) else '0';
  rx_data <= stimulus(cycle, to_integer(choice));
  dut : entity work.block_ctrl
    generic map (G_STEP_DIV => G_STEP_DIV, G_TIMEOUT_CYCLES => G_TIMEOUT_CYCLES)
    port map (clk, rst, '1', rx_valid, rx_data, tx_busy, tx_start, open,
              open, open, pe_result, open, open, open, open, open);
end architecture;
