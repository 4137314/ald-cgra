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
  pe_smoke UUT (
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
    // UUT.$auto$async2sync.\cc:107:execute$165  = 1'b0;
    // UUT.$auto$async2sync.\cc:107:execute$171  = 1'b0;
    // UUT.$auto$async2sync.\cc:107:execute$177  = 1'b0;
    // UUT.$auto$async2sync.\cc:107:execute$183  = 1'b0;
    // UUT.$auto$async2sync.\cc:116:execute$163  = 1'b1;
    // UUT.$auto$async2sync.\cc:116:execute$169  = 1'b1;
    // UUT.$auto$async2sync.\cc:116:execute$175  = 1'b1;
    // UUT.$auto$async2sync.\cc:116:execute$181  = 1'b1;
    // UUT.$auto$async2sync.\cc:116:execute$187  = 1'b1;
    // UUT.$past$pe_smoke.\sv:14$55$0  = 1'b0;
    UUT._witness_.anyinit_procdff_143 = 1'b0;
    UUT._witness_.anyinit_procdff_144 = 1'b0;
    UUT._witness_.anyinit_procdff_145 = 16'b1000000000000000;
    UUT._witness_.anyinit_procdff_146 = 1'b0;
    UUT._witness_.anyinit_procdff_147 = 1'b0;
    UUT._witness_.anyinit_procdff_148 = 4'b0000;
    UUT._witness_.anyinit_procdff_149 = 16'b1000000000000000;
    UUT._witness_.anyinit_procdff_150 = 1'b0;
    UUT._witness_.anyinit_procdff_151 = 1'b0;
    UUT._witness_.anyinit_procdff_152 = 4'b0000;
    UUT._witness_.anyinit_procdff_153 = 16'b1000000000000000;
    UUT.dut.dut.r = 16'b0000000000000000;
    UUT.past_valid = 1'b0;

    // state 0
    UUT.cfg = 32'b00000000000000000000000000000000;
    UUT.e = 16'b0000000000000000;
    UUT.n = 16'b0000000000000000;
    UUT.s = 16'b0000000000000000;
    UUT.w = 16'b0000000000000000;
    UUT.rst = 1'b1;
    UUT.step = 1'b0;
  end
  always @(posedge clock) begin
    // state 1
    if (cycle == 0) begin
      UUT.cfg <= 32'b00000000000000000000000000000000;
      UUT.e <= 16'b0000000000000000;
      UUT.n <= 16'b0000000000000000;
      UUT.s <= 16'b0000000000000000;
      UUT.w <= 16'b0000000000000000;
      UUT.rst <= 1'b0;
      UUT.step <= 1'b0;
    end

    // state 2
    if (cycle == 1) begin
      UUT.cfg <= 32'b00000000000000000000000000000000;
      UUT.e <= 16'b0000000000000000;
      UUT.n <= 16'b0000000000000000;
      UUT.s <= 16'b0000000000000000;
      UUT.w <= 16'b0000000000000000;
      UUT.rst <= 1'b0;
      UUT.step <= 1'b0;
    end

    genclock <= cycle < 2;
    cycle <= cycle + 1;
  end
endmodule
