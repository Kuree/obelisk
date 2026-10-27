// RUN: %split-file %s %t
// RUN: cd %t && not %test-talos --test-max-sdf-matched-updates=4 -emit-slang design.sv -o %t/design.mlir 2> %t/err
// RUN: FileCheck %s < %t/err
// RUN: test ! -e %t/design.mlir

// One compact wildcard CELL amplifies its IOPATH records over every matching
// instance. The compile-time Clause 32 update cap must stop the entire fold
// promptly and emit one diagnostic, without per-match errors or warnings.
// Three records over three instances exceed a lowered limit of four.
// CHECK-COUNT-1: error: static SDF annotation-update resource limit exceeded
// CHECK-NOT: SDF delay is incompatible with target precision
// CHECK-NOT: warning:

//--- design.sv
`timescale 1fs / 1fs
module sdf_update_leaf(input wire source, output wire destination);
  assign destination = source;
  specify (source => destination) = 1; endspecify
endmodule
module sdf_matched_update_resource;
  logic source;
  wire [2:0] destination;
  sdf_update_leaf leaf_0(source, destination[0]);
  sdf_update_leaf leaf_1(source, destination[1]);
  sdf_update_leaf leaf_2(source, destination[2]);
  initial $sdf_annotate("updates.sdf");
endmodule
//--- updates.sdf
(DELAYFILE
  (SDFVERSION "4.0")
  (TIMESCALE 1fs)
  (CELL
    (CELLTYPE "sdf_update_leaf")
    (INSTANCE *)
    (DELAY (ABSOLUTE
      (IOPATH source destination (1))
      (IOPATH source destination (1))
      (IOPATH source destination (1))
    ))))
