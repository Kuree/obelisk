// RUN: obelisk -emit-sim %s | FileCheck %s

module casex_lowering;
  logic [1:0] selector;
  logic [7:0] value;
  always_comb begin
    casex (selector)
      2'b0x: value = 8'h1;
      default: value = 8'h0;
    endcase
  end
endmodule

// CHECK: obelisk_sim.logic.compare casexz_eq
// CHECK-NOT: obelisk.sv.
