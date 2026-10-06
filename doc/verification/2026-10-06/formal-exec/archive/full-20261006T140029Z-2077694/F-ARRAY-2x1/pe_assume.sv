// Assume-guarantee boundary for ARRAY tasks ONLY. The production PE transition
// is proved separately by mandatory F-PE-ALU-MUX on the same RTL/spec hashes.
// Abstract outputs may initially be arbitrary; this is an overapproximation
// of the production zero initial state. No opcode or operand is restricted.
// The imported production ARRAY wiring and all its PE port connections remain.
module pe_Brtl(input clk, rst, step, input [31:0] cfg,
               input [15:0] \neigh[n] , \neigh[s] , \neigh[e] , \neigh[w] ,
               output [15:0] dout);
    (* anyseq *) reg [15:0] abstract_q;
    assign dout=abstract_q;
    wire [15:0] a,b,alu;
    formal_pe_spec reference(cfg, \neigh[n] , \neigh[s] , \neigh[e] , \neigh[w] ,
                            abstract_q, a,b,alu);
    wire [15:0] next_q=rst ? 16'b0 : step ? alu : abstract_q;
    reg past_valid=0;
    always @(posedge clk) begin
        past_valid <= 1;
        if (past_valid) assume(abstract_q == $past(next_q));
    end
endmodule
