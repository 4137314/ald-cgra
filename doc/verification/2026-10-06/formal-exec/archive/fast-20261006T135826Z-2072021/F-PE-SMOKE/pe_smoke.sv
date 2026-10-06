// F-PE-RESET/HOLD/NOP/CONST. One synchronous clock, initial reset;
// subsequent reset, step, config and all four 16-bit operands unconstrained.
module pe_smoke(input clk);
    (* anyseq *) reg rst, step;
    (* anyseq *) reg [31:0] cfg;
    (* anyseq *) reg [15:0] n, s, e, w;
    wire [15:0] q;
    pe_ports dut(clk, rst, step, cfg, n, s, e, w, q);
    reg past_valid = 0;
    always @(posedge clk) begin
        past_valid <= 1;
        if (!past_valid) assume(rst);
        if (past_valid) begin
            if ($past(rst)) assert(q == 0);
            if (!$past(rst) && !$past(step)) assert(q == $past(q));
            if (!$past(rst) && $past(step) && $past(cfg[19:16]) == 0)
                assert(q == $past(q));
            if (!$past(rst) && $past(step) && $past(cfg[19:16]) == 15)
                assert(q == $past(cfg[15:0]));
`ifdef NEGATIVE
            // Deliberately false after reset. Acceptance requires FAIL + trace.
            if ($past(rst)) assert(q != 0);
`endif
            cover(!rst && q != 0);
        end
    end
endmodule
