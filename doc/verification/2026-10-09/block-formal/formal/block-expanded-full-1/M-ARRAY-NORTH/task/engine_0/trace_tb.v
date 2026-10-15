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
    // UUT.$auto$async2sync.\cc:107:execute$2043  = 1'b0;
    // UUT.$auto$async2sync.\cc:116:execute$2017  = 1'b1;
    // UUT.$auto$async2sync.\cc:116:execute$2023  = 1'b1;
    // UUT.$auto$async2sync.\cc:116:execute$2029  = 1'b1;
    // UUT.$auto$async2sync.\cc:116:execute$2035  = 1'b1;
    // UUT.$auto$async2sync.\cc:116:execute$2041  = 1'b1;
    // UUT.$auto$async2sync.\cc:116:execute$2047  = 1'b1;
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
    UUT._witness_.anyinit_auto_ff_cc_337_slice_597 = 1'b1;
    UUT._witness_.anyinit_auto_ff_cc_337_slice_598 = 1'b1;
    UUT._witness_.anyinit_auto_ff_cc_337_slice_599 = 1'b1;
    UUT._witness_.anyinit_auto_ff_cc_337_slice_600 = 1'b1;
    UUT._witness_.anyinit_auto_ff_cc_337_slice_601 = 1'b1;
    UUT._witness_.anyinit_auto_ff_cc_337_slice_602 = 1'b1;
    UUT._witness_.anyinit_auto_ff_cc_337_slice_603 = 1'b1;
    UUT._witness_.anyinit_auto_ff_cc_337_slice_604 = 1'b1;
    UUT._witness_.anyinit_auto_ff_cc_337_slice_605 = 1'b1;
    UUT._witness_.anyinit_auto_ff_cc_337_slice_606 = 1'b1;
    UUT._witness_.anyinit_auto_ff_cc_337_slice_607 = 1'b1;
    UUT._witness_.anyinit_auto_ff_cc_337_slice_608 = 1'b1;
    UUT._witness_.anyinit_auto_ff_cc_337_slice_609 = 1'b1;
    UUT._witness_.anyinit_auto_ff_cc_337_slice_610 = 1'b1;
    UUT._witness_.anyinit_auto_ff_cc_337_slice_611 = 1'b1;
    UUT._witness_.anyinit_auto_ff_cc_337_slice_612 = 1'b1;
    UUT._witness_.anyinit_auto_ff_cc_337_slice_613 = 1'b1;
    UUT._witness_.anyinit_auto_ff_cc_337_slice_614 = 1'b1;
    UUT._witness_.anyinit_auto_ff_cc_337_slice_615 = 1'b1;
    UUT._witness_.anyinit_auto_ff_cc_337_slice_616 = 1'b1;
    UUT._witness_.anyinit_auto_ff_cc_337_slice_617 = 1'b1;
    UUT._witness_.anyinit_auto_ff_cc_337_slice_618 = 1'b1;
    UUT._witness_.anyinit_auto_ff_cc_337_slice_619 = 1'b1;
    UUT._witness_.anyinit_auto_ff_cc_337_slice_620 = 1'b1;
    UUT._witness_.anyinit_auto_ff_cc_337_slice_621 = 1'b1;
    UUT._witness_.anyinit_auto_ff_cc_337_slice_622 = 1'b1;
    UUT._witness_.anyinit_auto_ff_cc_337_slice_623 = 1'b1;
    UUT._witness_.anyinit_auto_ff_cc_337_slice_624 = 1'b1;
    UUT._witness_.anyinit_auto_ff_cc_337_slice_625 = 1'b1;
    UUT._witness_.anyinit_auto_ff_cc_337_slice_626 = 1'b1;
    UUT._witness_.anyinit_auto_ff_cc_337_slice_627 = 1'b1;
    UUT._witness_.anyinit_auto_ff_cc_337_slice_628 = 1'b1;
    UUT._witness_.anyinit_auto_ff_cc_337_slice_629 = 1'b1;
    UUT._witness_.anyinit_auto_ff_cc_337_slice_630 = 1'b1;
    UUT._witness_.anyinit_auto_ff_cc_337_slice_631 = 1'b1;
    UUT._witness_.anyinit_auto_ff_cc_337_slice_632 = 1'b1;
    UUT._witness_.anyinit_auto_ff_cc_337_slice_633 = 1'b1;
    UUT._witness_.anyinit_auto_ff_cc_337_slice_634 = 1'b1;
    UUT._witness_.anyinit_auto_ff_cc_337_slice_635 = 1'b1;
    UUT._witness_.anyinit_auto_ff_cc_337_slice_636 = 1'b1;
    UUT._witness_.anyinit_auto_ff_cc_337_slice_637 = 1'b1;
    UUT._witness_.anyinit_auto_ff_cc_337_slice_638 = 1'b1;
    UUT._witness_.anyinit_auto_ff_cc_337_slice_639 = 1'b1;
    UUT._witness_.anyinit_auto_ff_cc_337_slice_640 = 1'b1;
    UUT._witness_.anyinit_auto_ff_cc_337_slice_641 = 1'b1;
    UUT._witness_.anyinit_auto_ff_cc_337_slice_642 = 1'b1;
    UUT._witness_.anyinit_auto_ff_cc_337_slice_643 = 1'b1;
    UUT._witness_.anyinit_auto_ff_cc_337_slice_644 = 1'b1;
    UUT._witness_.anyinit_auto_ff_cc_337_slice_645 = 1'b1;
    UUT._witness_.anyinit_auto_ff_cc_337_slice_646 = 1'b1;
    UUT.dut.dut.\gen_rows:2 .\gen_cols:3 .u_pe.past_valid = 1'b0;

    // state 0
    UUT.north = 48'b000000000000000000000000000000000000000000000000;
    UUT.load_constants = 1'b0;
    UUT.abstract_q = 16'b0000000000000000;
    UUT.abstract_q = 16'b0000000000000000;
    UUT.abstract_q = 16'b0000000000000000;
    UUT.abstract_q = 16'b0000000000000000;
    UUT.abstract_q = 16'b0000000000000000;
    UUT.abstract_q = 16'b0000000000000000;
    UUT.rst = 1'b1;
    UUT.step = 1'b0;
  end
  always @(posedge clock) begin
    // state 1
    if (cycle == 0) begin
      UUT.north <= 48'b100000000000000000000000000000000000000000000000;
      UUT.load_constants <= 1'b0;
      UUT.abstract_q <= 16'b0000000000000000;
      UUT.abstract_q <= 16'b0000000000000000;
      UUT.abstract_q <= 16'b0000000000000000;
      UUT.abstract_q <= 16'b0000000000000000;
      UUT.abstract_q <= 16'b0000000000000000;
      UUT.abstract_q <= 16'b0000000000000000;
      UUT.rst <= 1'b0;
      UUT.step <= 1'b1;
    end

    // state 2
    if (cycle == 1) begin
      UUT.north <= 48'b000000000000000000000000000000000000000000000000;
      UUT.load_constants <= 1'b0;
      UUT.abstract_q <= 16'b0000000000000000;
      UUT.abstract_q <= 16'b0000000000000000;
      UUT.abstract_q <= 16'b0000000000000000;
      UUT.abstract_q <= 16'b1000000000000000;
      UUT.abstract_q <= 16'b0000000000000000;
      UUT.abstract_q <= 16'b0000000000000000;
      UUT.rst <= 1'b0;
      UUT.step <= 1'b1;
    end

    // state 3
    if (cycle == 2) begin
      UUT.north <= 48'b000000000000000000000000000000000000000000000000;
      UUT.load_constants <= 1'b0;
      UUT.abstract_q <= 16'b0000000000000000;
      UUT.abstract_q <= 16'b0000000000000000;
      UUT.abstract_q <= 16'b0000000000000000;
      UUT.abstract_q <= 16'b0000000000000000;
      UUT.abstract_q <= 16'b0000000000000000;
      UUT.abstract_q <= 16'b0000000000000000;
      UUT.rst <= 1'b0;
      UUT.step <= 1'b0;
    end

    // state 4
    if (cycle == 3) begin
      UUT.north <= 48'b000000000000000000000000000000000000000000000000;
      UUT.load_constants <= 1'b0;
      UUT.abstract_q <= 16'b0000000000000000;
      UUT.abstract_q <= 16'b0000000000000000;
      UUT.abstract_q <= 16'b0000000000000000;
      UUT.abstract_q <= 16'b0000000000000000;
      UUT.abstract_q <= 16'b0000000000000000;
      UUT.abstract_q <= 16'b0000000000000000;
      UUT.rst <= 1'b0;
      UUT.step <= 1'b0;
    end

    genclock <= cycle < 4;
    cycle <= cycle + 1;
  end
endmodule
