// RUN: not obelisk -emit-sim %s 2>&1 | FileCheck %s

module unsupported_casex;
  logic [1:0] selector;
  logic [7:0] value;
  always_comb begin
    casex (selector)
      2'b0x: value = 8'h1;
      default: value = 8'h0;
    endcase
  end
endmodule

// CHECK: unsupported semantic node in the first simulation slice
// CHECK-SAME: wildcard, inside, and pattern cases
