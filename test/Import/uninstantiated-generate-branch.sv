// RUN: obelisk -emit-slang %s | FileCheck %s

// slang elaborates only the selected arm of a generate condition and models the
// other one with Invalid* placeholders, reporting nothing about it because that
// code is not part of the design. A reference that resolves only in the arm
// that was never elaborated must therefore not reject the compilation. This is
// the shape lowRISC's DV_FCOV_SIGNAL_GEN_IF macro expands to.

module top #(parameter bit Enable = 1'b0) (input logic clk, output logic out);
  if (Enable) begin : g_impl
    logic flag;
    assign flag = 1'b1;
  end

  logic fcov;
  if (Enable) begin : g_fcov
    assign fcov = g_impl.flag;
  end else begin : g_no_fcov
    assign fcov = 1'b0;
  end

  assign out = fcov;
endmodule

// The unselected arms are imported as uninstantiated declarations; their
// unresolvable contents are placeholders that never reach the design.
// CHECK: slang.symbol.generate_block
// CHECK-SAME: hierarchical_name = "top.g_impl"
// CHECK-SAME: is_uninstantiated = true
// CHECK: slang.symbol.generate_block
// CHECK-SAME: hierarchical_name = "top.g_fcov"
// CHECK-SAME: is_uninstantiated = true

// The elaborated arm is imported normally and drives the output.
// CHECK: slang.symbol.generate_block
// CHECK-SAME: hierarchical_name = "top.g_no_fcov"
// CHECK-SAME: is_uninstantiated = false
// CHECK: slang.symbol.continuous_assign
