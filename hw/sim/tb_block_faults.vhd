-- Directed clocked faults absent from the clockless C model: reset during
-- upload/DOT and invalid concurrent requests during execution/readback.
library ieee;
use ieee.std_logic_1164.all;
use ieee.numeric_std.all;
use std.env.all;
use work.cgra_pkg.all;
use work.block_pkg.all;
entity tb_block_faults is
  generic (G_STEP_DIV : positive := 2);
end entity;
architecture sim of tb_block_faults is
  signal clk : std_logic := '0';
  signal rst, rx_valid, tx_busy, tx_start, selected, busy, owns, step, dp_rst : std_logic := '0';
  signal rx_data, tx_data : byte_t := (others => '0');
  signal a, b : data_t;
  signal cfg : cfg_vec_t(0 to 0);
  signal west, north, regs : data_vec_t(0 to 0);
  signal replies : bytes_t(0 to 63) := (others => (others => '0'));
  signal reply_count, steps : natural := 0;
  signal clear : boolean := false;
begin
  clk <= not clk after 5 ns;
  ctrl : entity work.block_ctrl generic map (G_TIMEOUT_CYCLES => 512, G_STEP_DIV => G_STEP_DIV)
    port map (clk, rst, '1', rx_valid, rx_data, tx_busy, tx_start, tx_data,
              selected, busy, regs(0), owns, step, dp_rst, a, b);
  cfg(0) <= cfg_word(OP_MAC, SEL_W, SEL_N, 0); west(0) <= a; north(0) <= b;
  mesh : entity work.cgra_array generic map (G_ROWS => 1, G_COLS => 1)
    port map (clk, rst or dp_rst, step, cfg, west, north, regs);
  transmitter : process(clk)
    variable remaining : natural := 0;
  begin
    if rising_edge(clk) then
      if clear then reply_count <= 0; steps <= 0; end if;
      if rst = '1' then tx_busy <= '0'; remaining := 0;
      else
        if step = '1' then steps <= steps+1; end if;
        if remaining > 0 then
          remaining := remaining-1;
          if remaining = 0 then tx_busy <= '0'; end if;
        end if;
        if tx_start = '1' then
          assert tx_busy = '0' and reply_count < replies'length severity failure;
          replies(reply_count) <= tx_data; reply_count <= reply_count+1;
          remaining := 32; tx_busy <= '1';
        end if;
      end if;
    end if;
  end process;
  stimulus : process
    variable payload : bytes_t(0 to BLOCK_PAYLOAD-1) := (others => (others => '0'));
    variable decoded : bytes_t(0 to 39);
    variable session : natural := 16#11223344#;
    procedure tick is begin wait until falling_edge(clk); end procedure;
    procedure clear_counts is begin clear <= true; tick; clear <= false; tick; end procedure;
    procedure send_byte(value : byte_t) is
    begin rx_valid <= '1'; rx_data <= value; tick; rx_valid <= '0'; tick; end procedure;
    procedure escaped(value : byte_t) is
    begin
      if value = x"7E" or value = x"7D" then send_byte(x"7D"); send_byte(value xor x"20");
      else send_byte(value); end if;
    end procedure;
    procedure frame(opcode, seq, n, nonce : natural) is
      variable raw : bytes_t(0 to BLOCK_RAW-1) := (others => (others => '0'));
      variable crc : unsigned(15 downto 0) := x"FFFF";
      variable word : unsigned(31 downto 0);
    begin
      raw(0) := x"01"; raw(1) := std_logic_vector(to_unsigned(opcode, 8));
      word := to_unsigned(nonce, 32);
      for i in 0 to 3 loop raw(2+i) := std_logic_vector(word(8*i+7 downto 8*i)); end loop;
      raw(6) := std_logic_vector(to_unsigned(seq mod 256, 8));
      raw(7) := std_logic_vector(to_unsigned(seq/256, 8));
      raw(8) := std_logic_vector(to_unsigned(n, 8));
      if n > 0 then raw(10 to 9+n) := payload(0 to n-1); end if;
      send_byte(x"7E");
      for i in 0 to 9+n loop crc := crc_byte(crc, raw(i)); escaped(raw(i)); end loop;
      escaped(std_logic_vector(crc(7 downto 0))); escaped(std_logic_vector(crc(15 downto 8)));
      send_byte(x"7E");
    end procedure;
    procedure await_reply is
      variable finished : boolean := false;
    begin
      for i in 1 to 12000 loop
        tick;
        if reply_count > 0 and busy = '0' and tx_busy = '0' then finished := true; exit; end if;
      end loop;
      assert finished report "directed reply timeout" severity failure;
      for i in 1 to 5 loop tick; end loop;
    end procedure;
    procedure check_reply(opcode, seq, status, n : natural) is
      variable escaped_byte : boolean := false;
      variable size : natural := 0;
      variable crc : unsigned(15 downto 0) := x"FFFF";
      variable nonce : unsigned(31 downto 0);
    begin
      await_reply;
      assert replies(0) = x"7E" and replies(reply_count-1) = x"7E" severity failure;
      for i in 1 to reply_count-2 loop
        if escaped_byte then decoded(size) := replies(i) xor x"20"; size := size+1; escaped_byte := false;
        elsif replies(i) = x"7D" then escaped_byte := true;
        else decoded(size) := replies(i); size := size+1; end if;
      end loop;
      assert not escaped_byte and size = 12+n report "bad reply length" severity failure;
      nonce := unsigned(std_logic_vector'(decoded(5) & decoded(4) & decoded(3) & decoded(2)));
      assert decoded(0) = x"01" and to_integer(unsigned(decoded(1))) = opcode+128 and
        nonce = to_unsigned(session, 32) and le16(decoded(6), decoded(7)) = seq and
        le16(decoded(8), decoded(9)) = n and to_integer(unsigned(decoded(10))) = status
        report "bad reply identity/status" severity failure;
      for i in 0 to size-3 loop crc := crc_byte(crc, decoded(i)); end loop;
      assert le16(decoded(size-2), decoded(size-1)) = to_integer(crc) severity failure;
    end procedure;
    procedure external_reset is
    begin
      rx_valid <= '0'; rst <= '1'; for i in 1 to 3 loop tick; end loop;
      assert owns = '0' and step = '0' and selected = '0' and busy = '0'
        report "reset failed to release block ownership/session" severity failure;
      rst <= '0'; clear_counts;
      for i in 1 to 30 loop tick; end loop;
      assert steps = 0 and reply_count = 0 report "activity after reset" severity failure;
    end procedure;
    procedure preflight is
    begin
      clear_counts; send_byte(x"08"); await_reply;
      assert reply_count = 5 and replies(0 to 4) = bytes_t'(x"CB", x"01", x"40", x"00", x"04") severity failure;
    end procedure;
    procedure hello is
    begin clear_counts; frame(0, 0, 0, session); check_reply(0, 0, 0, 6); end procedure;
    procedure upload(seq, bank, value : natural) is
    begin
      payload := (others => (others => '0'));
      payload(0) := std_logic_vector(to_unsigned(bank, 8)); payload(3) := x"40";
      for i in 0 to 63 loop payload(4+2*i) := std_logic_vector(to_unsigned(value, 8)); end loop;
      clear_counts; frame(1, seq, 132, session);
    end procedure;
    procedure dot(seq, count : natural) is
    begin
      payload(0) := std_logic_vector(to_unsigned(count, 8)); payload(1) := x"00";
      clear_counts; frame(2, seq, 2, session);
    end procedure;
    procedure wait_steps(n : natural) is
      variable reached : boolean := false;
    begin
      for i in 1 to 4000 loop tick; if steps >= n then reached := true; exit; end if; end loop;
      assert reached and owns = '1' report "DOT not running at fault point" severity failure;
    end procedure;
    procedure result(seq : natural) is
    begin
      clear_counts; frame(3, seq, 0, session); check_reply(3, seq, 0, 7);
      assert le16(decoded(11), decoded(12)) = 192 and decoded(13) = x"01" and
        decoded(14 to 16) = bytes_t'(x"00", x"00", x"00")
        report "result or execution count corrupted by concurrency/reset" severity failure;
      assert steps = 0 severity failure;
    end procedure;
  begin
    external_reset; preflight; hello;
    -- A 64-word upload takes 64 internal commit clocks after validation.
    -- Reset at clock 28 after the closing delimiter interrupts that commit.
    upload(1, 0, 1); for i in 1 to 28 loop tick; end loop;
    assert busy = '1' and reply_count = 0 and steps = 0 severity failure;
    external_reset; preflight; session := session+1; hello;
    dot(1, 1); check_reply(2, 1, 5, 1); assert steps = 0 severity failure;
    upload(2, 0, 1); check_reply(1, 2, 0, 1);
    upload(3, 1, 3); check_reply(1, 3, 0, 1);
    dot(4, 64); wait_steps(8);
    -- Deliberately violate single-outstanding-request: an otherwise valid
    -- HELLO must be ignored throughout execution, preserving the old session.
    frame(0, 0, 0, session+100); check_reply(2, 4, 0, 1);
    assert steps = 64 report "concurrent request duplicated/interrupted DOT" severity failure;
    result(5);
    dot(6, 64); wait_steps(8); external_reset; preflight;
    clear_counts; frame(3, 7, 0, session); check_reply(3, 7, 1, 1);
    session := session+1; hello;
    dot(1, 1); check_reply(2, 1, 5, 1); assert steps = 0 severity failure;
    upload(2, 0, 1); check_reply(1, 2, 0, 1);
    upload(3, 1, 3); check_reply(1, 3, 0, 1);
    dot(4, 64); check_reply(2, 4, 0, 1); assert steps = 64 severity failure;
    clear_counts; frame(3, 5, 0, session);
    for i in 1 to 1000 loop tick; exit when reply_count > 0; end loop;
    assert reply_count = 1 and tx_busy = '1' severity failure;
    frame(0, 0, 0, session+100); check_reply(3, 5, 0, 7);
    assert le16(decoded(11), decoded(12)) = 192 and steps = 0 severity failure;
    result(6);
    report "tb_block_faults PASS: upload/DOT reset, concurrent execution/readback; DIV=" & positive'image(G_STEP_DIV) severity note;
    stop; wait;
  end process;
end architecture;
