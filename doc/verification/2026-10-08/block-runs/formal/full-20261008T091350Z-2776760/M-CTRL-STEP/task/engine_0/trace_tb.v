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
    // UUT.$auto$ghdl.\cc:825:import_module$1532  = 4'b0001;
    // UUT.$auto$ghdl.\cc:825:import_module$1613  = 4'b0001;
    // UUT.$auto$ghdl.\cc:825:import_module$1659  = 4'b0001;
    // UUT.$auto$ghdl.\cc:825:import_module$1673  = 4'b0001;
    // UUT.$auto$ghdl.\cc:825:import_module$1694  = 4'b0001;
    // UUT.$auto$ghdl.\cc:825:import_module$1724  = 4'b0001;
    // UUT.$auto$ghdl.\cc:825:import_module$1734  = 4'b0001;
    // UUT.$auto$ghdl.\cc:825:import_module$335  = 3'b001;
    // UUT.$auto$ghdl.\cc:825:import_module$493  = 4'b0001;
    // UUT.$auto$ghdl.\cc:825:import_module$514  = 4'b0001;
    // UUT.$auto$ghdl.\cc:825:import_module$583  = 4'b0001;
    // UUT.$auto$ghdl.\cc:825:import_module$609  = 4'b0001;
    // UUT.$auto$ghdl.\cc:825:import_module$688  = 4'b0001;
    // UUT.$auto$ghdl.\cc:825:import_module$699  = 4'b0001;
    // UUT.$auto$ghdl.\cc:825:import_module$713  = 4'b0001;
    // UUT.$auto$ghdl.\cc:825:import_module$914  = 4'b0001;
    // UUT.$auto$ghdl.\cc:825:import_module$932  = 4'b0001;
    // UUT.$auto$ghdl.\cc:825:import_module$950  = 4'b0001;
    // UUT.$auto$ghdl.\cc:825:import_module$968  = 4'b0001;
    UUT._witness_.anyinit__1574 = 8'b00000000;
    UUT._witness_.anyinit__2070 = 8'b00000000;
    UUT._witness_.anyinit__2104 = 8'b00000000;
    UUT._witness_.anyinit__2139 = 8'b00000000;
    UUT._witness_.anyinit__2174 = 8'b00000000;
    UUT._witness_.anyinit__3314 = 2'b00;
    UUT._witness_.anyinit__3437 = 4'b0000;
    UUT._witness_.anyinit__3467 = 4'b0000;
    UUT._witness_.anyinit__3509 = 10'b0000000000;
    UUT._witness_.anyinit__3570 = 10'b0000000000;
    UUT._witness_.anyinit__3594 = 10'b0000000000;
    UUT.byte_idx = 2'b00;
    UUT.cfg_r = 32'b00000000000000000000000000000000;
    UUT.cksum = 8'b00000000;
    UUT.ctrl_contract.f_age = 2'b10;
    UUT.ctrl_contract.f_cfg = 32'b00000000000000000000000000000000;
    UUT.ctrl_contract.f_cfg_write = 1'b0;
    UUT.ctrl_contract.f_data_age = 2'b10;
    UUT.ctrl_contract.f_exec = 1'b0;
    UUT.ctrl_contract.f_expected = 8'b00000000;
    UUT.ctrl_contract.f_input_write = 1'b0;
    UUT.ctrl_contract.f_mask = 16'b0000000000000000;
    UUT.ctrl_contract.f_north = 16'b0000000000000000;
    UUT.ctrl_contract.f_ok = 1'b0;
    UUT.ctrl_contract.f_old_index = 1'b0;
    UUT.ctrl_contract.f_old_pe = 16'b0000000000000000;
    UUT.ctrl_contract.f_pending = 8'b00000000;
    UUT.ctrl_contract.f_queued = 1'b0;
    UUT.ctrl_contract.f_receiving = 1'b0;
    UUT.ctrl_contract.f_request_sum = 8'b00000000;
    UUT.ctrl_contract.f_seen = 1'b0;
    UUT.ctrl_contract.f_sent = 3'b000;
    UUT.ctrl_contract.f_sum = 8'b00000000;
    UUT.ctrl_contract.f_west = 16'b0000000000000000;
    UUT.dp_rst = 1'b1;
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
    UUT.run_phase = 1'b0;
    UUT.run_ret = 5'b01000;
    UUT.run_wait = 1'b0;
    UUT.shift = 24'b000000000000000000000000;
    UUT.state = 5'b00000;
    UUT.step = 1'b0;
    UUT.tx_byte = 8'b00000000;
    UUT.tx_start = 1'b0;
    UUT.wd = 2'b00;
    UUT.west_r = 16'b0000000000000000;
    UUT.word_idx = 1'b0;
    UUT.wr_ret = 5'b00101;
    UUT.\:3698 [3'b000] = 8'b11001010;

    // state 0
    PI_rst = 1'b0;
    PI_rx_data = 8'b00000100;
    PI_rx_valid = 1'b1;
    PI_pe_regs = 16'b0000000001110111;
    PI_tx_busy = 1'b0;
  end
  always @(posedge clock) begin
    // state 1
    if (cycle == 0) begin
      PI_rst <= 1'b0;
      PI_rx_data <= 8'b00111111;
      PI_rx_valid <= 1'b1;
      PI_pe_regs <= 16'b0000000000000000;
      PI_tx_busy <= 1'b0;
    end

    // state 2
    if (cycle == 1) begin
      PI_rst <= 1'b0;
      PI_rx_data <= 8'b00111111;
      PI_rx_valid <= 1'b1;
      PI_pe_regs <= 16'b0000000000000000;
      PI_tx_busy <= 1'b0;
    end

    // state 3
    if (cycle == 2) begin
      PI_rst <= 1'b0;
      PI_rx_data <= 8'b00000011;
      PI_rx_valid <= 1'b1;
      PI_pe_regs <= 16'b1111111111111111;
      PI_tx_busy <= 1'b0;
    end

    // state 4
    if (cycle == 3) begin
      PI_rst <= 1'b0;
      PI_rx_data <= 8'b00000000;
      PI_rx_valid <= 1'b0;
      PI_pe_regs <= 16'b0000000000000000;
      PI_tx_busy <= 1'b0;
    end

    genclock <= cycle < 4;
    cycle <= cycle + 1;
  end
endmodule
