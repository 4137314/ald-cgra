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
  reg [0:0] PI_rx_serial;
  wire [0:0] PI_clk = clock;
  uart_rx UUT (
    .rst(PI_rst),
    .rx_serial(PI_rx_serial),
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
    // UUT.$auto$ghdl.\cc:825:import_module$434  = 3'b001;
    // UUT.$auto$ghdl.\cc:825:import_module$444  = 3'b001;
    // UUT.$auto$ghdl.\cc:825:import_module$453  = 3'b001;
    UUT.bit_idx = 3'b000;
    UUT.clk_cnt = 2'b00;
    UUT.data_r = 8'b00000000;
    UUT.rx_data = 8'b10000000;
    UUT.rx_s1 = 1'b1;
    UUT.rx_s2 = 1'b1;
    UUT.rx_valid = 1'b0;
    UUT.state = 3'b000;
    UUT.uart_rx_contract.f_seen = 1'b0;

    // state 0
    PI_rst = 1'b0;
    PI_rx_serial = 1'b0;
  end
  always @(posedge clock) begin
    // state 1
    if (cycle == 0) begin
      PI_rst <= 1'b0;
      PI_rx_serial <= 1'b0;
    end

    // state 2
    if (cycle == 1) begin
      PI_rst <= 1'b0;
      PI_rx_serial <= 1'b1;
    end

    // state 3
    if (cycle == 2) begin
      PI_rst <= 1'b0;
      PI_rx_serial <= 1'b0;
    end

    // state 4
    if (cycle == 3) begin
      PI_rst <= 1'b0;
      PI_rx_serial <= 1'b0;
    end

    // state 5
    if (cycle == 4) begin
      PI_rst <= 1'b0;
      PI_rx_serial <= 1'b0;
    end

    genclock <= cycle < 5;
    cycle <= cycle + 1;
  end
endmodule
