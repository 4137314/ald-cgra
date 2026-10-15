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
    // UUT.$auto$ghdl.\cc:825:import_module$2120  = 4'b0001;
    // UUT.$auto$ghdl.\cc:825:import_module$2146  = 4'b0001;
    // UUT.$auto$ghdl.\cc:825:import_module$2174  = 4'b0001;
    // UUT.$auto$ghdl.\cc:825:import_module$2239  = 4'b0001;
    // UUT.$auto$ghdl.\cc:825:import_module$2270  = 4'b0001;
    // UUT.$auto$ghdl.\cc:825:import_module$252  = 4'b0001;
    // UUT.$auto$ghdl.\cc:825:import_module$267  = 4'b0001;
    // UUT.$auto$ghdl.\cc:825:import_module$290  = 4'b0001;
    // UUT.$auto$ghdl.\cc:825:import_module$301  = 4'b0001;
    UUT._witness_.anyinit__3836 = 8'b00000000;
    UUT._witness_.anyinit__809 = 2'b00;
    UUT._witness_.anyinit__957 = 1'b0;
    UUT.active = 1'b0;
    UUT.bad = 1'b0;
    UUT.block_rx_contract.f_crc = 16'b1111111111111111;
    UUT.block_rx_contract.f_previous_crc = 16'b1111111111111111;
    UUT.block_rx_contract.f_previous_data = 1152'b100000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000;
    UUT.block_rx_contract.f_previous_used = 8'b00000000;
    UUT.block_rx_contract.f_seen = 1'b0;
    UUT.block_rx_contract.f_was_append = 1'b0;
    UUT.block_rx_contract.f_written_byte = 8'b00000000;
    UUT.crc = 16'b1111111111111111;
    UUT.data = 1152'b000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000;
    UUT.escaped = 1'b0;
    UUT.length = 8'b10010001;
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
      PI_rx_data <= 8'b00000001;
      PI_ready <= 1'b1;
      PI_rx_valid <= 1'b1;
    end

    // state 2
    if (cycle == 1) begin
      PI_rst <= 1'b0;
      PI_rx_data <= 8'b00100101;
      PI_ready <= 1'b1;
      PI_rx_valid <= 1'b1;
    end

    // state 3
    if (cycle == 2) begin
      PI_rst <= 1'b0;
      PI_rx_data <= 8'b10010000;
      PI_ready <= 1'b1;
      PI_rx_valid <= 1'b1;
    end

    // state 4
    if (cycle == 3) begin
      PI_rst <= 1'b0;
      PI_rx_data <= 8'b10010000;
      PI_ready <= 1'b1;
      PI_rx_valid <= 1'b1;
    end

    // state 5
    if (cycle == 4) begin
      PI_rst <= 1'b0;
      PI_rx_data <= 8'b10010000;
      PI_ready <= 1'b1;
      PI_rx_valid <= 1'b1;
    end

    // state 6
    if (cycle == 5) begin
      PI_rst <= 1'b0;
      PI_rx_data <= 8'b00111111;
      PI_ready <= 1'b1;
      PI_rx_valid <= 1'b1;
    end

    // state 7
    if (cycle == 6) begin
      PI_rst <= 1'b0;
      PI_rx_data <= 8'b11111010;
      PI_ready <= 1'b1;
      PI_rx_valid <= 1'b1;
    end

    // state 8
    if (cycle == 7) begin
      PI_rst <= 1'b0;
      PI_rx_data <= 8'b11111110;
      PI_ready <= 1'b1;
      PI_rx_valid <= 1'b1;
    end

    // state 9
    if (cycle == 8) begin
      PI_rst <= 1'b0;
      PI_rx_data <= 8'b00000000;
      PI_ready <= 1'b1;
      PI_rx_valid <= 1'b1;
    end

    // state 10
    if (cycle == 9) begin
      PI_rst <= 1'b0;
      PI_rx_data <= 8'b00000000;
      PI_ready <= 1'b1;
      PI_rx_valid <= 1'b1;
    end

    // state 11
    if (cycle == 10) begin
      PI_rst <= 1'b0;
      PI_rx_data <= 8'b01111011;
      PI_ready <= 1'b1;
      PI_rx_valid <= 1'b1;
    end

    // state 12
    if (cycle == 11) begin
      PI_rst <= 1'b0;
      PI_rx_data <= 8'b00000000;
      PI_ready <= 1'b1;
      PI_rx_valid <= 1'b1;
    end

    // state 13
    if (cycle == 12) begin
      PI_rst <= 1'b0;
      PI_rx_data <= 8'b01111110;
      PI_ready <= 1'b1;
      PI_rx_valid <= 1'b1;
    end

    // state 14
    if (cycle == 13) begin
      PI_rst <= 1'b0;
      PI_rx_data <= 8'b00000000;
      PI_ready <= 1'b0;
      PI_rx_valid <= 1'b0;
    end

    genclock <= cycle < 14;
    cycle <= cycle + 1;
  end
endmodule
