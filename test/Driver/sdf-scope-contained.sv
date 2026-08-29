// RUN: cd %S && obelisk -emit-slang %s -o %t.mlir 2>%t.err
// RUN: FileCheck %s --check-prefix=IR < %t.mlir
// RUN: FileCheck %s --check-prefix=WARN < %t.err

`timescale 1ns / 1ns

module sdf_scope_leaf(input wire source, output wire destination);
  assign destination = source;
  specify (source => destination) = 9; endspecify
endmodule

module sdf_scope_branch(input wire source, output wire destination);
  sdf_scope_leaf leaf(source, destination);
endmodule

module sdf_scope_contained;
  logic source;
  wire left_destination, right_destination;
  sdf_scope_branch left(source, left_destination);
  sdf_scope_branch right(source, right_destination);
  initial $sdf_annotate("Inputs/sdf-scope-contained.sdf", left);
endmodule

// IR-COUNT-2: timing_delay_fs = array<i64: 9000000>
// IR-NOT: timing_delay_fs = array<i64: 2000000>
// WARN: Inputs/sdf-scope-contained.sdf:4:3: warning: SDF CELL did not match an elaborated instance
