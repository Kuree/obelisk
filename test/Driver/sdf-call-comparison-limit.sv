// RUN: %split-file %s %t
// RUN: cd %t && not %test-obelisk --test-max-sdf-application-work=10 -emit-slang design.sv -o %t/design.mlir 2> %t/err
// RUN: FileCheck %s < %t/err
// RUN: test ! -e %t/design.mlir

// Production application (not parser coverage): repeated same-file calls in
// one ordered initial block bypass the distinct-file cap but must not make the
// Clause 32.5/.6 call-order proof quadratic without a deterministic bound.
// Six calls need fifteen pair comparisons against a lowered limit of ten.
// CHECK-COUNT-1: error: static SDF annotation application-work resource limit exceeded

//--- design.sv
`timescale 1fs / 1fs
module sdf_resource_leaf(input wire source, output wire destination);
  assign destination = source;
  specify (source => destination) = 1; endspecify
endmodule
module sdf_call_comparison_resource;
  logic source;
  wire destination;
  sdf_resource_leaf dut(source, destination);
  initial begin
    $sdf_annotate("calls.sdf", dut);
    $sdf_annotate("calls.sdf", dut);
    $sdf_annotate("calls.sdf", dut);
    $sdf_annotate("calls.sdf", dut);
    $sdf_annotate("calls.sdf", dut);
    $sdf_annotate("calls.sdf", dut);
  end
endmodule
//--- calls.sdf
(DELAYFILE
  (SDFVERSION "4.0")
  (TIMESCALE 1fs)
  (CELL (CELLTYPE "sdf_resource_leaf") (INSTANCE dut)))
