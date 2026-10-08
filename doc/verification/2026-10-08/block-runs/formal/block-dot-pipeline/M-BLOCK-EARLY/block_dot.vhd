-- Two resident banks with synchronous reads. Reuses PE0 of the production
-- array as a MAC; all other PEs are configured NOP by the top-level mux.
-- No memory reset: lengths/session in the controller guard every read.
library ieee;
use ieee.std_logic_1164.all;
use ieee.numeric_std.all;
use work.cgra_pkg.all;
use work.block_pkg.all;
entity block_dot is
  generic (G_STEP_DIV : positive := 2);
  port (clk, rst : in std_logic;
        wr_en, wr_bank : in std_logic;
        wr_addr : in natural range 0 to BLOCK_WORDS-1;
        wr_data : in data_t;
        start : in std_logic; count : in natural range 0 to BLOCK_WORDS;
        pe_result : in data_t;
        busy, done, step, dp_rst : out std_logic;
        a, b, result : out data_t);
end entity;
architecture rtl of block_dot is
  type ram_t is array (0 to BLOCK_WORDS-1) of data_t;
  signal bank_a, bank_b : ram_t;
  signal qa, qb : data_t;
  signal operand_a, operand_b : data_t := (others => '0');
  -- Explicit launch registers for the guarded MAC path. Keep these external
  -- to the BRAM so physical endpoint checks can verify all 32 C->PE D paths.
  attribute DONT_TOUCH : string;
  attribute DONT_TOUCH of operand_a, operand_b : signal is "TRUE";
  type state_t is (S_IDLE, S_RESET, S_FETCH, S_READ, S_WAIT, S_CAPTURE, S_RESULT);
  signal state : state_t := S_IDLE;
  signal index : natural range 0 to BLOCK_WORDS-1 := 0;
  signal remaining : natural range 0 to BLOCK_WORDS := 0;
  signal guard : natural range 0 to G_STEP_DIV := 0;
begin
  -- A conventional simple-dual-port synchronous RAM template. Physical BRAM
  -- usage must be checked in the synthesis report for the selected device.
  memory : process(clk)
  begin
    if rising_edge(clk) then
      if wr_en = '1' and state = S_IDLE and rst = '0' then
        if wr_bank = '0' then bank_a(wr_addr) <= wr_data;
        else bank_b(wr_addr) <= wr_data; end if;
      end if;
      qa <= bank_a(index); qb <= bank_b(index);
    end if;
  end process;
  a <= operand_a; b <= operand_b;
  busy <= '0' when state = S_IDLE else '1';
  dp_rst <= '1' when state = S_RESET else '0';
  step <= '1' when state = S_CAPTURE or state = S_READ else '0';
  process(clk)
  begin
    if rising_edge(clk) then
      done <= '0';
      if rst = '1' then
        state <= S_IDLE; index <= 0; remaining <= 0; guard <= 0;
        result <= (others => '0');
        operand_a <= (others => '0'); operand_b <= (others => '0');
      else
        case state is
          when S_IDLE =>
            if start = '1' then
              index <= 0; remaining <= count; state <= S_RESET;
            end if;
          when S_RESET =>
            if remaining = 0 then state <= S_RESULT;
            else state <= S_FETCH; end if;
          when S_FETCH => state <= S_READ; -- synchronous RAM data arrives here
          when S_READ =>
            operand_a <= qa; operand_b <= qb;
            guard <= G_STEP_DIV; state <= S_WAIT;
          when S_WAIT =>
            if guard = 1 then state <= S_CAPTURE;
            else guard <= guard - 1; end if;
          when S_CAPTURE =>
            remaining <= remaining - 1;
            if remaining = 1 then state <= S_RESULT;
            else index <= index + 1; state <= S_FETCH; end if;
          when S_RESULT =>
            result <= pe_result; done <= '1'; state <= S_IDLE;
        end case;
      end if;
    end if;
  end process;
end architecture;
