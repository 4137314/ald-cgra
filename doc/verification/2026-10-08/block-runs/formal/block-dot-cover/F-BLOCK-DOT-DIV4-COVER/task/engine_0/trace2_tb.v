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
  reg [0:0] PI_wr_bank;
  reg [10:0] PI_count;
  reg [9:0] PI_wr_addr;
  reg [15:0] PI_wr_data;
  wire [0:0] PI_clk = clock;
  reg [0:0] PI_wr_en;
  reg [0:0] PI_start;
  reg [15:0] PI_pe_result;
  block_dot UUT (
    .rst(PI_rst),
    .wr_bank(PI_wr_bank),
    .count(PI_count),
    .wr_addr(PI_wr_addr),
    .wr_data(PI_wr_data),
    .clk(PI_clk),
    .wr_en(PI_wr_en),
    .start(PI_start),
    .pe_result(PI_pe_result)
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
    UUT.a = 16'b0000000000000000;
    UUT.b = 16'b0000000000000000;
    UUT.done = 1'b0;
    UUT.guard = 3'b000;
    UUT.index = 10'b0000000000;
    UUT.remaining = 11'b00000000000;
    UUT.result = 16'b0000000000000000;
    UUT.state = 3'b000;
    UUT.bank_a[10'b0000000000] = 16'b0000000000000000;
    UUT.bank_a[10'b0000000001] = 16'b0000000000000000;
    UUT.bank_b[10'b0000000000] = 16'b0000000000000000;
    UUT.bank_b[10'b0000000001] = 16'b0000000000000000;

    // state 0
    PI_rst = 1'b0;
    PI_wr_bank = 1'b0;
    PI_count = 11'b00000000010;
    PI_wr_addr = 10'b0000000000;
    PI_wr_data = 16'b0000000000000000;
    PI_wr_en = 1'b0;
    PI_start = 1'b1;
    PI_pe_result = 16'b0000000000000000;
  end
  always @(posedge clock) begin
    // state 1
    if (cycle == 0) begin
      PI_rst <= 1'b0;
      PI_wr_bank <= 1'b0;
      PI_count <= 11'b00000000000;
      PI_wr_addr <= 10'b0000000000;
      PI_wr_data <= 16'b0000000000000000;
      PI_wr_en <= 1'b0;
      PI_start <= 1'b0;
      PI_pe_result <= 16'b0000000000000000;
    end

    // state 2
    if (cycle == 1) begin
      PI_rst <= 1'b0;
      PI_wr_bank <= 1'b0;
      PI_count <= 11'b00000000000;
      PI_wr_addr <= 10'b0000000000;
      PI_wr_data <= 16'b0000000000000000;
      PI_wr_en <= 1'b0;
      PI_start <= 1'b0;
      PI_pe_result <= 16'b0000000000000000;
    end

    // state 3
    if (cycle == 2) begin
      PI_rst <= 1'b0;
      PI_wr_bank <= 1'b0;
      PI_count <= 11'b00000000001;
      PI_wr_addr <= 10'b0000000000;
      PI_wr_data <= 16'b0000000000000000;
      PI_wr_en <= 1'b0;
      PI_start <= 1'b1;
      PI_pe_result <= 16'b0000000000000000;
    end

    // state 4
    if (cycle == 3) begin
      PI_rst <= 1'b0;
      PI_wr_bank <= 1'b0;
      PI_count <= 11'b00000000000;
      PI_wr_addr <= 10'b0000000000;
      PI_wr_data <= 16'b0000000000000000;
      PI_wr_en <= 1'b0;
      PI_start <= 1'b1;
      PI_pe_result <= 16'b0000000000000000;
    end

    // state 5
    if (cycle == 4) begin
      PI_rst <= 1'b0;
      PI_wr_bank <= 1'b0;
      PI_count <= 11'b00000000000;
      PI_wr_addr <= 10'b0000000000;
      PI_wr_data <= 16'b0000000000000000;
      PI_wr_en <= 1'b0;
      PI_start <= 1'b1;
      PI_pe_result <= 16'b0000000000000000;
    end

    // state 6
    if (cycle == 5) begin
      PI_rst <= 1'b0;
      PI_wr_bank <= 1'b0;
      PI_count <= 11'b00000000001;
      PI_wr_addr <= 10'b0000000000;
      PI_wr_data <= 16'b0000000000000000;
      PI_wr_en <= 1'b0;
      PI_start <= 1'b1;
      PI_pe_result <= 16'b0000000000000000;
    end

    // state 7
    if (cycle == 6) begin
      PI_rst <= 1'b0;
      PI_wr_bank <= 1'b0;
      PI_count <= 11'b00000000001;
      PI_wr_addr <= 10'b0000000000;
      PI_wr_data <= 16'b0000000000000000;
      PI_wr_en <= 1'b0;
      PI_start <= 1'b1;
      PI_pe_result <= 16'b0000000000000000;
    end

    // state 8
    if (cycle == 7) begin
      PI_rst <= 1'b0;
      PI_wr_bank <= 1'b0;
      PI_count <= 11'b00000000000;
      PI_wr_addr <= 10'b0000000000;
      PI_wr_data <= 16'b0000000000000000;
      PI_wr_en <= 1'b0;
      PI_start <= 1'b1;
      PI_pe_result <= 16'b0000000000000000;
    end

    // state 9
    if (cycle == 8) begin
      PI_rst <= 1'b0;
      PI_wr_bank <= 1'b0;
      PI_count <= 11'b00000000000;
      PI_wr_addr <= 10'b0000000000;
      PI_wr_data <= 16'b0000000000000000;
      PI_wr_en <= 1'b0;
      PI_start <= 1'b1;
      PI_pe_result <= 16'b0000000000000000;
    end

    // state 10
    if (cycle == 9) begin
      PI_rst <= 1'b0;
      PI_wr_bank <= 1'b0;
      PI_count <= 11'b00000000000;
      PI_wr_addr <= 10'b0000000000;
      PI_wr_data <= 16'b0000000000000000;
      PI_wr_en <= 1'b0;
      PI_start <= 1'b1;
      PI_pe_result <= 16'b0000000000000000;
    end

    // state 11
    if (cycle == 10) begin
      PI_rst <= 1'b0;
      PI_wr_bank <= 1'b0;
      PI_count <= 11'b00000000001;
      PI_wr_addr <= 10'b0000000000;
      PI_wr_data <= 16'b0000000000000000;
      PI_wr_en <= 1'b0;
      PI_start <= 1'b1;
      PI_pe_result <= 16'b0000000000000000;
    end

    // state 12
    if (cycle == 11) begin
      PI_rst <= 1'b0;
      PI_wr_bank <= 1'b0;
      PI_count <= 11'b00000000000;
      PI_wr_addr <= 10'b0000000000;
      PI_wr_data <= 16'b0000000000000000;
      PI_wr_en <= 1'b0;
      PI_start <= 1'b1;
      PI_pe_result <= 16'b0000000000000000;
    end

    // state 13
    if (cycle == 12) begin
      PI_rst <= 1'b0;
      PI_wr_bank <= 1'b0;
      PI_count <= 11'b00000000000;
      PI_wr_addr <= 10'b0000000000;
      PI_wr_data <= 16'b0000000000000000;
      PI_wr_en <= 1'b0;
      PI_start <= 1'b1;
      PI_pe_result <= 16'b0000000000000000;
    end

    // state 14
    if (cycle == 13) begin
      PI_rst <= 1'b0;
      PI_wr_bank <= 1'b0;
      PI_count <= 11'b00000000000;
      PI_wr_addr <= 10'b0000000000;
      PI_wr_data <= 16'b0000000000000000;
      PI_wr_en <= 1'b0;
      PI_start <= 1'b1;
      PI_pe_result <= 16'b0000000000000000;
    end

    // state 15
    if (cycle == 14) begin
      PI_rst <= 1'b0;
      PI_wr_bank <= 1'b0;
      PI_count <= 11'b00000000000;
      PI_wr_addr <= 10'b0000000000;
      PI_wr_data <= 16'b0000000000000000;
      PI_wr_en <= 1'b0;
      PI_start <= 1'b0;
      PI_pe_result <= 16'b0000000000000000;
    end

    genclock <= cycle < 15;
    cycle <= cycle + 1;
  end
endmodule
