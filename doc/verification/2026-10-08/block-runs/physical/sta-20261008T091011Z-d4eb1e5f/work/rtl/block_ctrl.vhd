-- Experimental controller: preflight 08 latches framed mode until external
-- reset. Requests are staged/CRC-validated before memory or datapath effects.
library ieee;
use ieee.std_logic_1164.all;
use ieee.numeric_std.all;
use work.cgra_pkg.all;
use work.block_pkg.all;
entity block_ctrl is
  generic (G_TIMEOUT_CYCLES : positive := 100_000_000; G_STEP_DIV : positive := 2);
  port (clk, rst : in std_logic;
        legacy_idle : in std_logic;
        rx_valid : in std_logic; rx_data : in byte_t;
        tx_busy : in std_logic; tx_start : out std_logic; tx_data : out byte_t;
        selected, busy : out std_logic;
        pe_result : in data_t;
        owns_array, step, dp_rst : out std_logic; a, b : out data_t);
end entity;
architecture rtl of block_ctrl is
  type state_t is (S_IDLE, S_CHECK, S_COMPARE, S_OP, S_UPLOAD,
                   S_DOT_START, S_DOT_WAIT, S_FINISH, S_CRC_INIT, S_CRC,
                   S_ENCODE, S_CLOSE, S_SEND, S_SEND_WAIT);
  signal state : state_t := S_IDLE;
  signal framed : std_logic := '0';
  signal q : bytes_t(0 to BLOCK_RAW-1);
  signal q_valid, rx_ready, receiving : std_logic;
  signal q_length : natural range 0 to BLOCK_RAW;
  signal request_length : natural range 0 to BLOCK_RAW := 0;
  signal cached_q : bytes_t(0 to BLOCK_RAW-1) := (others => (others => '0'));
  signal cached_length : natural range 0 to BLOCK_RAW := 0;
  signal response, cached_response : bytes_t(0 to 18) := (others => (others => '0'));
  signal body_length, cached_body : natural range 11 to 17 := 11;
  signal cache_response, active, result_valid : boolean := false;
  signal session : unsigned(31 downto 0) := (others => '0');
  signal executions : unsigned(31 downto 0) := (others => '0');
  signal saved_result : data_t := (others => '0');
  type lengths_t is array (0 to 1) of natural range 0 to BLOCK_WORDS;
  signal loaded : lengths_t := (others => 0);
  signal scan : natural range 0 to BLOCK_RAW-1 := 0;
  signal crc : unsigned(15 downto 0) := x"FFFF";
  signal wire : bytes_t(0 to 39) := (others => (others => '0'));
  signal wire_length : natural range 0 to 40 := 0;
  signal wire_index : natural range 0 to 39 := 0;
  signal upload_bank : natural range 0 to 1 := 0;
  signal upload_offset : natural range 0 to BLOCK_WORDS-1 := 0;
  signal upload_count : natural range 1 to BLOCK_CHUNK := 1;
  signal upload_index : natural range 0 to BLOCK_CHUNK-1 := 0;
  signal wr_en, wr_bank, dot_start, dot_busy, dot_done : std_logic;
  signal wr_addr : natural range 0 to BLOCK_WORDS-1;
  signal wr_data, dot_result : data_t;
  signal dot_count : natural range 0 to BLOCK_WORDS := 0;
  function nonce(x : bytes_t) return unsigned is
  begin return unsigned(std_logic_vector'(x(5) & x(4) & x(3) & x(2))); end function;
begin
  -- selected is combinational for the preflight byte itself, so the legacy
  -- parser cannot also consume 08. While framed, even invalid bytes stay here.
  selected <= '1' when framed = '1' or
    (legacy_idle = '1' and rx_valid = '1' and rx_data = x"08") else '0';
  busy <= '1' when state /= S_IDLE or receiving = '1' else '0';
  rx_ready <= '1' when framed = '1' and state = S_IDLE and q_valid = '0' else '0';
  receiver : entity work.block_rx
    generic map (G_TIMEOUT_CYCLES => G_TIMEOUT_CYCLES)
    port map (clk, rst, rx_ready, rx_valid, rx_data, q, q_length, q_valid, receiving);
  wr_en <= '1' when state = S_UPLOAD else '0';
  wr_bank <= '1' when upload_bank = 1 else '0';
  wr_addr <= upload_offset + upload_index;
  wr_data <= signed(std_logic_vector'(q(15+2*upload_index) & q(14+2*upload_index)));
  dot_start <= '1' when state = S_DOT_START else '0';
  owns_array <= dot_busy;
  engine : entity work.block_dot
    generic map (G_STEP_DIV => G_STEP_DIV)
    port map (clk, rst, wr_en, wr_bank, wr_addr, wr_data,
              dot_start, dot_count, pe_result, dot_busy, dot_done,
              step, dp_rst, a, b, dot_result);
  process(clk)
    variable plen, seq, bank, offset, count : natural;
    variable n : unsigned(31 downto 0);
    variable byte : byte_t;
  begin
    if rising_edge(clk) then
      tx_start <= '0';
      if rst = '1' then
        state <= S_IDLE; framed <= '0'; active <= false; result_valid <= false;
        loaded <= (others => 0); executions <= (others => '0');
        session <= (others => '0'); cached_length <= 0; cache_response <= false;
        upload_offset <= 0; upload_index <= 0;
        tx_data <= (others => '0');
      else
        case state is
          when S_IDLE =>
            if framed = '0' and legacy_idle = '1' and rx_valid = '1' and rx_data = x"08" then
              framed <= '1'; wire(0 to 4) <= (x"CB", x"01", x"40", x"00", x"04");
              wire_length <= 5; wire_index <= 0; state <= S_SEND;
            elsif q_valid = '1' then
              request_length <= q_length; state <= S_CHECK;
            end if;
          when S_CHECK =>
            response(0 to 9) <= q(0 to 9); response(1) <= q(1) or x"80";
            response(8) <= x"01"; response(9) <= x"00"; response(10) <= x"00";
            body_length <= 11; cache_response <= false; state <= S_FINISH;
            plen := request_length - 12; seq := le16(q(6), q(7)); n := nonce(q);
            if active and n = session and seq = le16(cached_q(6), cached_q(7)) then
              if request_length /= cached_length then response(10) <= x"03";
              else scan <= 0; state <= S_COMPARE; end if;
            elsif q(1) = x"00" then
              if plen /= 0 or seq /= 0 or n = 0 then response(10) <= x"05";
              elsif active and n = session then response(10) <= x"01";
              else
                active <= true; session <= n; loaded <= (others => 0);
                result_valid <= false; executions <= (others => '0'); cache_response <= true;
                response(8) <= x"06"; body_length <= 16;
                response(11 to 15) <= (x"00", x"04", x"40", x"01", x"00");
              end if;
            elsif not active or n /= session then response(10) <= x"01";
            elsif le16(cached_q(6), cached_q(7)) = 65535 or
                  seq /= le16(cached_q(6), cached_q(7)) + 1 then response(10) <= x"02";
            else cache_response <= true; state <= S_OP;
            end if;
          when S_COMPARE =>
            if q(scan) /= cached_q(scan) then response(10) <= x"03"; state <= S_FINISH;
            elsif scan = request_length-1 then
              response <= cached_response; body_length <= cached_body; state <= S_CRC_INIT;
            else scan <= scan + 1; end if;
          when S_OP =>
            plen := request_length - 12; state <= S_FINISH;
            case q(1) is
              when x"01" =>
                bank := to_integer(unsigned(q(10))); offset := le16(q(11), q(12));
                count := to_integer(unsigned(q(13)));
                if plen < 4 or bank > 1 or count = 0 or count > BLOCK_CHUNK or
                  plen /= 4+2*count or offset >= BLOCK_WORDS then response(10) <= x"05";
                elsif count > BLOCK_WORDS-offset then response(10) <= x"05";
                elsif offset /= 0 and offset /= loaded(bank) then response(10) <= x"05";
                else
                  upload_bank <= bank; upload_offset <= offset; upload_count <= count;
                  upload_index <= 0; state <= S_UPLOAD;
                end if;
              when x"02" =>
                count := le16(q(10), q(11));
                if plen /= 2 or count > loaded(0) or count > loaded(1) then response(10) <= x"05";
                else dot_count <= count; state <= S_DOT_START; end if;
              when x"03" =>
                if plen /= 0 then response(10) <= x"05";
                elsif not result_valid then response(10) <= x"06";
                else
                  response(8) <= x"07"; body_length <= 17;
                  response(11) <= std_logic_vector(saved_result(7 downto 0));
                  response(12) <= std_logic_vector(saved_result(15 downto 8));
                  for i in 0 to 3 loop response(13+i) <= std_logic_vector(executions(8*i+7 downto 8*i)); end loop;
                end if;
              when others => response(10) <= x"04";
            end case;
          when S_UPLOAD =>
            if upload_index = upload_count-1 then
              loaded(upload_bank) <= upload_offset + upload_count;
              result_valid <= false; state <= S_FINISH;
            else upload_index <= upload_index + 1; end if;
          when S_DOT_START => state <= S_DOT_WAIT;
          when S_DOT_WAIT =>
            if dot_done = '1' then
              saved_result <= dot_result; executions <= executions+1;
              result_valid <= true; state <= S_FINISH;
            end if;
          when S_FINISH =>
            if cache_response then
              cached_q <= q; cached_length <= request_length;
              cached_response <= response; cached_body <= body_length;
            end if;
            state <= S_CRC_INIT;
          when S_CRC_INIT => crc <= x"FFFF"; scan <= 0; state <= S_CRC;
          when S_CRC =>
            if scan < body_length then crc <= crc_byte(crc, response(scan)); scan <= scan+1;
            else
              response(body_length) <= std_logic_vector(crc(7 downto 0));
              response(body_length+1) <= std_logic_vector(crc(15 downto 8));
              scan <= 0; wire(0) <= x"7E"; wire_length <= 1; state <= S_ENCODE;
            end if;
          when S_ENCODE =>
            byte := response(scan);
            if byte = x"7E" or byte = x"7D" then
              wire(wire_length) <= x"7D"; wire(wire_length+1) <= byte xor x"20";
              wire_length <= wire_length+2;
            else wire(wire_length) <= byte; wire_length <= wire_length+1; end if;
            if scan = body_length+1 then state <= S_CLOSE;
            else scan <= scan+1; end if;
          when S_CLOSE =>
            wire(wire_length) <= x"7E"; wire_length <= wire_length+1;
            wire_index <= 0; state <= S_SEND;
          when S_SEND =>
            if tx_busy = '0' then
              tx_data <= wire(wire_index); tx_start <= '1'; state <= S_SEND_WAIT;
            end if;
          when S_SEND_WAIT =>
            if tx_busy = '1' then
              if wire_index = wire_length-1 then state <= S_IDLE;
              else wire_index <= wire_index+1; state <= S_SEND; end if;
            end if;
        end case;
      end if;
    end if;
  end process;
end architecture;
