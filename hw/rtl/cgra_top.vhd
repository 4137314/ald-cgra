-- cgra_top.vhd
-- FPGA top level: UART <-> protocol controller <-> R x C CGRA (default 4x4).
--
-- LEDs: led(0) heartbeat, led(1) controller busy,
--       led(2) UART RX activity, led(3) UART TX activity.

library ieee;
use ieee.std_logic_1164.all;
use ieee.numeric_std.all;
use work.cgra_pkg.all;
use work.cgra_comp_pkg.all;

entity cgra_top is
  generic (
    G_CLK_FREQ_HZ    : natural := 100_000_000;
    G_BAUD           : natural := 115_200;
    G_TIMEOUT_CYCLES : natural := 100_000_000;
    -- Datapath multicycle factor: clocks per array step (see cgra_ctrl). Keep in
    -- sync with the -setup number in scr/constraints.tcl -- the flow passes both
    -- from one knob (make STEP_DIV=...).
    G_STEP_DIV       : positive := 2;
    G_ROWS           : positive := ROWS;
    G_COLS           : positive := COLS;
    G_BLOCK_ENABLE   : boolean := false
  );
  port (
    clk       : in  std_logic;
    btn_rst   : in  std_logic;                     -- active-high push button
    uart_rx_i : in  std_logic;                     -- from host
    uart_tx_o : out std_logic;                     -- to host
    led       : out std_logic_vector(3 downto 0)
  );
end entity cgra_top;

architecture rtl of cgra_top is

  constant CLKS_PER_BIT : natural := G_CLK_FREQ_HZ / G_BAUD;

  signal rst_s1, rst_s2 : std_logic := '1';
  signal rst            : std_logic;
  attribute ASYNC_REG : string;
  attribute ASYNC_REG of rst_s1, rst_s2 : signal is "TRUE";

  signal rx_data  : std_logic_vector(7 downto 0);
  signal rx_valid : std_logic;
  signal tx_data  : std_logic_vector(7 downto 0);
  signal tx_start : std_logic;
  signal tx_busy  : std_logic;

  signal cfg      : cfg_vec_t(0 to G_ROWS * G_COLS - 1);
  signal west_in  : data_vec_t(0 to G_ROWS - 1);
  signal north_in : data_vec_t(0 to G_COLS - 1);
  signal pe_regs  : data_vec_t(0 to G_ROWS * G_COLS - 1);
  signal step     : std_logic;
  signal dp_rst   : std_logic;
  signal arr_rst  : std_logic;
  signal ctrl_busy : std_logic;

  signal ctrl_rx_valid, ctrl_tx_start : std_logic;
  signal ctrl_tx_data : std_logic_vector(7 downto 0);
  signal array_cfg : cfg_vec_t(0 to G_ROWS * G_COLS - 1);
  signal array_west : data_vec_t(0 to G_ROWS - 1);
  signal array_north : data_vec_t(0 to G_COLS - 1);
  signal array_step, array_dp_rst, block_busy : std_logic;

  signal hb_cnt : unsigned(25 downto 0) := (others => '0');

begin

  -- reset synchroniser
  rst_p : process (clk)
  begin
    if rising_edge(clk) then
      rst_s1 <= btn_rst;
      rst_s2 <= rst_s1;
    end if;
  end process;
  rst <= rst_s2;

  arr_rst <= rst or array_dp_rst;

  u_rx : uart_rx
    generic map (CLKS_PER_BIT => CLKS_PER_BIT)
    port map (
      clk       => clk,
      rst       => rst,
      rx_serial => uart_rx_i,
      rx_valid  => rx_valid,
      rx_data   => rx_data
    );

  u_tx : uart_tx
    generic map (CLKS_PER_BIT => CLKS_PER_BIT)
    port map (
      clk       => clk,
      rst       => rst,
      tx_start  => tx_start,
      tx_data   => tx_data,
      tx_serial => uart_tx_o,
      tx_busy   => tx_busy
    );

  u_ctrl : cgra_ctrl
    generic map (G_TIMEOUT_CYCLES => G_TIMEOUT_CYCLES,
                 G_STEP_DIV       => G_STEP_DIV,
                 G_ROWS => G_ROWS, G_COLS => G_COLS)
    port map (
      clk      => clk,
      rst      => rst,
      rx_data  => rx_data,
      rx_valid => ctrl_rx_valid,
      tx_data  => ctrl_tx_data,
      tx_start => ctrl_tx_start,
      tx_busy  => tx_busy,
      cfg      => cfg,
      west_o   => west_in,
      north_o  => north_in,
      step     => step,
      dp_rst   => dp_rst,
      pe_regs  => pe_regs,
      busy     => ctrl_busy
    );

  legacy_path : if not G_BLOCK_ENABLE generate
    ctrl_rx_valid <= rx_valid;
    tx_data <= ctrl_tx_data; tx_start <= ctrl_tx_start;
    array_cfg <= cfg; array_west <= west_in; array_north <= north_in;
    array_step <= step; array_dp_rst <= dp_rst; block_busy <= '0';
  end generate;

  blocks : if G_BLOCK_ENABLE generate
    signal selected, block_tx_start, owns, block_step, block_rst : std_logic;
    signal block_tx_data : std_logic_vector(7 downto 0);
    signal a, b : data_t;
  begin
    u_block : entity work.block_ctrl
      generic map (G_TIMEOUT_CYCLES => G_TIMEOUT_CYCLES, G_STEP_DIV => G_STEP_DIV)
      port map (clk => clk, rst => rst, legacy_idle => not ctrl_busy,
                rx_valid => rx_valid, rx_data => rx_data, tx_busy => tx_busy,
                tx_start => block_tx_start, tx_data => block_tx_data,
                selected => selected, busy => block_busy, pe_result => pe_regs(0),
                owns_array => owns, step => block_step, dp_rst => block_rst, a => a, b => b);
    ctrl_rx_valid <= rx_valid and not selected;
    tx_start <= block_tx_start when selected = '1' else ctrl_tx_start;
    tx_data <= block_tx_data when selected = '1' else ctrl_tx_data;
    array_step <= block_step when owns = '1' else step;
    array_dp_rst <= block_rst when owns = '1' else dp_rst;
    process(all)
    begin
      array_cfg <= cfg; array_west <= west_in; array_north <= north_in;
      if owns = '1' then
        array_cfg <= (others => cfg_word(OP_NOP, SEL_ZERO, SEL_ZERO, 0));
        array_cfg(0) <= cfg_word(OP_MAC, SEL_W, SEL_N, 0);
        array_west <= (others => (others => '0')); array_west(0) <= a;
        array_north <= (others => (others => '0')); array_north(0) <= b;
      end if;
    end process;
  end generate;

  u_array : cgra_array
    generic map (G_ROWS => G_ROWS, G_COLS => G_COLS)
    port map (
      clk      => clk,
      rst      => arr_rst,
      step     => array_step,
      cfg      => array_cfg,
      west_in  => array_west,
      north_in => array_north,
      pe_out   => pe_regs
    );

  hb_p : process (clk)
  begin
    if rising_edge(clk) then
      hb_cnt <= hb_cnt + 1;
    end if;
  end process;

  led(0) <= std_logic(hb_cnt(hb_cnt'high));
  led(1) <= ctrl_busy or block_busy;
  led(2) <= not uart_rx_i;
  led(3) <= tx_busy;

end architecture rtl;
