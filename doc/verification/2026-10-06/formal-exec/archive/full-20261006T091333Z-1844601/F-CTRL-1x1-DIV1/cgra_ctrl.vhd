-- cgra_ctrl.vhd
-- UART command interpreter / CGRA controller (protocol v3).
--
-- Transactions (see cgra_pkg for the command bytes and the v3 EXEC framing):
--   CMD_ID   : reply ID_BYTE0, PROTO_VER, ROWS, COLS, DATA_W
--   CMD_CFG  : receive NUM_PE 32-bit config words (little endian, row-major)
--              + 1 checksum byte; reply ACK if the checksum matches, else NACK
--   CMD_WR   : receive ROWS+COLS 16-bit words (all west rows then north columns,
--              little endian) + 1 checksum byte; reply ACK / NACK
--   CMD_RUN  : receive 1 byte N, pulse 'step' for N logical steps, reply ACK
--   CMD_RD   : send NUM_PE 16-bit PE registers (little endian, row-major)
--              + 1 checksum byte
--   CMD_RST  : pulse dp_rst (clears PE registers, keeps configuration), ACK
--   CMD_EXEC : v3 fused transaction -- receive steps, flags, a 16-bit tap mask
--              and the same edge-input payload as CMD_WR, then (checksum
--              permitting) optionally clear the datapath, step the array, and
--              stream back only the masked PE registers, a data checksum and a
--              status byte. One round trip instead of WR + RUN + RD.
--   other    : reply NACK
--
-- The checksum is the 8-bit sum (mod 256) of the payload bytes. A watchdog
-- aborts a partially received command after G_TIMEOUT_CYCLES without a byte,
-- so a desynchronised host cannot wedge the FSM.
--
-- Structure notes:
--   * S_WR is shared by CMD_WR and CMD_EXEC; wr_ret says where to go after the
--     2*(G_ROWS+G_COLS) input bytes (the checksum state of either command).
--   * S_RUN is shared by CMD_RUN and CMD_EXEC; run_ret says where to go when
--     the step count reaches zero (plain ACK, or the EXEC read-back phase).
--   * The PE read-back multiplexer output is REGISTERED into rd_word before it
--     reaches the checksum adder, so the word_idx -> cksum path is a mux OR an
--     adder, never both in one clock (this was the design's control-path wall
--     once the datapath multicycle exception was in place; see hw/perf/PERFLOG).

library ieee;
use ieee.std_logic_1164.all;
use ieee.numeric_std.all;
use work.cgra_pkg.all;

entity cgra_ctrl is
  generic (
    G_TIMEOUT_CYCLES : natural := 100_000_000; -- 1 s at 100 MHz
    -- Clocks per logical array step. 'step' is issued once every G_STEP_DIV
    -- clocks, giving the ALU datapath G_STEP_DIV clock periods to settle -- the
    -- multicycle factor. MUST equal the -setup number in cgra_timing_constraints
    -- (scr/constraints.tcl). Raising it lets the clock run faster (the datapath
    -- gets more periods) at the cost of more clocks per step. One step is always
    -- one result, so emu.c stays golden regardless of the value.
    G_STEP_DIV       : positive := 2;
    G_ROWS           : positive := ROWS;
    G_COLS           : positive := COLS
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
    cfg      : out cfg_vec_t(0 to (G_ROWS * G_COLS) - 1);
    west_o   : out data_vec_t(0 to G_ROWS - 1);
    north_o  : out data_vec_t(0 to G_COLS - 1);
    step     : out std_logic;
    dp_rst   : out std_logic;
    pe_regs  : in  data_vec_t(0 to (G_ROWS * G_COLS) - 1);

    busy     : out std_logic
  );
end entity cgra_ctrl;

