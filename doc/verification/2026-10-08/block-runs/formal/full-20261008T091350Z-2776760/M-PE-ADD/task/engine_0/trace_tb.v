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
  pe_contract UUT (
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
    // UUT.$auto$async2sync.\cc:107:execute$9685  = 1'b0;
    // UUT.$auto$async2sync.\cc:116:execute$9689  = 1'b1;
    UUT._witness_.anyinit_auto_ff_cc_337_slice_378 = 1'b1;
    UUT._witness_.anyinit_auto_ff_cc_337_slice_379 = 1'b1;
    UUT._witness_.anyinit_auto_ff_cc_337_slice_380 = 1'b1;
    UUT._witness_.anyinit_auto_ff_cc_337_slice_381 = 1'b1;
    UUT._witness_.anyinit_auto_ff_cc_337_slice_382 = 1'b1;
    UUT._witness_.anyinit_auto_ff_cc_337_slice_383 = 1'b1;
    UUT._witness_.anyinit_auto_ff_cc_337_slice_384 = 1'b1;
    UUT._witness_.anyinit_auto_ff_cc_337_slice_385 = 1'b1;
    UUT._witness_.anyinit_auto_ff_cc_337_slice_386 = 1'b1;
    UUT._witness_.anyinit_auto_ff_cc_337_slice_387 = 1'b1;
    UUT._witness_.anyinit_auto_ff_cc_337_slice_388 = 1'b1;
    UUT._witness_.anyinit_auto_ff_cc_337_slice_389 = 1'b1;
    UUT._witness_.anyinit_auto_ff_cc_337_slice_390 = 1'b1;
    UUT._witness_.anyinit_auto_ff_cc_337_slice_391 = 1'b1;
    UUT._witness_.anyinit_auto_ff_cc_337_slice_392 = 1'b1;
    UUT._witness_.anyinit_auto_ff_cc_337_slice_393 = 1'b1;
    UUT.dut.dut.r = 16'b0000000000000000;
    UUT.past_valid = 1'b0;

    // state 0
    UUT.cfg = 32'b00000000000000000000000000000000;
    UUT.rst = 1'b1;
    UUT.step = 1'b0;
    UUT.n = 16'b0000000000000000;
    UUT.s = 16'b0000000000000000;
    UUT.e = 16'b0000000000000000;
    UUT.w = 16'b0000000000000000;
  end
  always @(posedge clock) begin
    // state 1
    if (cycle == 0) begin
      UUT.cfg <= 32'b00000000100000100011110101110011;
      UUT.rst <= 1'b0;
      UUT.step <= 1'b1;
      UUT.n <= 16'b0100110101101010;
      UUT.s <= 16'b0111010001010001;
      UUT.e <= 16'b0000000000000000;
      UUT.w <= 16'b0000000000000000;
    end

    // state 2
    if (cycle == 1) begin
      UUT.cfg <= 32'b00000000000000000000000000000000;
      UUT.rst <= 1'b0;
      UUT.step <= 1'b0;
      UUT.n <= 16'b0000000000000000;
      UUT.s <= 16'b0000000000000000;
      UUT.e <= 16'b0000000000000000;
      UUT.w <= 16'b0000000000000000;
    end

    // state 3
    if (cycle == 2) begin
      UUT.cfg <= 32'b00000000000000000000000000000000;
      UUT.rst <= 1'b0;
      UUT.step <= 1'b0;
      UUT.n <= 16'b0000000000000000;
      UUT.s <= 16'b0000000000000000;
      UUT.e <= 16'b0000000000000000;
      UUT.w <= 16'b0000000000000000;
    end

    genclock <= cycle < 3;
    cycle <= cycle + 1;
  end
endmodule
