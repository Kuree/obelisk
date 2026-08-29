// RUN: cd %S && obelisk -emit-slang %s -o %t.mlir 2>&1 | FileCheck %s

module sdf_unsupported_cell(input wire source, input wire destination);
endmodule

module sdf_unsupported_matched;
  logic source, destination;
  sdf_unsupported_cell dut(source, destination);
  initial $sdf_annotate("Inputs/sdf-unsupported-matched.sdf");
endmodule

// CHECK: Inputs/sdf-unsupported-matched.sdf:8:5: warning: unsupported SDF timing data in matching CELL: TIMINGCHECK
// CHECK-NOT: TIMINGENV