architecture rtl of cgra_ctrl is
  function max_size(a, b : positive) return positive is
  begin
    if a > b then return a; else return b; end if;
  end function;

  type state_t is (S_IDLE, S_ID, S_CFG, S_CFG_CK, S_WR, S_WR_CK,
                   S_RUN_ARG, S_RUN, S_RUN_ACK, S_RD, S_RD_CK,
                   S_EX_HDR, S_EX_CK, S_EX_RD, S_EX_CKTX, S_EX_ST,
                   S_SEND, S_SEND_WAIT);
  signal state     : state_t := S_IDLE;
  signal ret_state : state_t := S_IDLE;
  signal wr_ret    : state_t := S_WR_CK;    -- after the edge-input bytes
  signal run_ret   : state_t := S_RUN_ACK;  -- after the last logical step

  signal cfg_r   : cfg_vec_t(0 to (G_ROWS * G_COLS) - 1)  := (others => (others => '0'));
  signal west_r  : data_vec_t(0 to G_ROWS - 1)   := (others => (others => '0'));
  signal north_r : data_vec_t(0 to G_COLS - 1)   := (others => (others => '0'));

  signal shift    : std_logic_vector(23 downto 0) := (others => '0');
  signal byte_idx : natural range 0 to 3 := 0;
  signal word_idx : natural range 0 to max_size(G_ROWS * G_COLS, G_ROWS + G_COLS) - 1 := 0;
  signal run_cnt  : unsigned(7 downto 0) := (others => '0');
  -- Counts clocks within one logical step (0 .. G_STEP_DIV-1); 'step' fires at
  -- phase 0, the remaining phases let the datapath settle. See S_RUN and the
  -- matching multicycle-path constraint in scr/constraints.tcl.
  signal run_phase : natural range 0 to G_STEP_DIV - 1 := 0;
  -- Guard the first capture too: EXEC can reset PE outputs immediately before
  -- entering S_RUN. Subsequent captures are spaced by run_phase.
  signal run_wait : natural range 0 to G_STEP_DIV - 1 := 0;
  signal tx_byte  : std_logic_vector(7 downto 0) := (others => '0');
  signal wd       : natural range 0 to G_TIMEOUT_CYCLES := 0;
  signal cksum    : unsigned(7 downto 0) := (others => '0');
  signal id_idx   : natural range 0 to 4 := 0;

  -- Registered PE read-back mux output: pe_regs(word_idx) delayed by one clock.
  -- Every consumer (S_RD, S_EX_RD) reaches it through at least two states of
  -- the transmit handshake, so it is always settled by the time it is used.
  signal rd_word  : data_t := (others => '0');

  -- CMD_EXEC decode
  signal ex_steps : unsigned(7 downto 0) := (others => '0');
  signal ex_flags : std_logic_vector(7 downto 0) := (others => '0');
  signal ex_mask  : std_logic_vector(15 downto 0) := (others => '0');
  signal ex_ok    : std_logic := '0';         -- payload checksum verdict
  signal hdr_idx  : natural range 0 to 3 := 0;
  signal rd_settle : std_logic := '0';        -- skip cycle: let rd_word catch up

  -- ID reply payload
  type id_arr_t is array (0 to 4) of std_logic_vector(7 downto 0);
  constant ID_BYTES : id_arr_t := (
    ID_BYTE0,
    PROTO_VER,
    std_logic_vector(to_unsigned(G_ROWS, 8)),
    std_logic_vector(to_unsigned(G_COLS, 8)),
    std_logic_vector(to_unsigned(DATA_W, 8))
  );

