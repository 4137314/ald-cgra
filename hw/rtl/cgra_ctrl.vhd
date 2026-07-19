-- cgra_ctrl.vhd
-- UART command interpreter / CGRA controller (protocol v2).
--
-- Transactions (see cgra_pkg for the command bytes):
--   CMD_ID  : reply ID_BYTE0, PROTO_VER, ROWS, COLS, DATA_W
--   CMD_CFG : receive NUM_PE 32-bit config words (little endian, row-major)
--             + 1 checksum byte; reply ACK if the checksum matches, else NACK
--   CMD_WR  : receive ROWS+COLS 16-bit words (west 0..3 then north 0..3,
--             little endian) + 1 checksum byte; reply ACK / NACK
--   CMD_RUN : receive 1 byte N, pulse 'step' for N clock cycles, reply ACK
--   CMD_RD  : send NUM_PE 16-bit PE registers (little endian, row-major)
--             + 1 checksum byte
--   CMD_RST : pulse dp_rst (clears PE registers, keeps configuration), ACK
--   other   : reply NACK
--
-- The checksum is the 8-bit sum (mod 256) of the payload bytes. A watchdog
-- aborts a partially received command after G_TIMEOUT_CYCLES without a byte,
-- so a desynchronised host cannot wedge the FSM.

library ieee;
use ieee.std_logic_1164.all;
use ieee.numeric_std.all;
use work.cgra_pkg.all;

entity cgra_ctrl is
  generic (
    G_TIMEOUT_CYCLES : natural := 100_000_000  -- 1 s at 100 MHz
  );
  port (
    clk      : in  std_logic;
    rst      : in  std_logic;

    -- UART receive side
    rx_data  : in  std_logic_vector(7 downto 0);
    rx_valid : in  std_logic;

    -- UART transmit side
    tx_data  : out std_logic_vector(7 downto 0);
    tx_start : out std_logic;
    tx_busy  : in  std_logic;

    -- CGRA fabric
    cfg      : out cfg_vec_t(0 to NUM_PE - 1);
    west_o   : out data_vec_t(0 to ROWS - 1);
    north_o  : out data_vec_t(0 to COLS - 1);
    step     : out std_logic;
    dp_rst   : out std_logic;
    pe_regs  : in  data_vec_t(0 to NUM_PE - 1);

    busy     : out std_logic
  );
end entity cgra_ctrl;

architecture rtl of cgra_ctrl is

  type state_t is (S_IDLE, S_ID, S_CFG, S_CFG_CK, S_WR, S_WR_CK,
                   S_RUN_ARG, S_RUN, S_RD, S_RD_CK, S_SEND, S_SEND_WAIT);
  signal state     : state_t := S_IDLE;
  signal ret_state : state_t := S_IDLE;

  signal cfg_r   : cfg_vec_t(0 to NUM_PE - 1)  := (others => (others => '0'));
  signal west_r  : data_vec_t(0 to ROWS - 1)   := (others => (others => '0'));
  signal north_r : data_vec_t(0 to COLS - 1)   := (others => (others => '0'));

  signal shift    : std_logic_vector(23 downto 0) := (others => '0');
  signal byte_idx : natural range 0 to 3 := 0;
  signal word_idx : natural range 0 to NUM_PE - 1 := 0;
  signal run_cnt  : unsigned(7 downto 0) := (others => '0');
  signal tx_byte  : std_logic_vector(7 downto 0) := (others => '0');
  signal wd       : natural range 0 to G_TIMEOUT_CYCLES := 0;
  signal cksum    : unsigned(7 downto 0) := (others => '0');
  signal id_idx   : natural range 0 to 4 := 0;

  -- ID reply payload
  type id_arr_t is array (0 to 4) of std_logic_vector(7 downto 0);
  constant ID_BYTES : id_arr_t := (
    ID_BYTE0,
    PROTO_VER,
    std_logic_vector(to_unsigned(ROWS, 8)),
    std_logic_vector(to_unsigned(COLS, 8)),
    std_logic_vector(to_unsigned(DATA_W, 8))
  );

