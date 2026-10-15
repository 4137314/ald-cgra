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
  reg [0:0] PI_tx_start;
  reg [7:0] PI_tx_data;
  wire [0:0] PI_clk = clock;
  uart_tx UUT (
    .rst(PI_rst),
    .tx_start(PI_tx_start),
    .tx_data(PI_tx_data),
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
    // UUT.$auto$ghdl.\cc:825:import_module$284  = 4'b0001;
    // UUT.$auto$ghdl.\cc:825:import_module$331  = 3'b001;
    UUT.bit_idx = 3'b000;
    UUT.clk_cnt = 2'b00;
    UUT.data_r = 8'b00000000;
    UUT.state = 2'b00;
    UUT.tx_busy = 1'b0;
    UUT.tx_serial = 1'b0;
    UUT.uart_tx_contract.f_seen = 1'b0;
    UUT.uart_tx_contract.f_word = 8'b00000000;

    // state 0
    PI_rst = 1'b0;
    PI_tx_start = 1'b1;
    PI_tx_data = 8'b10000000;
  end
  always @(posedge clock) begin
    // state 1
    if (cycle == 0) begin
      PI_rst <= 1'b0;
      PI_tx_start <= 1'b1;
      PI_tx_data <= 8'b00000000;
    end

    // state 2
    if (cycle == 1) begin
      PI_rst <= 1'b0;
      PI_tx_start <= 1'b0;
      PI_tx_data <= 8'b00000000;
    end

    // state 3
    if (cycle == 2) begin
      PI_rst <= 1'b0;
      PI_tx_start <= 1'b0;
      PI_tx_data <= 8'b00000000;
    end

    // state 4
    if (cycle == 3) begin
      PI_rst <= 1'b0;
      PI_tx_start <= 1'b0;
      PI_tx_data <= 8'b00000000;
    end

    genclock <= cycle < 4;
    cycle <= cycle + 1;
  end
endmodule
