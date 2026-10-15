-- Bounded frame staging. Only valid_o authorizes effects. The enclosing
-- dispatcher stays in block mode after timeout/corruption until external reset.
library ieee;
use ieee.std_logic_1164.all;
use ieee.numeric_std.all;
use work.block_pkg.all;
entity block_rx is
  generic (G_TIMEOUT_CYCLES : positive := 100_000_000);
  port (clk, rst, ready, rx_valid : in std_logic; rx_data : in byte_t;
        raw : out bytes_t(0 to BLOCK_RAW-1);
        length : out natural range 0 to BLOCK_RAW;
        valid_o : out std_logic; receiving : out std_logic);
end entity;
architecture rtl of block_rx is
  signal data : bytes_t(0 to BLOCK_RAW-1) := (others => (others => '0'));
  signal used : natural range 0 to BLOCK_RAW := 0;
  signal active, escaped, bad : boolean := false;
  signal timer : natural range 0 to G_TIMEOUT_CYCLES-1 := 0;
  signal crc : unsigned(15 downto 0) := x"FFFF";
begin
  raw <= data;
  receiving <= '1' when active else '0';
  process(clk)
    variable b : byte_t;
    variable append_byte : boolean;
    variable n : natural;
  begin
    if rising_edge(clk) then
      valid_o <= '0';
      if rst = '1' or ready = '0' then
        active <= false; escaped <= false; bad <= false; used <= 0; timer <= 0;
        length <= 0;
      elsif active and rx_valid = '0' and timer = G_TIMEOUT_CYCLES-1 then
        active <= false; escaped <= false; bad <= false; used <= 0; timer <= 0;
      elsif rx_valid = '1' then
        timer <= 0;
        if rx_data = x"7E" then
          if not active then
            active <= true; escaped <= false; bad <= false; used <= 0; crc <= x"FFFF";
          elsif used /= 0 or escaped or bad then
            if not escaped and not bad and used >= BLOCK_HEADER+2 then
              n := le16(data(8), data(9));
              if data(0) = x"01" and n <= BLOCK_PAYLOAD and used = BLOCK_HEADER+n+2 then
                if le16(data(used-2), data(used-1)) = to_integer(crc) then
                  valid_o <= '1'; length <= used;
                end if;
              end if;
            end if;
            active <= false; escaped <= false; bad <= false; used <= 0;
          end if;
        elsif active and not bad then
          b := rx_data; append_byte := true;
          if escaped then
            escaped <= false;
            if b = x"5E" or b = x"5D" then b := b xor x"20";
            else bad <= true; append_byte := false; end if;
          elsif b = x"7D" then
            escaped <= true; append_byte := false;
          end if;
          if append_byte then
            if used = BLOCK_RAW then bad <= true;
            else
              data(used) <= b; used <= used + 1;
              if used < BLOCK_HEADER then crc <= crc_byte(crc, b);
              elsif used < BLOCK_HEADER + le16(data(8), data(9)) then
                crc <= crc_byte(crc, b);
              end if;
            end if;
          end if;
        end if;
      elsif active then timer <= timer + 1;
      end if;
    end if;
  end process;
end architecture;
