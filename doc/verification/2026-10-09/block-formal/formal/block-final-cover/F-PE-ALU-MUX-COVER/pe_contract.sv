// F-PE-ALU/MUX: independent bit-vector specification, all 16 opcodes and
// both complete 3-bit selector domains (6 and reserved 7 select zero).
// No restriction on operand bits, config, step or subsequent reset cycles.
module pe_contract(input clk);
    (* anyseq *) reg rst, step;
    (* anyseq *) reg [31:0] cfg;
    (* anyseq *) reg [15:0] n, s, e, w;
    wire [15:0] q;
    pe_ports dut(clk, rst, step, cfg, n, s, e, w, q);

    wire [15:0] a, b, alu;
    formal_pe_spec reference(cfg, n, s, e, w, q, a, b, alu);
    wire [15:0] next_q = rst ? 16'b0 : step ? alu : q;
    reg past_valid = 0;
    always @(posedge clk) begin
        past_valid <= 1;
        if (!past_valid) assume(rst);
        if (past_valid) assert(q == $past(next_q));
    end
    genvar op;
    generate for (op=0; op<16; op=op+1) begin: opcode_reachable
        always @(posedge clk)
            cover(past_valid && !rst && step && cfg[19:16] == op);
    end endgenerate
    always @(posedge clk) begin
        cover(past_valid && !rst && step && cfg[19:16] == 13 && a == 16'h8000);
        cover(past_valid && !rst && step && cfg[19:16] == 2 && a == 16'hffff && b == 1);
        cover(past_valid && !rst && step && cfg[19:16] == 10 && a[15] && b[3:0] == 15);
        cover(past_valid && !rst && step && cfg[22:20] == 5 && q != 0);
    end
endmodule
