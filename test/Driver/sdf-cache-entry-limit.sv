// RUN: %split-file %s %t
// RUN: cd %t && not %test-talos --test-max-sdf-cache-entries=8 -emit-slang design.sv -o %t/design.mlir 2> %t/err
// RUN: FileCheck %s < %t/err
// RUN: test ! -e %t/design.mlir

// Parsed files are charged by compact model entries as well as source bytes;
// IEEE 1800-2017 Clause 32 does not permit annotation text to create
// unbounded compiler-resident state. Each file holds six entries (three
// IOPATH records and their delay fields) against a lowered limit of eight.
// CHECK-COUNT-1: error: static SDF annotation parsed-entry resource limit exceeded

//--- design.sv
`timescale 1fs / 1fs
module sdf_resource_leaf(input wire source, output wire destination);
  assign destination = source;
  specify (source => destination) = 1; endspecify
endmodule
module sdf_parsed_entry_resource;
  logic source;
  wire destination;
  sdf_resource_leaf dut(source, destination);
  initial begin
    $sdf_annotate("entries-0.sdf", dut);
    $sdf_annotate("entries-1.sdf", dut);
  end
endmodule
//--- entries-0.sdf
(DELAYFILE
  (SDFVERSION "4.0")
  (TIMESCALE 1fs)
  (CELL
    (CELLTYPE "does_not_match")
    (INSTANCE)
    (DELAY (ABSOLUTE
      (IOPATH source destination (1))
      (IOPATH source destination (1))
      (IOPATH source destination (1))
    ))))
//--- entries-1.sdf
(DELAYFILE
  (SDFVERSION "4.0")
  (TIMESCALE 1fs)
  (CELL
    (CELLTYPE "does_not_match")
    (INSTANCE)
    (DELAY (ABSOLUTE
      (IOPATH source destination (1))
      (IOPATH source destination (1))
      (IOPATH source destination (1))
    ))))
