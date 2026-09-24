-- uart_rx.vhd
-- UART receiver, 8N1, mid-bit sampling. rx_valid pulses one clock per byte.

library ieee;
use ieee.std_logic_1164.all;
use ieee.numeric_std.all;

entity uart_rx is
  generic (
    CLKS_PER_BIT : natural := 868  -- clock frequency / baud rate
  );
  port (
    clk       : in  std_logic;
    rst       : in  std_logic;
    rx_serial : in  std_logic;
    rx_valid  : out std_logic;
    rx_data   : out std_logic_vector(7 downto 0)
  );
end entity uart_rx;

architecture rtl of uart_rx is
  type state_t is (S_IDLE, S_START, S_DATA, S_STOP, S_BREAK);
  signal state   : state_t := S_IDLE;
  signal clk_cnt : natural range 0 to CLKS_PER_BIT - 1 := 0;
  signal bit_idx : natural range 0 to 7 := 0;
  signal data_r  : std_logic_vector(7 downto 0) := (others => '0');
  signal rx_s1   : std_logic := '1';
  signal rx_s2   : std_logic := '1';
begin

  -- two-flop synchroniser for the asynchronous serial input
  sync_p : process (clk)
  begin
    if rising_edge(clk) then
      rx_s1 <= rx_serial;
      rx_s2 <= rx_s1;
    end if;
  end process;

  fsm_p : process (clk)
  begin
    if rising_edge(clk) then
      rx_valid <= '0';
      if rst = '1' then
        state   <= S_IDLE;
        clk_cnt <= 0;
        bit_idx <= 0;
      else
        case state is

          when S_IDLE =>
            clk_cnt <= 0;
            bit_idx <= 0;
            if rx_s2 = '0' then
              state <= S_START;
            end if;

          when S_START =>
            if clk_cnt = (CLKS_PER_BIT - 1) / 2 then
              clk_cnt <= 0;
              if rx_s2 = '0' then   -- confirmed start bit at mid-point
                state <= S_DATA;
              else                  -- glitch, go back to idle
                state <= S_IDLE;
              end if;
            else
              clk_cnt <= clk_cnt + 1;
            end if;

          when S_DATA =>
            if clk_cnt = CLKS_PER_BIT - 1 then
              clk_cnt <= 0;
              data_r  <= rx_s2 & data_r(7 downto 1);  -- LSB first
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
            if clk_cnt = CLKS_PER_BIT - 1 then
              clk_cnt  <= 0;
              if rx_s2 = '1' then
                rx_data  <= data_r;
                rx_valid <= '1';
                state    <= S_IDLE;
              else
                -- Framing error: discard the byte and wait for the line to
                -- return idle, so a break cannot generate repeated commands.
                state <= S_BREAK;
              end if;
            else
              clk_cnt <= clk_cnt + 1;
            end if;

          when S_BREAK =>
            if rx_s2 = '1' then
              state <= S_IDLE;
            end if;

        end case;
      end if;
    end if;
  end process;

end architecture rtl;
