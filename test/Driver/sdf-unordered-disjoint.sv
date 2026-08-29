// RUN: cd %S && obelisk -emit-slang %s -o %t.mlir
// RUN: FileCheck %s < %t.mlir

`timescale 1ns / 1ns

module sdf_unordered_leaf(input wire a, output wire y);
  assign y = a;
  specify (a => y) = 9; endspecify
endmodule

module sdf_unordered_disjoint;
  logic a;
  wire left_y, right_y;
  sdf_unordered_leaf left(a, left_y);
  sdf_unordered_leaf right(a, right_y);
  initial $sdf_annotate("Inputs/sdf-unordered-scope.sdf", left);
  initial $sdf_annotate("Inputs/sdf-unordered-scope.sdf", right);
endmodule

// CHECK-COUNT-2: timing_delay_fs = array<i64: 2000000>
// CHECK-COUNT-2: obelisk.sdf_compile_time_applied
