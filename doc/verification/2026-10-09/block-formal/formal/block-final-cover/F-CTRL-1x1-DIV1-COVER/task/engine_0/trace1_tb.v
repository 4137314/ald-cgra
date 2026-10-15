`ifndef VERILATOR
module testbench;
  reg [4095:0] vcdfile;
  reg clock;
`else
module testbench(input clock, output reg genclock);
  initial genclock = 1;
`endif
  reg genclock = 1;
  reg [31:0] cycle = 0;
  reg [0:0] PI_rst;
  reg [7:0] PI_rx_data;
  reg [0:0] PI_rx_valid;
  reg [15:0] PI_pe_regs;
  wire [0:0] PI_clk = clock;
  reg [0:0] PI_tx_busy;
  cgra_ctrl UUT (
    .rst(PI_rst),
    .rx_data(PI_rx_data),
    .rx_valid(PI_rx_valid),
    .pe_regs(PI_pe_regs),
    .clk(PI_clk),
    .tx_busy(PI_tx_busy)
  );
`ifndef VERILATOR
  initial begin
    if ($value$plusargs("vcd=%s", vcdfile)) begin
      $dumpfile(vcdfile);
      $dumpvars(0, testbench);
    end
    #5 clock = 0;
    while (genclock) begin
      #5 clock = 0;
      #5 clock = 1;
    end
  end
`endif
  initial begin
`ifndef VERILATOR
    #1;
`endif
    // UUT.$auto$ghdl.\cc:825:import_module$709  = 3'b001;
    UUT.byte_idx = 2'b00;
    UUT.cfg_r = 32'b00000000000000000000000000000000;
    UUT.cksum = 8'b00000000;
    UUT.ctrl_contract.f_cfg = 32'b00000000000000000000000000000000;
    UUT.ctrl_contract.f_did_capture = 1'b0;
    UUT.ctrl_contract.f_exec = 1'b0;
    UUT.ctrl_contract.f_mask = 16'b0000000000000000;
    UUT.ctrl_contract.f_modified_cfg = 1'b0;
    UUT.ctrl_contract.f_modified_inputs = 1'b0;
    UUT.ctrl_contract.f_north = 16'b0000000000000000;
    UUT.ctrl_contract.f_ok = 1'b0;
    UUT.ctrl_contract.f_pending = 8'b00000000;
    UUT.ctrl_contract.f_receiving = 1'b0;
    UUT.ctrl_contract.f_request_sum = 8'b00000000;
    UUT.ctrl_contract.f_west = 16'b0000000000000000;
    UUT.dp_rst = 1'b0;
    UUT.ex_flags = 8'b00000000;
    UUT.ex_mask = 16'b0000000000000000;
    UUT.ex_ok = 1'b0;
    UUT.ex_steps = 8'b00000000;
    UUT.hdr_idx = 2'b00;
    UUT.id_idx = 3'b000;
    UUT.north_r = 16'b0000000000000000;
    UUT.rd_settle = 1'b0;
    UUT.rd_word = 16'b0000000000000000;
    UUT.ret_state = 5'b00000;
    UUT.run_cnt = 8'b00000000;
    UUT.run_ret = 5'b01000;
    UUT.shift = 24'b000000000000000000000000;
    UUT.state = 5'b00000;
    UUT.step = 1'b0;
    UUT.tx_byte = 8'b00000000;
    UUT.tx_start = 1'b0;
    UUT.wd = 2'b00;
    UUT.west_r = 16'b0000000000000000;
    UUT.word_idx = 1'b0;
    UUT.wr_ret = 5'b00101;
    UUT.\:3633 [3'b000] = 8'b11001010;

    // state 0
    PI_rst = 1'b0;
    PI_rx_data = 8'b00000100;
    PI_rx_valid = 1'b1;
    PI_pe_regs = 16'b0000000000000000;
    PI_tx_busy = 1'b0;
  end
  always @(posedge clock) begin
    // state 1
    if (cycle == 0) begin
      PI_rst <= 1'b0;
      PI_rx_data <= 8'b11111111;
      PI_rx_valid <= 1'b1;
      PI_pe_regs <= 16'b0000000000000000;
      PI_tx_busy <= 1'b0;
    end

    genclock <= cycle < 1;
    cycle <= cycle + 1;
  end
endmodule
