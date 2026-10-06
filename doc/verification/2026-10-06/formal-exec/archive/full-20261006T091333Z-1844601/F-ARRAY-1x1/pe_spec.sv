// Independent 16-bit transition specification shared by PE/array monitors.
// No state, input assumptions or substitute DUT. Production pe.vhd is imported
// separately; the unsigned low-half product differs from its signed ALU.
module formal_pe_spec(input [31:0] cfg,
                      input [15:0] n, s, e, w, q,
                      output [15:0] a, b, output reg [15:0] alu);
    function [15:0] operand(input [2:0] sel);
        case (sel)
            0: operand = n;
            1: operand = s;
            2: operand = e;
            3: operand = w;
            4: operand = cfg[15:0];
            5: operand = q;
            default: operand = 16'b0;
        endcase
    endfunction
    assign a = operand(cfg[22:20]);
    assign b = operand(cfg[25:23]);
    // Low-half multiplication is unsigned modulo 2^16. This deliberately
    // differs from the DUT's signed 32-bit product and MAC extension.
    wire [15:0] product = a * b;

    always @* begin
        case (cfg[19:16])
             0: alu = q;
             1: alu = a;
             2: alu = a + b;
             3: alu = a - b;
             4: alu = product;
             5: alu = product + q;
             6: alu = a & b;
             7: alu = a | b;
             8: alu = a ^ b;
             9: alu = a << b[3:0];
            10: alu = $signed(a) >>> b[3:0];
            11: alu = ($signed(a) > $signed(b)) ? a : b;
            12: alu = ($signed(a) < $signed(b)) ? a : b;
            13: alu = a[15] ? (~a + 16'd1) : a;
            14: alu = q + a;
            15: alu = cfg[15:0];
        endcase
    end
endmodule
