// F-ARRAY: every PE transition from the PREVIOUS row-major register vector.
// All configurations/edges are symbolic each cycle, including unrelated PEs.
// This checks wiring, borders, muxes, arithmetic and synchronous capture
// together, without abstracting any production PE or truncating operands.
module array_contract(input clk);
    parameter ROWS=1, COLS=1;
    localparam COUNT=ROWS*COLS;
    (* anyseq *) reg rst, step;
`ifdef MUTATION_NORTH
    // Targeted negative stimulus only, never an acceptance-proof restriction:
    // load distinct constants, then route north. The reached trace must fail.
    (* anyseq *) reg load_constants;
    wire [32*COUNT-1:0] cfg;
    genvar stimulus_pe;
    generate for (stimulus_pe=0; stimulus_pe<COUNT; stimulus_pe=stimulus_pe+1) begin: stimulus
        assign cfg[32*stimulus_pe+:32] = load_constants ? 32'h000f0000+stimulus_pe+1 : 32'h00010000;
    end endgenerate
`else
    (* anyseq *) reg [32*COUNT-1:0] cfg;
`endif
    (* anyseq *) reg [16*ROWS-1:0] west;
    (* anyseq *) reg [16*COLS-1:0] north;
    wire [16*COUNT-1:0] q;
    array_ports dut(clk, rst, step, cfg, west, north, q);
    reg past_valid=0;
    always @(posedge clk) begin
        past_valid <= 1;
        if (!past_valid) assume(rst);
        cover(past_valid && !rst && step && q != 0);
    end
    genvar i;
    generate for (i=0; i<COUNT; i=i+1) begin: cell_contract
        localparam R=i/COLS, C=i%COLS;
        wire [15:0] n, s, e, w;
        if (R == 0) assign n=north[16*C+:16];
        else assign n=q[16*(i-COLS)+:16];
        if (R == ROWS-1) assign s=0;
        else assign s=q[16*(i+COLS)+:16];
        if (C == 0) assign w=west[16*R+:16];
        else assign w=q[16*(i-1)+:16];
        if (C == COLS-1) assign e=0;
        else assign e=q[16*(i+1)+:16];
        wire [15:0] a, b, alu;
        formal_pe_spec reference(cfg[32*i+:32], n, s, e, w, q[16*i+:16], a, b, alu);
        wire [15:0] next_q = rst ? 16'b0 : step ? alu : q[16*i+:16];
        always @(posedge clk)
            assert(!past_valid || q[16*i+:16] == $past(next_q));
    end endgenerate
    // Exercise all neighbor selector paths after reset, including edge zeros.
    genvar direction;
    generate for (direction=0; direction<4; direction=direction+1) begin: route_cover
        always @(posedge clk)
            cover(past_valid && !rst && step && cfg[19:16] == 1 && cfg[22:20] == direction);
    end endgenerate
endmodule
