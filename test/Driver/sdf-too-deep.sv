// RUN: cd %S && not obelisk -emit-slang %s -o %t.mlir 2>&1 | FileCheck %s

module sdf_too_deep;
  initial $sdf_annotate("Inputs/sdf-too-deep.sdf");
endmodule

// CHECK: Inputs/sdf-too-deep.sdf:4:
// CHECK-SAME: error: SDF nesting exceeds the supported limit of 64 lists
