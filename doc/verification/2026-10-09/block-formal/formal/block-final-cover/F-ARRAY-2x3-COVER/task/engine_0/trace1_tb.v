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
    // UUT.$auto$async2sync.\cc:107:execute$29986  = 1'b0;
    // UUT.$auto$async2sync.\cc:116:execute$29990  = 1'b1;
    // UUT.$auto$async2sync.\cc:116:execute$29996  = 1'b1;
    // UUT.$auto$async2sync.\cc:116:execute$30002  = 1'b1;
    // UUT.$auto$async2sync.\cc:116:execute$30008  = 1'b1;
    // UUT.$auto$async2sync.\cc:116:execute$30014  = 1'b1;
    UUT._witness_.anyinit_auto_ff_cc_337_slice_501 = 1'b1;
    UUT._witness_.anyinit_auto_ff_cc_337_slice_502 = 1'b1;
    UUT._witness_.anyinit_auto_ff_cc_337_slice_503 = 1'b1;
    UUT._witness_.anyinit_auto_ff_cc_337_slice_504 = 1'b1;
    UUT._witness_.anyinit_auto_ff_cc_337_slice_505 = 1'b1;
    UUT._witness_.anyinit_auto_ff_cc_337_slice_506 = 1'b1;
    UUT._witness_.anyinit_auto_ff_cc_337_slice_507 = 1'b1;
    UUT._witness_.anyinit_auto_ff_cc_337_slice_508 = 1'b1;
    UUT._witness_.anyinit_auto_ff_cc_337_slice_509 = 1'b1;
    UUT._witness_.anyinit_auto_ff_cc_337_slice_510 = 1'b1;
    UUT._witness_.anyinit_auto_ff_cc_337_slice_511 = 1'b1;
    UUT._witness_.anyinit_auto_ff_cc_337_slice_512 = 1'b1;
    UUT._witness_.anyinit_auto_ff_cc_337_slice_513 = 1'b1;
    UUT._witness_.anyinit_auto_ff_cc_337_slice_514 = 1'b1;
    UUT._witness_.anyinit_auto_ff_cc_337_slice_515 = 1'b1;
    UUT._witness_.anyinit_auto_ff_cc_337_slice_516 = 1'b1;
    UUT._witness_.anyinit_auto_ff_cc_337_slice_517 = 1'b1;
    UUT._witness_.anyinit_auto_ff_cc_337_slice_518 = 1'b1;
    UUT._witness_.anyinit_auto_ff_cc_337_slice_519 = 1'b1;
    UUT._witness_.anyinit_auto_ff_cc_337_slice_520 = 1'b1;
    UUT._witness_.anyinit_auto_ff_cc_337_slice_521 = 1'b1;
    UUT._witness_.anyinit_auto_ff_cc_337_slice_522 = 1'b1;
    UUT._witness_.anyinit_auto_ff_cc_337_slice_523 = 1'b1;
    UUT._witness_.anyinit_auto_ff_cc_337_slice_524 = 1'b1;
    UUT._witness_.anyinit_auto_ff_cc_337_slice_525 = 1'b1;
    UUT._witness_.anyinit_auto_ff_cc_337_slice_526 = 1'b1;
    UUT._witness_.anyinit_auto_ff_cc_337_slice_527 = 1'b1;
    UUT._witness_.anyinit_auto_ff_cc_337_slice_528 = 1'b1;
    UUT._witness_.anyinit_auto_ff_cc_337_slice_529 = 1'b1;
    UUT._witness_.anyinit_auto_ff_cc_337_slice_530 = 1'b1;
    UUT._witness_.anyinit_auto_ff_cc_337_slice_531 = 1'b1;
    UUT._witness_.anyinit_auto_ff_cc_337_slice_532 = 1'b1;
    UUT._witness_.anyinit_auto_ff_cc_337_slice_533 = 1'b1;
    UUT._witness_.anyinit_auto_ff_cc_337_slice_534 = 1'b1;
    UUT._witness_.anyinit_auto_ff_cc_337_slice_535 = 1'b1;
    UUT._witness_.anyinit_auto_ff_cc_337_slice_536 = 1'b1;
    UUT._witness_.anyinit_auto_ff_cc_337_slice_537 = 1'b1;
    UUT._witness_.anyinit_auto_ff_cc_337_slice_538 = 1'b1;
    UUT._witness_.anyinit_auto_ff_cc_337_slice_539 = 1'b1;
    UUT._witness_.anyinit_auto_ff_cc_337_slice_540 = 1'b1;
    UUT._witness_.anyinit_auto_ff_cc_337_slice_541 = 1'b1;
    UUT._witness_.anyinit_auto_ff_cc_337_slice_542 = 1'b1;
    UUT._witness_.anyinit_auto_ff_cc_337_slice_543 = 1'b1;
    UUT._witness_.anyinit_auto_ff_cc_337_slice_544 = 1'b1;
    UUT._witness_.anyinit_auto_ff_cc_337_slice_545 = 1'b1;
    UUT._witness_.anyinit_auto_ff_cc_337_slice_546 = 1'b1;
    UUT._witness_.anyinit_auto_ff_cc_337_slice_547 = 1'b1;
    UUT._witness_.anyinit_auto_ff_cc_337_slice_548 = 1'b1;
    UUT._witness_.anyinit_auto_ff_cc_337_slice_549 = 1'b1;
    UUT._witness_.anyinit_auto_ff_cc_337_slice_550 = 1'b1;
    UUT._witness_.anyinit_auto_ff_cc_337_slice_551 = 1'b1;
    UUT._witness_.anyinit_auto_ff_cc_337_slice_552 = 1'b1;
    UUT._witness_.anyinit_auto_ff_cc_337_slice_553 = 1'b1;
    UUT._witness_.anyinit_auto_ff_cc_337_slice_554 = 1'b1;
    UUT._witness_.anyinit_auto_ff_cc_337_slice_555 = 1'b1;
    UUT._witness_.anyinit_auto_ff_cc_337_slice_556 = 1'b1;
    UUT._witness_.anyinit_auto_ff_cc_337_slice_557 = 1'b1;
    UUT._witness_.anyinit_auto_ff_cc_337_slice_558 = 1'b1;
    UUT._witness_.anyinit_auto_ff_cc_337_slice_559 = 1'b1;
    UUT._witness_.anyinit_auto_ff_cc_337_slice_560 = 1'b1;
    UUT._witness_.anyinit_auto_ff_cc_337_slice_561 = 1'b1;
    UUT._witness_.anyinit_auto_ff_cc_337_slice_562 = 1'b1;
    UUT._witness_.anyinit_auto_ff_cc_337_slice_563 = 1'b1;
    UUT._witness_.anyinit_auto_ff_cc_337_slice_564 = 1'b1;
    UUT._witness_.anyinit_auto_ff_cc_337_slice_565 = 1'b1;
    UUT._witness_.anyinit_auto_ff_cc_337_slice_566 = 1'b1;
    UUT._witness_.anyinit_auto_ff_cc_337_slice_567 = 1'b1;
    UUT._witness_.anyinit_auto_ff_cc_337_slice_568 = 1'b1;
    UUT._witness_.anyinit_auto_ff_cc_337_slice_569 = 1'b1;
    UUT._witness_.anyinit_auto_ff_cc_337_slice_570 = 1'b1;
    UUT._witness_.anyinit_auto_ff_cc_337_slice_571 = 1'b1;
    UUT._witness_.anyinit_auto_ff_cc_337_slice_572 = 1'b1;
    UUT._witness_.anyinit_auto_ff_cc_337_slice_573 = 1'b1;
    UUT._witness_.anyinit_auto_ff_cc_337_slice_574 = 1'b1;
    UUT._witness_.anyinit_auto_ff_cc_337_slice_575 = 1'b1;
    UUT._witness_.anyinit_auto_ff_cc_337_slice_576 = 1'b1;
    UUT._witness_.anyinit_auto_ff_cc_337_slice_577 = 1'b1;
    UUT._witness_.anyinit_auto_ff_cc_337_slice_578 = 1'b1;
    UUT._witness_.anyinit_auto_ff_cc_337_slice_579 = 1'b1;
    UUT._witness_.anyinit_auto_ff_cc_337_slice_580 = 1'b1;
    UUT._witness_.anyinit_auto_ff_cc_337_slice_581 = 1'b1;
    UUT._witness_.anyinit_auto_ff_cc_337_slice_582 = 1'b1;
    UUT._witness_.anyinit_auto_ff_cc_337_slice_583 = 1'b1;
    UUT._witness_.anyinit_auto_ff_cc_337_slice_584 = 1'b1;
    UUT._witness_.anyinit_auto_ff_cc_337_slice_585 = 1'b1;
    UUT._witness_.anyinit_auto_ff_cc_337_slice_586 = 1'b1;
    UUT._witness_.anyinit_auto_ff_cc_337_slice_587 = 1'b1;
    UUT._witness_.anyinit_auto_ff_cc_337_slice_588 = 1'b1;
    UUT._witness_.anyinit_auto_ff_cc_337_slice_589 = 1'b1;
    UUT._witness_.anyinit_auto_ff_cc_337_slice_590 = 1'b1;
    UUT._witness_.anyinit_auto_ff_cc_337_slice_591 = 1'b1;
    UUT._witness_.anyinit_auto_ff_cc_337_slice_592 = 1'b1;
    UUT._witness_.anyinit_auto_ff_cc_337_slice_593 = 1'b1;
    UUT._witness_.anyinit_auto_ff_cc_337_slice_594 = 1'b1;
    UUT._witness_.anyinit_auto_ff_cc_337_slice_595 = 1'b1;
    UUT._witness_.anyinit_auto_ff_cc_337_slice_596 = 1'b1;
    UUT.dut.dut.\gen_rows:2 .\gen_cols:3 .u_pe.past_valid = 1'b0;

    // state 0
    UUT.cfg = 192'b000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000;
    UUT.abstract_q = 16'b0000000000000000;
    UUT.west = 32'b00000000000000000000000000000000;
    UUT.abstract_q = 16'b0000000000000000;
    UUT.rst = 1'b1;
    UUT.step = 1'b0;
    UUT.abstract_q = 16'b0000000000000000;
    UUT.north = 48'b000000000000000000000000000000000000000000000000;
    UUT.abstract_q = 16'b0000000000000000;
    UUT.abstract_q = 16'b0000000000000000;
    UUT.abstract_q = 16'b0000000000000000;
  end
  always @(posedge clock) begin
    // state 1
    if (cycle == 0) begin
      UUT.cfg <= 192'b000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000001000010000000000000000;
      UUT.abstract_q <= 16'b0000000000000000;
      UUT.west <= 32'b00000000000000000000000000000000;
      UUT.abstract_q <= 16'b0000000000000000;
      UUT.rst <= 1'b0;
      UUT.step <= 1'b1;
      UUT.abstract_q <= 16'b0000000000000000;
      UUT.north <= 48'b000000000000000000000000000000000000000000000000;
      UUT.abstract_q <= 16'b0000000000000000;
      UUT.abstract_q <= 16'b0000000000000000;
      UUT.abstract_q <= 16'b0000000000000000;
    end

    // state 2
    if (cycle == 1) begin
      UUT.cfg <= 192'b000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000;
      UUT.abstract_q <= 16'b0000000000000000;
      UUT.west <= 32'b00000000000000000000000000000000;
      UUT.abstract_q <= 16'b0000000000000000;
      UUT.rst <= 1'b0;
      UUT.step <= 1'b0;
      UUT.abstract_q <= 16'b0000000000000000;
      UUT.north <= 48'b000000000000000000000000000000000000000000000000;
      UUT.abstract_q <= 16'b0000000000000000;
      UUT.abstract_q <= 16'b0000000000000000;
      UUT.abstract_q <= 16'b0000000000000000;
    end

    genclock <= cycle < 2;
    cycle <= cycle + 1;
  end
endmodule
