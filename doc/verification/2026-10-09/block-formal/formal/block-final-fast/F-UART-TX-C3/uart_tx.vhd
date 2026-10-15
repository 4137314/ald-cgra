-- uart_tx.vhd
-- UART transmitter, 8N1. Pulse tx_start with tx_data valid while tx_busy = '0'.

library ieee;
use ieee.std_logic_1164.all;
use ieee.numeric_std.all;

entity uart_tx is
  generic (
    CLKS_PER_BIT : natural := 868  -- clock frequency / baud rate
  );
  port (
    clk       : in  std_logic;
    rst       : in  std_logic;
    tx_start  : in  std_logic;
    tx_data   : in  std_logic_vector(7 downto 0);
    tx_serial : out std_logic;
    tx_busy   : out std_logic
  );
end entity uart_tx;

architecture rtl of uart_tx is
  type state_t is (S_IDLE, S_START, S_DATA, S_STOP);
  signal state   : state_t := S_IDLE;
  signal clk_cnt : natural range 0 to CLKS_PER_BIT - 1 := 0;
  signal bit_idx : natural range 0 to 7 := 0;
  signal data_r  : std_logic_vector(7 downto 0) := (others => '0');
begin

  fsm_p : process (clk)
  begin
    if rising_edge(clk) then
      if rst = '1' then
        state     <= S_IDLE;
        tx_serial <= '1';
        tx_busy   <= '0';
        clk_cnt   <= 0;
        bit_idx   <= 0;
      else
        case state is

          when S_IDLE =>
            tx_serial <= '1';
            tx_busy   <= '0';
            clk_cnt   <= 0;
            bit_idx   <= 0;
            if tx_start = '1' then
              data_r  <= tx_data;
              tx_busy <= '1';
              state   <= S_START;
            end if;

          when S_START =>
            tx_serial <= '0';
            if clk_cnt = CLKS_PER_BIT - 1 then
              clk_cnt <= 0;
              state   <= S_DATA;
            else
              clk_cnt <= clk_cnt + 1;
            end if;

          when S_DATA =>
            tx_serial <= data_r(bit_idx);  -- LSB first
            if clk_cnt = CLKS_PER_BIT - 1 then
              clk_cnt <= 0;
              if bit_idx = 7 then
                bit_idx <= 0;
                state   <= S_STOP;
              else
                bit_idx <= bit_idx + 1;
              end if;
            else
              clk_cnt <= clk_cnt + 1;
            end if;

          when S_STOP =>
            tx_serial <= '1';
            if clk_cnt = CLKS_PER_BIT - 1 then
              clk_cnt <= 0;
              state   <= S_IDLE;
            else
              clk_cnt <= clk_cnt + 1;
            end if;

        end case;
      end if;
    end if;
  end process;

end architecture rtl;
