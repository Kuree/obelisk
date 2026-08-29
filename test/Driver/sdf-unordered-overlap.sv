// RUN: cd %S && not obelisk -emit-slang %s -o %t.mlir 2>&1 | FileCheck %s

`timescale 1ns / 1ns

module sdf_unordered_leaf(input wire a, output wire y);
  assign y = a;
  specify (a => y) = 9; endspecify
endmodule

module sdf_unordered_annotator;
  initial
    $sdf_annotate("Inputs/sdf-unordered-scope.sdf",
                  $root.sdf_unordered_overlap.dut);
endmodule

module sdf_unordered_overlap;
  logic a;
  wire y;
  sdf_unordered_leaf dut(a, y);
  sdf_unordered_annotator first();
  sdf_unordered_annotator second();
endmodule

// IEEE 1800-2017 32.5/.6 make successive annotation order observable.
// CHECK-COUNT-2: error: static $sdf_annotate cannot order calls across multiple initial blocks with overlapping scopes