begin
  assert G_ROWS * G_COLS <= 16
    report "protocol v3 supports at most 16 PEs" severity failure;
  assert DATA_W = 16 report "protocol v3 requires 16-bit data" severity failure;

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

      -- read-back mux, registered (see the note in the header comment)
      if word_idx < G_ROWS * G_COLS then
        rd_word <= pe_regs(word_idx);
      else
        rd_word <= (others => '0');
      end if;

      if rst = '1' then
        state    <= S_IDLE;
        cfg_r    <= (others => (others => '0'));
        west_r   <= (others => (others => '0'));
        north_r  <= (others => (others => '0'));
        run_cnt   <= (others => '0');
        run_phase <= 0;
        run_wait  <= 0;
        word_idx <= 0;
        byte_idx <= 0;
        hdr_idx  <= 0;
        wd       <= 0;
        cksum    <= (others => '0');
        ex_mask  <= (others => '0');
        ex_flags <= (others => '0');
        ex_ok    <= '0';
        rd_settle <= '0';
      else

        -- watchdog: only ticks while waiting for payload bytes
        case state is
          when S_CFG | S_CFG_CK | S_WR | S_WR_CK | S_RUN_ARG |
               S_EX_HDR | S_EX_CK =>
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
                  cksum  <= (others => '0');
                  wr_ret <= S_WR_CK;
                  state  <= S_WR;
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
                when CMD_EXEC =>
                  cksum   <= (others => '0');
                  hdr_idx <= 0;
                  state   <= S_EX_HDR;
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
                if word_idx = (G_ROWS * G_COLS) - 1 then
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

          -- Edge-input payload: shared by CMD_WR and CMD_EXEC (wr_ret selects
          -- which command's checksum state follows).
          when S_WR =>
            if rx_valid = '1' then
              cksum <= cksum + unsigned(rx_data);
              if byte_idx = 1 then
                if word_idx < G_ROWS then
                  west_r(word_idx) <=
                    signed(std_logic_vector'(rx_data & shift(23 downto 16)));
                else
                  north_r(word_idx - G_ROWS) <=
                    signed(std_logic_vector'(rx_data & shift(23 downto 16)));
                end if;
                byte_idx <= 0;
                if word_idx = G_ROWS + G_COLS - 1 then
                  word_idx <= 0;
                  state    <= wr_ret;
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
              run_cnt   <= unsigned(rx_data);
              run_phase <= 0;
              run_wait  <= G_STEP_DIV - 1;
              run_ret   <= S_RUN_ACK;
              state     <= S_RUN;
            end if;

          when S_RUN =>
            -- One logical array step every G_STEP_DIV clocks: pulse 'step' on
            -- phase 0, then G_STEP_DIV-1 settle cycles let the ALU datapath
            -- (operand mux -> DSP multiply/accumulate -> op mux) span
            -- G_STEP_DIV clock periods. The matching multicycle-path constraint
            -- (scr/constraints.tcl) tells the timing engine the same. One step
            -- still yields one result, so the fabric stays bit-identical to
            -- emu.c step_array() at any G_STEP_DIV.
            if run_wait > 0 then
              run_wait <= run_wait - 1;
            elsif run_cnt = 0 then
              state <= run_ret;
            elsif run_phase = 0 then
              step <= '1';                        -- launch the step
              if G_STEP_DIV = 1 then
                run_cnt <= run_cnt - 1;           -- single-cycle: consume now
              else
                run_phase <= 1;                   -- settle cycles follow
              end if;
            elsif run_phase = G_STEP_DIV - 1 then
              run_phase <= 0;
              run_cnt   <= run_cnt - 1;           -- last settle cycle
            else
              run_phase <= run_phase + 1;
            end if;

          when S_RUN_ACK =>
            tx_byte   <= RSP_ACK;
            ret_state <= S_IDLE;
            state     <= S_SEND;

          when S_RD =>
            if byte_idx = 0 then
              tx_byte   <= std_logic_vector(rd_word(7 downto 0));
              cksum     <= cksum + unsigned(rd_word(7 downto 0));
              byte_idx  <= 1;
              ret_state <= S_RD;
              state     <= S_SEND;
            else
              tx_byte  <= std_logic_vector(rd_word(15 downto 8));
              cksum    <= cksum + unsigned(rd_word(15 downto 8));
              byte_idx <= 0;
              if word_idx = (G_ROWS * G_COLS) - 1 then
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

          ------------------------------------------------------------------
          -- CMD_EXEC (protocol v3): steps, flags, tap mask, edge inputs.
          ------------------------------------------------------------------
          when S_EX_HDR =>
            if rx_valid = '1' then
              cksum <= cksum + unsigned(rx_data);
              case hdr_idx is
                when 0 => ex_steps <= unsigned(rx_data);  hdr_idx <= 1;
                when 1 => ex_flags <= rx_data;            hdr_idx <= 2;
                when 2 => ex_mask(7 downto 0) <= rx_data; hdr_idx <= 3;
                when others =>
                  ex_mask(15 downto 8) <= rx_data;
                  hdr_idx  <= 0;
                  word_idx <= 0;
                  byte_idx <= 0;
                  wr_ret   <= S_EX_CK;      -- reuse the CMD_WR payload path
                  state    <= S_WR;
              end case;
            end if;

          when S_EX_CK =>
            if rx_valid = '1' then
              -- Restart the checksum for the reply, and index from PE 0.
              cksum     <= (others => '0');
              word_idx  <= 0;
              byte_idx  <= 0;
              rd_settle <= '1';
              if unsigned(rx_data) = cksum then
                ex_ok <= '1';
                if ex_flags(EXEC_FLAG_RST) = '1' then
                  dp_rst <= '1';            -- first capture follows the guard interval
                end if;
                run_cnt   <= ex_steps;
                run_phase <= 0;
                run_wait  <= G_STEP_DIV - 1;
                run_ret   <= S_EX_RD;
                state     <= S_RUN;
              else
                -- Corrupted payload: do NOT advance the datapath. Still emit a
                -- full-length reply (the host sizes it from the mask it sent)
                -- carrying the current registers and a NACK status.
                ex_ok <= '0';
                state <= S_EX_RD;
              end if;
            end if;

          when S_EX_RD =>
            if rd_settle = '1' then
              rd_settle <= '0';             -- rd_word catches up with word_idx
            elsif ex_mask(word_idx) = '0' then
              if word_idx = (G_ROWS * G_COLS) - 1 then
                word_idx <= 0;
                state    <= S_EX_CKTX;
              else
                word_idx  <= word_idx + 1;
                rd_settle <= '1';
              end if;
            elsif byte_idx = 0 then
              tx_byte   <= std_logic_vector(rd_word(7 downto 0));
              cksum     <= cksum + unsigned(rd_word(7 downto 0));
              byte_idx  <= 1;
              ret_state <= S_EX_RD;
              state     <= S_SEND;
            else
              tx_byte  <= std_logic_vector(rd_word(15 downto 8));
              cksum    <= cksum + unsigned(rd_word(15 downto 8));
              byte_idx <= 0;
              if word_idx = (G_ROWS * G_COLS) - 1 then
                word_idx  <= 0;
                ret_state <= S_EX_CKTX;
              else
                word_idx  <= word_idx + 1;
                ret_state <= S_EX_RD;
              end if;
              state <= S_SEND;
            end if;

          when S_EX_CKTX =>
            tx_byte   <= std_logic_vector(cksum);
            ret_state <= S_EX_ST;
            state     <= S_SEND;

          when S_EX_ST =>
            if ex_ok = '1' then
              tx_byte <= RSP_ACK;
            else
              tx_byte <= RSP_NACK;
            end if;
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
