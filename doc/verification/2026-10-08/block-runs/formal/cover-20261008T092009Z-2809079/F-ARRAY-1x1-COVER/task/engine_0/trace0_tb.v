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
  wire [0:0] PI_clk = clock;
  array_contract UUT (
    .clk(PI_clk)
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
    // UUT.$auto$async2sync.\cc:107:execute$9247  = 1'b0;
    // UUT.$auto$async2sync.\cc:116:execute$9239  = 1'b1;
    // UUT.$auto$async2sync.\cc:116:execute$9245  = 1'b1;
    // UUT.$auto$async2sync.\cc:116:execute$9251  = 1'b1;
    // UUT.$auto$async2sync.\cc:116:execute$9257  = 1'b1;
    // UUT.$auto$async2sync.\cc:116:execute$9263  = 1'b1;
    UUT.\cell_contract[0] .reference.q = 16'b0000000000000000;
    UUT.past_valid = 1'b0;

    // state 0
    UUT.west = 16'b0000000000000000;
    UUT.north = 16'b0000000000000000;
    UUT.rst = 1'b1;
    UUT.step = 1'b0;
    UUT.cfg = 32'b00000000000000000000000000000000;
  end
  always @(posedge clock) begin
    // state 1
    if (cycle == 0) begin
      UUT.west <= 16'b0000000000000000;
      UUT.north <= 16'b0000000000000000;
      UUT.rst <= 1'b0;
      UUT.step <= 1'b1;
      UUT.cfg <= 32'b00000000000000010000000000000000;
    end

    // state 2
    if (cycle == 1) begin
      UUT.west <= 16'b0000000000000000;
      UUT.north <= 16'b0000000000000000;
      UUT.rst <= 1'b0;
      UUT.step <= 1'b0;
      UUT.cfg <= 32'b00000000000000000000000000000000;
    end

    genclock <= cycle < 2;
    cycle <= cycle + 1;
  end
endmodule
