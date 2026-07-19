-- cgra_pkg.vhd
-- Shared constants and types for the CGRA design.
--
-- Keep the protocol constants and the configuration word layout in sync
-- with lib/include/cgra.h (host-side library).
--
-- Configuration word layout (32 bit, one word per PE):
--   [15:0]  imm    : 16-bit signed immediate constant
--   [19:16] opcode : PE operation (see OP_* below)
--   [22:20] sel_a  : operand A mux select (see SEL_* below)
--   [25:23] sel_b  : operand B mux select
--   [31:26] reserved

library ieee;
use ieee.std_logic_1164.all;
use ieee.numeric_std.all;

package cgra_pkg is

  constant DATA_W : natural := 16;
  constant ROWS   : natural := 4;
  constant COLS   : natural := 4;
  constant NUM_PE : natural := ROWS * COLS;
  constant CFG_W  : natural := 32;

  subtype data_t is signed(DATA_W - 1 downto 0);
  subtype cfg_t  is std_logic_vector(CFG_W - 1 downto 0);

  type data_vec_t is array (natural range <>) of data_t;
  type cfg_vec_t  is array (natural range <>) of cfg_t;

  -- PE opcodes (cfg[19:16])
  constant OP_NOP   : std_logic_vector(3 downto 0) := x"0"; -- keep register
  constant OP_PASS  : std_logic_vector(3 downto 0) := x"1"; -- r <= a
  constant OP_ADD   : std_logic_vector(3 downto 0) := x"2"; -- r <= a + b
  constant OP_SUB   : std_logic_vector(3 downto 0) := x"3"; -- r <= a - b
  constant OP_MUL   : std_logic_vector(3 downto 0) := x"4"; -- r <= (a * b) low half
  constant OP_MAC   : std_logic_vector(3 downto 0) := x"5"; -- r <= r + a * b
  constant OP_AND   : std_logic_vector(3 downto 0) := x"6";
  constant OP_OR    : std_logic_vector(3 downto 0) := x"7";
  constant OP_XOR   : std_logic_vector(3 downto 0) := x"8";
  constant OP_SHL   : std_logic_vector(3 downto 0) := x"9"; -- r <= a << b[3:0]
  constant OP_SHR   : std_logic_vector(3 downto 0) := x"A"; -- r <= a >> b[3:0] (arithmetic)
  constant OP_MAX   : std_logic_vector(3 downto 0) := x"B"; -- signed max(a, b)
  constant OP_MIN   : std_logic_vector(3 downto 0) := x"C"; -- signed min(a, b)
  constant OP_ABS   : std_logic_vector(3 downto 0) := x"D"; -- r <= |a|
  constant OP_ACC   : std_logic_vector(3 downto 0) := x"E"; -- r <= r + a
  constant OP_CONST : std_logic_vector(3 downto 0) := x"F"; -- r <= imm

  -- Operand mux selects (cfg[22:20] = sel_a, cfg[25:23] = sel_b).
  -- N/S/E/W select the registered output of the neighbouring PE.
  -- On the array edges: N of row 0 is the north input port of that column,
  -- W of column 0 is the west input port of that row; S/E edges read zero.
  constant SEL_N     : std_logic_vector(2 downto 0) := "000";
  constant SEL_S     : std_logic_vector(2 downto 0) := "001";
  constant SEL_E     : std_logic_vector(2 downto 0) := "010";
  constant SEL_W     : std_logic_vector(2 downto 0) := "011";
  constant SEL_CONST : std_logic_vector(2 downto 0) := "100";
  constant SEL_SELF  : std_logic_vector(2 downto 0) := "101";
  constant SEL_ZERO  : std_logic_vector(2 downto 0) := "110";

  -- UART protocol commands (host -> FPGA, first byte of every transaction)
  constant CMD_ID  : std_logic_vector(7 downto 0) := x"01"; -- reply: ID_BYTE0, ID_BYTE1
  constant CMD_CFG : std_logic_vector(7 downto 0) := x"02"; -- + 16 x 32-bit LE words, reply ACK
  constant CMD_WR  : std_logic_vector(7 downto 0) := x"03"; -- + 8 x 16-bit LE (west 0..3, north 0..3), reply ACK
  constant CMD_RUN : std_logic_vector(7 downto 0) := x"04"; -- + 1 byte step count, reply ACK when done
  constant CMD_RD  : std_logic_vector(7 downto 0) := x"05"; -- reply: 16 x 16-bit LE PE registers (row-major)
  constant CMD_RST : std_logic_vector(7 downto 0) := x"06"; -- reset datapath registers (config kept), reply ACK

  constant RSP_ACK  : std_logic_vector(7 downto 0) := x"79";
  constant RSP_NACK : std_logic_vector(7 downto 0) := x"1F";
  constant ID_BYTE0 : std_logic_vector(7 downto 0) := x"CA"; -- device signature
  constant PROTO_VER : std_logic_vector(7 downto 0) := x"02"; -- protocol version

  -- Protocol v2 adds an 8-bit additive checksum (sum of payload bytes mod 256)
  -- after the CFG and WR payloads (device NACKs on mismatch) and after the RD
  -- reply. The ID reply is ID_BYTE0, PROTO_VER, ROWS, COLS, DATA_W.

  function cfg_word(op   : std_logic_vector(3 downto 0);
                    sa   : std_logic_vector(2 downto 0);
                    sb   : std_logic_vector(2 downto 0);
                    imm  : integer) return cfg_t;

end package cgra_pkg;

package body cgra_pkg is

  function cfg_word(op   : std_logic_vector(3 downto 0);
                    sa   : std_logic_vector(2 downto 0);
                    sb   : std_logic_vector(2 downto 0);
                    imm  : integer) return cfg_t is
    variable w : cfg_t := (others => '0');
  begin
    w(15 downto 0)  := std_logic_vector(to_signed(imm, 16));
    w(19 downto 16) := op;
    w(22 downto 20) := sa;
    w(25 downto 23) := sb;
    return w;
  end function;

end package body cgra_pkg;