begin

  cfg     <= cfg_r;
  west_o  <= west_r;
  north_o <= north_r;
  tx_data <= tx_byte;
  busy    <= '0' when state = S_IDLE else '1';

  fsm_p : process (clk)
  begin
    if rising_edge(clk) then
      -- single-cycle pulses
      tx_start <= '0';
      step     <= '0';
      dp_rst   <= '0';

      if rst = '1' then
        state    <= S_IDLE;
        cfg_r    <= (others => (others => '0'));
        west_r   <= (others => (others => '0'));
        north_r  <= (others => (others => '0'));
        run_cnt  <= (others => '0');
        word_idx <= 0;
        byte_idx <= 0;
        wd       <= 0;
        cksum    <= (others => '0');
      else

        -- watchdog: only ticks while waiting for payload bytes
        case state is
          when S_CFG | S_CFG_CK | S_WR | S_WR_CK | S_RUN_ARG =>
            if rx_valid = '1' then
              wd <= 0;
            elsif wd = G_TIMEOUT_CYCLES then
              wd    <= 0;
              state <= S_IDLE;
            else
              wd <= wd + 1;
            end if;
          when others =>
            wd <= 0;
        end case;

        case state is

          when S_IDLE =>
            word_idx <= 0;
            byte_idx <= 0;
            if rx_valid = '1' then
              case rx_data is
                when CMD_ID =>
                  id_idx <= 0;
                  state  <= S_ID;
                when CMD_CFG =>
                  cksum <= (others => '0');
                  state <= S_CFG;
                when CMD_WR =>
                  cksum <= (others => '0');
                  state <= S_WR;
                when CMD_RUN =>
                  state <= S_RUN_ARG;
                when CMD_RD =>
                  cksum <= (others => '0');
                  state <= S_RD;
                when CMD_RST =>
                  dp_rst    <= '1';
                  tx_byte   <= RSP_ACK;
                  ret_state <= S_IDLE;
                  state     <= S_SEND;
                when others =>
                  tx_byte   <= RSP_NACK;
                  ret_state <= S_IDLE;
                  state     <= S_SEND;
              end case;
            end if;

          when S_ID =>
            tx_byte <= ID_BYTES(id_idx);
            if id_idx = 4 then
              ret_state <= S_IDLE;
            else
              id_idx    <= id_idx + 1;
              ret_state <= S_ID;
            end if;
            state <= S_SEND;

          when S_CFG =>
            if rx_valid = '1' then
              cksum <= cksum + unsigned(rx_data);
              if byte_idx = 3 then
                cfg_r(word_idx) <= rx_data & shift;
                byte_idx <= 0;
                if word_idx = NUM_PE - 1 then
                  word_idx <= 0;
                  state    <= S_CFG_CK;
                else
                  word_idx <= word_idx + 1;
                end if;
              else
                shift    <= rx_data & shift(23 downto 8);
                byte_idx <= byte_idx + 1;
              end if;
            end if;

          when S_CFG_CK =>
            if rx_valid = '1' then
              if unsigned(rx_data) = cksum then
                tx_byte <= RSP_ACK;
              else
                tx_byte <= RSP_NACK;
              end if;
              ret_state <= S_IDLE;
              state     <= S_SEND;
            end if;

          when S_WR =>
            if rx_valid = '1' then
              cksum <= cksum + unsigned(rx_data);
              if byte_idx = 1 then
                if word_idx < ROWS then
                  west_r(word_idx) <=
                    signed(std_logic_vector'(rx_data & shift(23 downto 16)));
                else
                  north_r(word_idx - ROWS) <=
                    signed(std_logic_vector'(rx_data & shift(23 downto 16)));
                end if;
                byte_idx <= 0;
                if word_idx = ROWS + COLS - 1 then
                  word_idx <= 0;
                  state    <= S_WR_CK;
                else
                  word_idx <= word_idx + 1;
                end if;
              else
                shift(23 downto 16) <= rx_data;
                byte_idx <= 1;
              end if;
            end if;

          when S_WR_CK =>
            if rx_valid = '1' then
              if unsigned(rx_data) = cksum then
                tx_byte <= RSP_ACK;
              else
                tx_byte <= RSP_NACK;
              end if;
              ret_state <= S_IDLE;
              state     <= S_SEND;
            end if;

          when S_RUN_ARG =>
            if rx_valid = '1' then
              run_cnt <= unsigned(rx_data);
              state   <= S_RUN;
            end if;

          when S_RUN =>
            if run_cnt = 0 then
              tx_byte   <= RSP_ACK;
              ret_state <= S_IDLE;
              state     <= S_SEND;
            else
              step    <= '1';
              run_cnt <= run_cnt - 1;
            end if;

          when S_RD =>
            if byte_idx = 0 then
              tx_byte   <= std_logic_vector(pe_regs(word_idx)(7 downto 0));
              cksum     <= cksum + unsigned(pe_regs(word_idx)(7 downto 0));
              byte_idx  <= 1;
              ret_state <= S_RD;
              state     <= S_SEND;
            else
              tx_byte  <= std_logic_vector(pe_regs(word_idx)(15 downto 8));
              cksum    <= cksum + unsigned(pe_regs(word_idx)(15 downto 8));
              byte_idx <= 0;
              if word_idx = NUM_PE - 1 then
                word_idx  <= 0;
                ret_state <= S_RD_CK;
              else
                word_idx  <= word_idx + 1;
                ret_state <= S_RD;
              end if;
              state <= S_SEND;
            end if;

          when S_RD_CK =>
            tx_byte   <= std_logic_vector(cksum);
            ret_state <= S_IDLE;
            state     <= S_SEND;

          when S_SEND =>
            if tx_busy = '0' then
              tx_start <= '1';
              state    <= S_SEND_WAIT;
            end if;

          when S_SEND_WAIT =>
            if tx_busy = '1' then
              state <= ret_state;
            end if;

        end case;
      end if;
    end if;
  end process;

end architecture rtl;
