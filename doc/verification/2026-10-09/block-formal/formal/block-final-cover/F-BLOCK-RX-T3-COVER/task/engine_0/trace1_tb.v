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
  reg [0:0] PI_ready;
  reg [0:0] PI_rx_valid;
  wire [0:0] PI_clk = clock;
  block_rx UUT (
    .rst(PI_rst),
    .rx_data(PI_rx_data),
    .ready(PI_ready),
    .rx_valid(PI_rx_valid),
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
    // UUT.$auto$ghdl.\cc:825:import_module$2363  = 4'b0001;
    // UUT.$auto$ghdl.\cc:825:import_module$2371  = 4'b0001;
    // UUT.$auto$ghdl.\cc:825:import_module$2380  = 3'b001;
    UUT.active = 1'b0;
    UUT.bad = 1'b0;
    UUT.crc = 16'b1111111111111111;
    UUT.data = 1152'b000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000;
    UUT.escaped = 1'b0;
    UUT.length = 8'b00000000;
    UUT.timer = 2'b00;
    UUT.used = 8'b00000000;
    UUT.valid_o = 1'b0;

    // state 0
    PI_rst = 1'b0;
    PI_rx_data = 8'b01111110;
    PI_ready = 1'b1;
    PI_rx_valid = 1'b1;
  end
  always @(posedge clock) begin
    // state 1
    if (cycle == 0) begin
      PI_rst <= 1'b0;
      PI_rx_data <= 8'b01111101;
      PI_ready <= 1'b1;
      PI_rx_valid <= 1'b1;
    end

    // state 2
    if (cycle == 1) begin
      PI_rst <= 1'b0;
      PI_rx_data <= 8'b00000000;
      PI_ready <= 1'b0;
      PI_rx_valid <= 1'b0;
    end

    // state 3
    if (cycle == 2) begin
      PI_rst <= 1'b0;
      PI_rx_data <= 8'b01011110;
      PI_ready <= 1'b0;
      PI_rx_valid <= 1'b1;
    end

    // state 4
    if (cycle == 3) begin
      PI_rst <= 1'b0;
      PI_rx_data <= 8'b00000000;
      PI_ready <= 1'b0;
      PI_rx_valid <= 1'b0;
    end

    genclock <= cycle < 4;
    cycle <= cycle + 1;
  end
endmodule
