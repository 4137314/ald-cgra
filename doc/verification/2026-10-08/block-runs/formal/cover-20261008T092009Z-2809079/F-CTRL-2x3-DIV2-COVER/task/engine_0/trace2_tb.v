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
  reg [95:0] PI_pe_regs;
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
    UUT.byte_idx = 2'b00;
    UUT.cfg_r = 192'b000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000;
    UUT.cksum = 8'b00000000;
    UUT.ctrl_contract.f_did_capture = 1'b0;
    UUT.ctrl_contract.f_exec = 1'b0;
    UUT.ctrl_contract.f_mask = 16'b0000000000000000;
    UUT.ctrl_contract.f_ok = 1'b0;
    UUT.ctrl_contract.f_receiving = 1'b0;
    UUT.ctrl_contract.f_request_sum = 8'b00000000;
    UUT.dp_rst = 1'b0;
    UUT.ex_flags = 8'b00000000;
    UUT.ex_mask = 16'b0000000000000000;
    UUT.ex_ok = 1'b0;
    UUT.ex_steps = 8'b00000000;
    UUT.hdr_idx = 2'b00;
    UUT.id_idx = 3'b000;
    UUT.north_r = 48'b000000000000000000000000000000000000000000000000;
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
    UUT.west_r = 32'b00000000000000000000000000000000;
    UUT.word_idx = 3'b000;
    UUT.wr_ret = 5'b00101;
    UUT.\:3867 [3'b000] = 8'b11001010;

    // state 0
    PI_rst = 1'b0;
    PI_rx_data = 8'b00000111;
    PI_rx_valid = 1'b1;
    PI_pe_regs = 96'b000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000;
    PI_tx_busy = 1'b0;
  end
  always @(posedge clock) begin
    // state 1
    if (cycle == 0) begin
      PI_rst <= 1'b0;
      PI_rx_data <= 8'b00000001;
      PI_rx_valid <= 1'b1;
      PI_pe_regs <= 96'b000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000;
      PI_tx_busy <= 1'b0;
    end

    // state 2
    if (cycle == 1) begin
      PI_rst <= 1'b0;
      PI_rx_data <= 8'b10000000;
      PI_rx_valid <= 1'b1;
      PI_pe_regs <= 96'b000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000;
      PI_tx_busy <= 1'b0;
    end

    // state 3
    if (cycle == 2) begin
      PI_rst <= 1'b0;
      PI_rx_data <= 8'b11111111;
      PI_rx_valid <= 1'b1;
      PI_pe_regs <= 96'b000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000;
      PI_tx_busy <= 1'b0;
    end

    // state 4
    if (cycle == 3) begin
      PI_rst <= 1'b0;
      PI_rx_data <= 8'b00000000;
      PI_rx_valid <= 1'b1;
      PI_pe_regs <= 96'b000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000;
      PI_tx_busy <= 1'b0;
    end

    // state 5
    if (cycle == 4) begin
      PI_rst <= 1'b0;
      PI_rx_data <= 8'b00000000;
      PI_rx_valid <= 1'b1;
      PI_pe_regs <= 96'b000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000;
      PI_tx_busy <= 1'b0;
    end

    // state 6
    if (cycle == 5) begin
      PI_rst <= 1'b0;
      PI_rx_data <= 8'b00000000;
      PI_rx_valid <= 1'b1;
      PI_pe_regs <= 96'b000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000;
      PI_tx_busy <= 1'b0;
    end

    // state 7
    if (cycle == 6) begin
      PI_rst <= 1'b0;
      PI_rx_data <= 8'b00000000;
      PI_rx_valid <= 1'b1;
      PI_pe_regs <= 96'b000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000;
      PI_tx_busy <= 1'b0;
    end

    // state 8
    if (cycle == 7) begin
      PI_rst <= 1'b0;
      PI_rx_data <= 8'b00000000;
      PI_rx_valid <= 1'b1;
      PI_pe_regs <= 96'b000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000;
      PI_tx_busy <= 1'b0;
    end

    // state 9
    if (cycle == 8) begin
      PI_rst <= 1'b0;
      PI_rx_data <= 8'b00000000;
      PI_rx_valid <= 1'b1;
      PI_pe_regs <= 96'b000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000;
      PI_tx_busy <= 1'b0;
    end

    // state 10
    if (cycle == 9) begin
      PI_rst <= 1'b0;
      PI_rx_data <= 8'b00000000;
      PI_rx_valid <= 1'b1;
      PI_pe_regs <= 96'b000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000;
      PI_tx_busy <= 1'b0;
    end

    // state 11
    if (cycle == 10) begin
      PI_rst <= 1'b0;
      PI_rx_data <= 8'b00000000;
      PI_rx_valid <= 1'b1;
      PI_pe_regs <= 96'b000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000;
      PI_tx_busy <= 1'b0;
    end

    // state 12
    if (cycle == 11) begin
      PI_rst <= 1'b0;
      PI_rx_data <= 8'b00000000;
      PI_rx_valid <= 1'b1;
      PI_pe_regs <= 96'b000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000;
      PI_tx_busy <= 1'b0;
    end

    // state 13
    if (cycle == 12) begin
      PI_rst <= 1'b0;
      PI_rx_data <= 8'b01000000;
      PI_rx_valid <= 1'b1;
      PI_pe_regs <= 96'b000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000;
      PI_tx_busy <= 1'b0;
    end

    // state 14
    if (cycle == 13) begin
      PI_rst <= 1'b0;
      PI_rx_data <= 8'b11000111;
      PI_rx_valid <= 1'b1;
      PI_pe_regs <= 96'b000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000;
      PI_tx_busy <= 1'b0;
    end

    // state 15
    if (cycle == 14) begin
      PI_rst <= 1'b0;
      PI_rx_data <= 8'b10000111;
      PI_rx_valid <= 1'b1;
      PI_pe_regs <= 96'b000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000;
      PI_tx_busy <= 1'b1;
    end

    // state 16
    if (cycle == 15) begin
      PI_rst <= 1'b0;
      PI_rx_data <= 8'b10000000;
      PI_rx_valid <= 1'b0;
      PI_pe_regs <= 96'b000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000;
      PI_tx_busy <= 1'b1;
    end

    // state 17
    if (cycle == 16) begin
      PI_rst <= 1'b0;
      PI_rx_data <= 8'b00000111;
      PI_rx_valid <= 1'b1;
      PI_pe_regs <= 96'b000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000;
      PI_tx_busy <= 1'b1;
    end

    // state 18
    if (cycle == 17) begin
      PI_rst <= 1'b0;
      PI_rx_data <= 8'b00000111;
      PI_rx_valid <= 1'b1;
      PI_pe_regs <= 96'b000000000000000000000000000001110000000000000000000000000000000000000000000000000000000000000000;
      PI_tx_busy <= 1'b1;
    end

    // state 19
    if (cycle == 18) begin
      PI_rst <= 1'b0;
      PI_rx_data <= 8'b00000111;
      PI_rx_valid <= 1'b0;
      PI_pe_regs <= 96'b000000110000000000000000000000000000000000000000000000000000000000000000000000000000000000000000;
      PI_tx_busy <= 1'b1;
    end

    // state 20
    if (cycle == 19) begin
      PI_rst <= 1'b0;
      PI_rx_data <= 8'b00000111;
      PI_rx_valid <= 1'b1;
      PI_pe_regs <= 96'b000000000000010100000000000000000000000000000111000000000000000000000000000000000000000000000000;
      PI_tx_busy <= 1'b0;
    end

    // state 21
    if (cycle == 20) begin
      PI_rst <= 1'b0;
      PI_rx_data <= 8'b11000111;
      PI_rx_valid <= 1'b1;
      PI_pe_regs <= 96'b000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000;
      PI_tx_busy <= 1'b0;
    end

    // state 22
    if (cycle == 21) begin
      PI_rst <= 1'b0;
      PI_rx_data <= 8'b00000001;
      PI_rx_valid <= 1'b0;
      PI_pe_regs <= 96'b000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000;
      PI_tx_busy <= 1'b0;
    end

    // state 23
    if (cycle == 22) begin
      PI_rst <= 1'b0;
      PI_rx_data <= 8'b00000111;
      PI_rx_valid <= 1'b0;
      PI_pe_regs <= 96'b000000110000000000000000000000000000000000000000000000000000000000000000000000000000000000000000;
      PI_tx_busy <= 1'b1;
    end

    // state 24
    if (cycle == 23) begin
      PI_rst <= 1'b0;
      PI_rx_data <= 8'b00000011;
      PI_rx_valid <= 1'b0;
      PI_pe_regs <= 96'b000000000000000000000000000000000000000000000111000000000000000000000000000000000000000000000000;
      PI_tx_busy <= 1'b0;
    end

    // state 25
    if (cycle == 24) begin
      PI_rst <= 1'b0;
      PI_rx_data <= 8'b00000011;
      PI_rx_valid <= 1'b1;
      PI_pe_regs <= 96'b000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000;
      PI_tx_busy <= 1'b0;
    end

    // state 26
    if (cycle == 25) begin
      PI_rst <= 1'b0;
      PI_rx_data <= 8'b00000111;
      PI_rx_valid <= 1'b1;
      PI_pe_regs <= 96'b000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000;
      PI_tx_busy <= 1'b1;
    end

    // state 27
    if (cycle == 26) begin
      PI_rst <= 1'b0;
      PI_rx_data <= 8'b00000111;
      PI_rx_valid <= 1'b1;
      PI_pe_regs <= 96'b000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000;
      PI_tx_busy <= 1'b0;
    end

    // state 28
    if (cycle == 27) begin
      PI_rst <= 1'b0;
      PI_rx_data <= 8'b00000111;
      PI_rx_valid <= 1'b1;
      PI_pe_regs <= 96'b000000010000000000000000000000000000000000000000000000000000000000000000000000000000000000000000;
      PI_tx_busy <= 1'b0;
    end

    // state 29
    if (cycle == 28) begin
      PI_rst <= 1'b0;
      PI_rx_data <= 8'b00000011;
      PI_rx_valid <= 1'b1;
      PI_pe_regs <= 96'b000000000000000100000000000000000000000000000000000000000000000000000000000000000000000000000000;
      PI_tx_busy <= 1'b1;
    end

    // state 30
    if (cycle == 29) begin
      PI_rst <= 1'b0;
      PI_rx_data <= 8'b00000111;
      PI_rx_valid <= 1'b0;
      PI_pe_regs <= 96'b000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000;
      PI_tx_busy <= 1'b0;
    end

    // state 31
    if (cycle == 30) begin
      PI_rst <= 1'b0;
      PI_rx_data <= 8'b00000111;
      PI_rx_valid <= 1'b1;
      PI_pe_regs <= 96'b000000000000000000000000000000000000000000000001000000000000000000000000000000000000000000000000;
      PI_tx_busy <= 1'b0;
    end

    // state 32
    if (cycle == 31) begin
      PI_rst <= 1'b0;
      PI_rx_data <= 8'b00000111;
      PI_rx_valid <= 1'b1;
      PI_pe_regs <= 96'b000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000;
      PI_tx_busy <= 1'b1;
    end

    // state 33
    if (cycle == 32) begin
      PI_rst <= 1'b0;
      PI_rx_data <= 8'b00000111;
      PI_rx_valid <= 1'b1;
      PI_pe_regs <= 96'b000000000000000000000000000000000000000000000000000111000000000000000000000000001000000000000000;
      PI_tx_busy <= 1'b1;
    end

    // state 34
    if (cycle == 33) begin
      PI_rst <= 1'b0;
      PI_rx_data <= 8'b00000111;
      PI_rx_valid <= 1'b0;
      PI_pe_regs <= 96'b000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000;
      PI_tx_busy <= 1'b0;
    end

    // state 35
    if (cycle == 34) begin
      PI_rst <= 1'b0;
      PI_rx_data <= 8'b00000111;
      PI_rx_valid <= 1'b1;
      PI_pe_regs <= 96'b000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000;
      PI_tx_busy <= 1'b1;
    end

    // state 36
    if (cycle == 35) begin
      PI_rst <= 1'b0;
      PI_rx_data <= 8'b00000001;
      PI_rx_valid <= 1'b1;
      PI_pe_regs <= 96'b000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000;
      PI_tx_busy <= 1'b1;
    end

    // state 37
    if (cycle == 36) begin
      PI_rst <= 1'b0;
      PI_rx_data <= 8'b00000111;
      PI_rx_valid <= 1'b1;
      PI_pe_regs <= 96'b000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000;
      PI_tx_busy <= 1'b0;
    end

    // state 38
    if (cycle == 37) begin
      PI_rst <= 1'b0;
      PI_rx_data <= 8'b00000111;
      PI_rx_valid <= 1'b1;
      PI_pe_regs <= 96'b000000001000000000000000000000000000000000000000000000000000000000000000000000000000000000000000;
      PI_tx_busy <= 1'b1;
    end

    // state 39
    if (cycle == 38) begin
      PI_rst <= 1'b0;
      PI_rx_data <= 8'b00000111;
      PI_rx_valid <= 1'b1;
      PI_pe_regs <= 96'b000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000;
      PI_tx_busy <= 1'b1;
    end

    // state 40
    if (cycle == 39) begin
      PI_rst <= 1'b0;
      PI_rx_data <= 8'b00000011;
      PI_rx_valid <= 1'b1;
      PI_pe_regs <= 96'b000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000;
      PI_tx_busy <= 1'b0;
    end

    // state 41
    if (cycle == 40) begin
      PI_rst <= 1'b0;
      PI_rx_data <= 8'b00000001;
      PI_rx_valid <= 1'b1;
      PI_pe_regs <= 96'b000000000000000000000000000000000000000000000000011110010000000000000000000000000000000000000000;
      PI_tx_busy <= 1'b1;
    end

    // state 42
    if (cycle == 41) begin
      PI_rst <= 1'b0;
      PI_rx_data <= 8'b00000001;
      PI_rx_valid <= 1'b0;
      PI_pe_regs <= 96'b000000000000000000000000000000000000011100000000000000000000000000000000000000000000000000000000;
      PI_tx_busy <= 1'b0;
    end

    // state 43
    if (cycle == 42) begin
      PI_rst <= 1'b0;
      PI_rx_data <= 8'b00000001;
      PI_rx_valid <= 1'b1;
      PI_pe_regs <= 96'b000000000000000000000000000000000000000000000000000000000000000010000000000000000000000000000000;
      PI_tx_busy <= 1'b0;
    end

    // state 44
    if (cycle == 43) begin
      PI_rst <= 1'b0;
      PI_rx_data <= 8'b00000011;
      PI_rx_valid <= 1'b1;
      PI_pe_regs <= 96'b000000000000000000000000000000000000000000000000000000000000000000000000011111110000000000000000;
      PI_tx_busy <= 1'b1;
    end

    // state 45
    if (cycle == 44) begin
      PI_rst <= 1'b0;
      PI_rx_data <= 8'b00000001;
      PI_rx_valid <= 1'b1;
      PI_pe_regs <= 96'b000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000;
      PI_tx_busy <= 1'b0;
    end

    // state 46
    if (cycle == 45) begin
      PI_rst <= 1'b0;
      PI_rx_data <= 8'b00000001;
      PI_rx_valid <= 1'b0;
      PI_pe_regs <= 96'b000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000;
      PI_tx_busy <= 1'b0;
    end

    // state 47
    if (cycle == 46) begin
      PI_rst <= 1'b0;
      PI_rx_data <= 8'b00000111;
      PI_rx_valid <= 1'b0;
      PI_pe_regs <= 96'b000000000000000000000000000000000000000000000000000000000000000000000111000000000000000000000000;
      PI_tx_busy <= 1'b1;
    end

    // state 48
    if (cycle == 47) begin
      PI_rst <= 1'b0;
      PI_rx_data <= 8'b00000011;
      PI_rx_valid <= 1'b1;
      PI_pe_regs <= 96'b000000000000000000000000000000000000000000000000100000000000000000000000000000000000000000000110;
      PI_tx_busy <= 1'b0;
    end

    // state 49
    if (cycle == 48) begin
      PI_rst <= 1'b0;
      PI_rx_data <= 8'b00000111;
      PI_rx_valid <= 1'b1;
      PI_pe_regs <= 96'b000000000000000000000000000000000000000000000000000000000000000000000000000000000000000011111001;
      PI_tx_busy <= 1'b0;
    end

    // state 50
    if (cycle == 49) begin
      PI_rst <= 1'b0;
      PI_rx_data <= 8'b00000111;
      PI_rx_valid <= 1'b1;
      PI_pe_regs <= 96'b000000000000000000000000000000000000000000000000000000000000000000000000000000000000000010000000;
      PI_tx_busy <= 1'b1;
    end

    // state 51
    if (cycle == 50) begin
      PI_rst <= 1'b0;
      PI_rx_data <= 8'b00000001;
      PI_rx_valid <= 1'b1;
      PI_pe_regs <= 96'b000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000;
      PI_tx_busy <= 1'b0;
    end

    // state 52
    if (cycle == 51) begin
      PI_rst <= 1'b0;
      PI_rx_data <= 8'b00000111;
      PI_rx_valid <= 1'b1;
      PI_pe_regs <= 96'b000000001000000000000000000000000000000000000000000000000000000000000000000000000000000000000000;
      PI_tx_busy <= 1'b0;
    end

    // state 53
    if (cycle == 52) begin
      PI_rst <= 1'b0;
      PI_rx_data <= 8'b00000001;
      PI_rx_valid <= 1'b1;
      PI_pe_regs <= 96'b000000000000000000000000000000000000000000000000000000000000000000000000000000000000000100000000;
      PI_tx_busy <= 1'b1;
    end

    // state 54
    if (cycle == 53) begin
      PI_rst <= 1'b0;
      PI_rx_data <= 8'b00000111;
      PI_rx_valid <= 1'b0;
      PI_pe_regs <= 96'b000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000;
      PI_tx_busy <= 1'b1;
    end

    // state 55
    if (cycle == 54) begin
      PI_rst <= 1'b0;
      PI_rx_data <= 8'b00000111;
      PI_rx_valid <= 1'b1;
      PI_pe_regs <= 96'b000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000;
      PI_tx_busy <= 1'b0;
    end

    // state 56
    if (cycle == 55) begin
      PI_rst <= 1'b0;
      PI_rx_data <= 8'b00000111;
      PI_rx_valid <= 1'b1;
      PI_pe_regs <= 96'b000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000;
      PI_tx_busy <= 1'b1;
    end

    // state 57
    if (cycle == 56) begin
      PI_rst <= 1'b0;
      PI_rx_data <= 8'b10001000;
      PI_rx_valid <= 1'b1;
      PI_pe_regs <= 96'b000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000;
      PI_tx_busy <= 1'b0;
    end

    // state 58
    if (cycle == 57) begin
      PI_rst <= 1'b0;
      PI_rx_data <= 8'b00000011;
      PI_rx_valid <= 1'b1;
      PI_pe_regs <= 96'b000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000;
      PI_tx_busy <= 1'b0;
    end

    // state 59
    if (cycle == 58) begin
      PI_rst <= 1'b0;
      PI_rx_data <= 8'b00000011;
      PI_rx_valid <= 1'b1;
      PI_pe_regs <= 96'b000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000;
      PI_tx_busy <= 1'b1;
    end

    // state 60
    if (cycle == 59) begin
      PI_rst <= 1'b0;
      PI_rx_data <= 8'b00000011;
      PI_rx_valid <= 1'b1;
      PI_pe_regs <= 96'b000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000;
      PI_tx_busy <= 1'b0;
    end

    // state 61
    if (cycle == 60) begin
      PI_rst <= 1'b0;
      PI_rx_data <= 8'b00000111;
      PI_rx_valid <= 1'b1;
      PI_pe_regs <= 96'b000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000;
      PI_tx_busy <= 1'b0;
    end

    // state 62
    if (cycle == 61) begin
      PI_rst <= 1'b0;
      PI_rx_data <= 8'b00000111;
      PI_rx_valid <= 1'b1;
      PI_pe_regs <= 96'b000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000;
      PI_tx_busy <= 1'b1;
    end

    // state 63
    if (cycle == 62) begin
      PI_rst <= 1'b0;
      PI_rx_data <= 8'b00000111;
      PI_rx_valid <= 1'b1;
      PI_pe_regs <= 96'b000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000;
      PI_tx_busy <= 1'b0;
    end

    genclock <= cycle < 63;
    cycle <= cycle + 1;
  end
endmodule
