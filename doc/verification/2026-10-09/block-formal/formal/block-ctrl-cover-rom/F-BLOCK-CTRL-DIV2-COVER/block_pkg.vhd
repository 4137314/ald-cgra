-- Experimental block framing revision 1; see doc/block-protocol.md.
library ieee;
use ieee.std_logic_1164.all;
use ieee.numeric_std.all;
package block_pkg is
  constant BLOCK_WORDS : positive := 1024;
  constant BLOCK_CHUNK : positive := 64;
  constant BLOCK_HEADER : positive := 10;
  constant BLOCK_PAYLOAD : positive := 132;
  constant BLOCK_RAW : positive := 144;
  subtype byte_t is std_logic_vector(7 downto 0);
  type bytes_t is array (natural range <>) of byte_t;
  function crc_byte(crc : unsigned(15 downto 0); b : byte_t) return unsigned;
  function le16(lo, hi : byte_t) return natural;
end package;
package body block_pkg is
  function crc_byte(crc : unsigned(15 downto 0); b : byte_t) return unsigned is
    variable c : unsigned(15 downto 0) := crc xor (unsigned(b) & x"00");
  begin
    for i in 0 to 7 loop
      if c(15) = '1' then c := shift_left(c, 1) xor x"1021";
      else c := shift_left(c, 1); end if;
    end loop;
    return c;
  end function;
  function le16(lo, hi : byte_t) return natural is
  begin return to_integer(unsigned(hi)) * 256 + to_integer(unsigned(lo)); end function;
end package body;
