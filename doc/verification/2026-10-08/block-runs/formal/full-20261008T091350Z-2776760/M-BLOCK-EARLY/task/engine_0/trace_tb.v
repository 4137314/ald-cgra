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
    // UUT.$auto$ghdl.\cc:825:import_module$147  = 4'b0001;
    // UUT.$auto$ghdl.\cc:825:import_module$173  = 3'b001;
    // UUT.$auto$ghdl.\cc:825:import_module$184  = 4'b0001;
    // UUT.$auto$ghdl.\cc:825:import_module$223  = 4'b0001;
    // UUT.$auto$ghdl.\cc:825:import_module$241  = 4'b0001;
    UUT.block_dot_contract.f_a = 16'b0000000000000000;
    UUT.block_dot_contract.f_age = 2'b10;
    UUT.block_dot_contract.f_b = 16'b0000000000000000;
    UUT.block_dot_contract.f_config_age = 2'b10;
    UUT.block_dot_contract.f_data_age = 2'b10;
    UUT.block_dot_contract.f_index = 10'b0000000000;
    UUT.block_dot_contract.f_operand_write = 1'b0;
    UUT.block_dot_contract.f_pending = 11'b00000000000;
    UUT.block_dot_contract.f_previous_result = 16'b0000000000000000;
    UUT.block_dot_contract.f_seen = 1'b0;
    UUT.done = 1'b0;
    UUT.guard = 2'b00;
    UUT.index = 10'b0000000000;
    UUT.operand_a = 16'b0000000000000000;
    UUT.operand_b = 16'b0000000000000000;
    UUT.owned = 1'b0;
    UUT.qa = 16'b0000000000000000;
    UUT.qb = 16'b0000000000000000;
    UUT.remaining = 11'b00000000000;
    UUT.result = 16'b0000000000000000;
    UUT.state = 3'b000;
    UUT.bank_a[10'b0000000000] = 16'b1000000000000000;
    UUT.bank_b[10'b0000000000] = 16'b0000000000000000;

    // state 0
    PI_rst = 1'b0;
    PI_wr_bank = 1'b0;
    PI_count = 11'b00001000000;
    PI_wr_addr = 10'b1000000000;
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
      PI_count <= 11'b01000000000;
      PI_wr_addr <= 10'b1000000000;
      PI_wr_data <= 16'b0000000000000000;
      PI_wr_en <= 1'b0;
      PI_start <= 1'b1;
      PI_pe_result <= 16'b1000000000000000;
    end

    // state 2
    if (cycle == 1) begin
      PI_rst <= 1'b0;
      PI_wr_bank <= 1'b0;
      PI_count <= 11'b00000000010;
      PI_wr_addr <= 10'b0000000000;
      PI_wr_data <= 16'b0000000000000000;
      PI_wr_en <= 1'b0;
      PI_start <= 1'b0;
      PI_pe_result <= 16'b1000000000000000;
    end

    // state 3
    if (cycle == 2) begin
      PI_rst <= 1'b0;
      PI_wr_bank <= 1'b0;
      PI_count <= 11'b00000000000;
      PI_wr_addr <= 10'b0000000000;
      PI_wr_data <= 16'b0000000000000000;
      PI_wr_en <= 1'b0;
      PI_start <= 1'b0;
      PI_pe_result <= 16'b1000000000000000;
    end

    // state 4
    if (cycle == 3) begin
      PI_rst <= 1'b0;
      PI_wr_bank <= 1'b0;
      PI_count <= 11'b00000000000;
      PI_wr_addr <= 10'b0000000000;
      PI_wr_data <= 16'b0000000000000000;
      PI_wr_en <= 1'b0;
      PI_start <= 1'b0;
      PI_pe_result <= 16'b0000000000000000;
    end

    genclock <= cycle < 4;
    cycle <= cycle + 1;
  end
endmodule
