// RUN: cd %S && obelisk -emit-slang %s -o %t.mlir
// RUN: FileCheck %s < %t.mlir

`define SDF_TASK $sdf_``annotate

module sdf_macro_generated;
  initial `SDF_TASK("Inputs/sdf-aot-static.sdf");
endmodule

// CHECK: callee_name = "$sdf_annotate"
// CHECK-SAME: obelisk.sdf_compile_time_applied
