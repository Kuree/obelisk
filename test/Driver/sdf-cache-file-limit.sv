// RUN: %split-file %s %t
// RUN: cd %t && not %test-obelisk --test-max-sdf-cache-files=2 -emit-slang design.sv -o %t/design.mlir 2> %t/err
// RUN: FileCheck %s < %t/err
// RUN: test ! -e %t/design.mlir

// IEEE 1800-2017 32.3 allows many annotation files, but static compilation
// admits a deterministic bounded cache rather than unbounded attacker-chosen
// file, byte, or parsed-entry state. The test driver lowers the file limit.
// CHECK-COUNT-1: error: static SDF annotation file-count resource limit exceeded

//--- design.sv
`timescale 1fs / 1fs
module sdf_resource_leaf(input wire source, output wire destination);
  assign destination = source;
  specify (source => destination) = 1; endspecify
endmodule
module sdf_cache_file_limit;
  logic source;
  wire destination;
  sdf_resource_leaf dut(source, destination);
  initial begin
    $sdf_annotate("first.sdf", dut);
    $sdf_annotate("second.sdf", dut);
    $sdf_annotate("third.sdf", dut);
  end
endmodule
//--- first.sdf
(DELAYFILE
  (SDFVERSION "4.0")
  (TIMESCALE 1fs)
  (CELL (CELLTYPE "sdf_resource_leaf") (INSTANCE)
    (DELAY (ABSOLUTE (IOPATH source destination (1))))))
//--- second.sdf
(DELAYFILE
  (SDFVERSION "4.0")
  (TIMESCALE 1fs)
  (CELL (CELLTYPE "sdf_resource_leaf") (INSTANCE)
    (DELAY (ABSOLUTE (IOPATH source destination (1))))))
//--- third.sdf
(DELAYFILE
  (SDFVERSION "4.0")
  (TIMESCALE 1fs)
  (CELL (CELLTYPE "sdf_resource_leaf") (INSTANCE)
    (DELAY (ABSOLUTE (IOPATH source destination (1))))))
