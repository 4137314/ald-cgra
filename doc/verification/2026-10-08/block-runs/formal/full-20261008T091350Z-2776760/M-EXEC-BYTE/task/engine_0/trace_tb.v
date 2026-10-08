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
    // UUT.$auto$ghdl.\cc:825:import_module$1537  = 4'b0001;
    // UUT.$auto$ghdl.\cc:825:import_module$1618  = 4'b0001;
    // UUT.$auto$ghdl.\cc:825:import_module$1664  = 4'b0001;
    // UUT.$auto$ghdl.\cc:825:import_module$1678  = 4'b0001;
    // UUT.$auto$ghdl.\cc:825:import_module$1699  = 4'b0001;
    // UUT.$auto$ghdl.\cc:825:import_module$1729  = 4'b0001;
    // UUT.$auto$ghdl.\cc:825:import_module$1739  = 4'b0001;
    // UUT.$auto$ghdl.\cc:825:import_module$340  = 3'b001;
    // UUT.$auto$ghdl.\cc:825:import_module$498  = 4'b0001;
    // UUT.$auto$ghdl.\cc:825:import_module$519  = 4'b0001;
    // UUT.$auto$ghdl.\cc:825:import_module$588  = 4'b0001;
    // UUT.$auto$ghdl.\cc:825:import_module$614  = 4'b0001;
    // UUT.$auto$ghdl.\cc:825:import_module$693  = 4'b0001;
    // UUT.$auto$ghdl.\cc:825:import_module$704  = 4'b0001;
    // UUT.$auto$ghdl.\cc:825:import_module$718  = 4'b0001;
    // UUT.$auto$ghdl.\cc:825:import_module$919  = 4'b0001;
    // UUT.$auto$ghdl.\cc:825:import_module$937  = 4'b0001;
    // UUT.$auto$ghdl.\cc:825:import_module$955  = 4'b0001;
    // UUT.$auto$ghdl.\cc:825:import_module$973  = 4'b0001;
    UUT._witness_.anyinit__1586 = 8'b00000000;
    UUT._witness_.anyinit__2082 = 8'b00000000;
    UUT._witness_.anyinit__2116 = 8'b00000000;
    UUT._witness_.anyinit__2151 = 8'b00000000;
    UUT._witness_.anyinit__2186 = 8'b00000000;
    UUT._witness_.anyinit__3326 = 2'b00;
    UUT._witness_.anyinit__3449 = 4'b0000;
    UUT._witness_.anyinit__3479 = 4'b0000;
    UUT._witness_.anyinit__3521 = 10'b0000000000;
    UUT._witness_.anyinit__3582 = 10'b0000000000;
    UUT._witness_.anyinit__3606 = 10'b0000000000;
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
    UUT.\:3710 [3'b000] = 8'b11001010;

    // state 0
    PI_rst = 1'b0;
    PI_rx_data = 8'b00000111;
    PI_rx_valid = 1'b1;
    PI_pe_regs = 16'b0000000001111001;
    PI_tx_busy = 1'b0;
  end
  always @(posedge clock) begin
    // state 1
    if (cycle == 0) begin
      PI_rst <= 1'b0;
      PI_rx_data <= 8'b00000111;
      PI_rx_valid <= 1'b1;
      PI_pe_regs <= 16'b1000101110011000;
      PI_tx_busy <= 1'b1;
    end

    // state 2
    if (cycle == 1) begin
      PI_rst <= 1'b0;
      PI_rx_data <= 8'b01010101;
      PI_rx_valid <= 1'b1;
      PI_pe_regs <= 16'b0000000000001001;
      PI_tx_busy <= 1'b0;
    end

    // state 3
    if (cycle == 2) begin
      PI_rst <= 1'b0;
      PI_rx_data <= 8'b00000111;
      PI_rx_valid <= 1'b1;
      PI_pe_regs <= 16'b0001110110011101;
      PI_tx_busy <= 1'b0;
    end

    // state 4
    if (cycle == 3) begin
      PI_rst <= 1'b0;
      PI_rx_data <= 8'b01100011;
      PI_rx_valid <= 1'b1;
      PI_pe_regs <= 16'b0000000000001100;
      PI_tx_busy <= 1'b0;
    end

    // state 5
    if (cycle == 4) begin
      PI_rst <= 1'b0;
      PI_rx_data <= 8'b11101010;
      PI_rx_valid <= 1'b1;
      PI_pe_regs <= 16'b1101111100000001;
      PI_tx_busy <= 1'b0;
    end

    // state 6
    if (cycle == 5) begin
      PI_rst <= 1'b0;
      PI_rx_data <= 8'b00000111;
      PI_rx_valid <= 1'b1;
      PI_pe_regs <= 16'b0000000001001001;
      PI_tx_busy <= 1'b0;
    end

    // state 7
    if (cycle == 6) begin
      PI_rst <= 1'b0;
      PI_rx_data <= 8'b00000100;
      PI_rx_valid <= 1'b1;
      PI_pe_regs <= 16'b0000000000000000;
      PI_tx_busy <= 1'b0;
    end

    // state 8
    if (cycle == 7) begin
      PI_rst <= 1'b0;
      PI_rx_data <= 8'b00000100;
      PI_rx_valid <= 1'b1;
      PI_pe_regs <= 16'b0000000000000000;
      PI_tx_busy <= 1'b0;
    end

    // state 9
    if (cycle == 8) begin
      PI_rst <= 1'b0;
      PI_rx_data <= 8'b00000100;
      PI_rx_valid <= 1'b1;
      PI_pe_regs <= 16'b0100000000000010;
      PI_tx_busy <= 1'b0;
    end

    // state 10
    if (cycle == 9) begin
      PI_rst <= 1'b0;
      PI_rx_data <= 8'b10000000;
      PI_rx_valid <= 1'b1;
      PI_pe_regs <= 16'b1110111111111111;
      PI_tx_busy <= 1'b0;
    end

    // state 11
    if (cycle == 10) begin
      PI_rst <= 1'b0;
      PI_rx_data <= 8'b00000011;
      PI_rx_valid <= 1'b0;
      PI_pe_regs <= 16'b0000000000000000;
      PI_tx_busy <= 1'b0;
    end

    // state 12
    if (cycle == 11) begin
      PI_rst <= 1'b0;
      PI_rx_data <= 8'b00000000;
      PI_rx_valid <= 1'b0;
      PI_pe_regs <= 16'b0000000000000000;
      PI_tx_busy <= 1'b0;
    end

    genclock <= cycle < 12;
    cycle <= cycle + 1;
  end
endmodule
