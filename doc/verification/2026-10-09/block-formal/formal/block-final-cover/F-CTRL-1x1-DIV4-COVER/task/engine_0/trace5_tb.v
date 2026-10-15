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
    // UUT.$auto$ghdl.\cc:825:import_module$747  = 3'b001;
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
    UUT.run_phase = 2'b00;
    UUT.run_ret = 5'b01000;
    UUT.run_wait = 2'b00;
    UUT.shift = 24'b000000000000000000000000;
    UUT.state = 5'b00000;
    UUT.step = 1'b0;
    UUT.tx_byte = 8'b00000000;
    UUT.tx_start = 1'b0;
    UUT.wd = 2'b00;
    UUT.west_r = 16'b0000000000000000;
    UUT.word_idx = 1'b0;
    UUT.wr_ret = 5'b00101;
    UUT.\:3710 [3'b000] = 8'b11001010;

    // state 0
    PI_rst = 1'b0;
    PI_rx_data = 8'b00000010;
    PI_rx_valid = 1'b1;
    PI_pe_regs = 16'b0000000000000000;
    PI_tx_busy = 1'b0;
  end
  always @(posedge clock) begin
    // state 1
    if (cycle == 0) begin
      PI_rst <= 1'b0;
      PI_rx_data <= 8'b00000000;
      PI_rx_valid <= 1'b1;
      PI_pe_regs <= 16'b0000000000000000;
      PI_tx_busy <= 1'b0;
    end

    // state 2
    if (cycle == 1) begin
      PI_rst <= 1'b0;
      PI_rx_data <= 8'b00000000;
      PI_rx_valid <= 1'b1;
      PI_pe_regs <= 16'b0000000000000000;
      PI_tx_busy <= 1'b0;
    end

    // state 3
    if (cycle == 2) begin
      PI_rst <= 1'b0;
      PI_rx_data <= 8'b00100100;
      PI_rx_valid <= 1'b1;
      PI_pe_regs <= 16'b0000000000000000;
      PI_tx_busy <= 1'b0;
    end

    // state 4
    if (cycle == 3) begin
      PI_rst <= 1'b0;
      PI_rx_data <= 8'b00000000;
      PI_rx_valid <= 1'b1;
      PI_pe_regs <= 16'b0000000000000000;
      PI_tx_busy <= 1'b0;
    end

    // state 5
    if (cycle == 4) begin
      PI_rst <= 1'b0;
      PI_rx_data <= 8'b00100100;
      PI_rx_valid <= 1'b1;
      PI_pe_regs <= 16'b0000000000000000;
      PI_tx_busy <= 1'b1;
    end

    // state 6
    if (cycle == 5) begin
      PI_rst <= 1'b0;
      PI_rx_data <= 8'b00000001;
      PI_rx_valid <= 1'b1;
      PI_pe_regs <= 16'b0000000000000000;
      PI_tx_busy <= 1'b0;
    end

    // state 7
    if (cycle == 6) begin
      PI_rst <= 1'b0;
      PI_rx_data <= 8'b10000000;
      PI_rx_valid <= 1'b1;
      PI_pe_regs <= 16'b0000000000000000;
      PI_tx_busy <= 1'b1;
    end

    // state 8
    if (cycle == 7) begin
      PI_rst <= 1'b0;
      PI_rx_data <= 8'b00000011;
      PI_rx_valid <= 1'b1;
      PI_pe_regs <= 16'b0000000000000000;
      PI_tx_busy <= 1'b1;
    end

    // state 9
    if (cycle == 8) begin
      PI_rst <= 1'b0;
      PI_rx_data <= 8'b00000000;
      PI_rx_valid <= 1'b1;
      PI_pe_regs <= 16'b0000000000000000;
      PI_tx_busy <= 1'b1;
    end

    // state 10
    if (cycle == 9) begin
      PI_rst <= 1'b0;
      PI_rx_data <= 8'b00100000;
      PI_rx_valid <= 1'b1;
      PI_pe_regs <= 16'b0000000000000000;
      PI_tx_busy <= 1'b1;
    end

    // state 11
    if (cycle == 10) begin
      PI_rst <= 1'b1;
      PI_rx_data <= 8'b00000000;
      PI_rx_valid <= 1'b1;
      PI_pe_regs <= 16'b0000000000000000;
      PI_tx_busy <= 1'b0;
    end

    // state 12
    if (cycle == 11) begin
      PI_rst <= 1'b0;
      PI_rx_data <= 8'b00000100;
      PI_rx_valid <= 1'b1;
      PI_pe_regs <= 16'b0000000000000000;
      PI_tx_busy <= 1'b0;
    end

    // state 13
    if (cycle == 12) begin
      PI_rst <= 1'b0;
      PI_rx_data <= 8'b10000000;
      PI_rx_valid <= 1'b1;
      PI_pe_regs <= 16'b0000000000000000;
      PI_tx_busy <= 1'b1;
    end

    // state 14
    if (cycle == 13) begin
      PI_rst <= 1'b0;
      PI_rx_data <= 8'b00000100;
      PI_rx_valid <= 1'b0;
      PI_pe_regs <= 16'b0000000000000000;
      PI_tx_busy <= 1'b1;
    end

    // state 15
    if (cycle == 14) begin
      PI_rst <= 1'b0;
      PI_rx_data <= 8'b00000111;
      PI_rx_valid <= 1'b0;
      PI_pe_regs <= 16'b0000000000000000;
      PI_tx_busy <= 1'b0;
    end

    // state 16
    if (cycle == 15) begin
      PI_rst <= 1'b0;
      PI_rx_data <= 8'b10000000;
      PI_rx_valid <= 1'b1;
      PI_pe_regs <= 16'b0000000000000000;
      PI_tx_busy <= 1'b0;
    end

    // state 17
    if (cycle == 16) begin
      PI_rst <= 1'b0;
      PI_rx_data <= 8'b00000000;
      PI_rx_valid <= 1'b0;
      PI_pe_regs <= 16'b0000000000000000;
      PI_tx_busy <= 1'b0;
    end

    // state 18
    if (cycle == 17) begin
      PI_rst <= 1'b0;
      PI_rx_data <= 8'b00000000;
      PI_rx_valid <= 1'b0;
      PI_pe_regs <= 16'b0000000000000000;
      PI_tx_busy <= 1'b0;
    end

    genclock <= cycle < 18;
    cycle <= cycle + 1;
  end
endmodule
