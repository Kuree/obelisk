// RUN: cd %S/../Inputs/sdf && not obelisk -emit-slang %s -o %t.mlir 2> %t.err
// RUN: FileCheck %s < %t.err
// RUN: test ! -e %t.mlir

// Production application/folding (not parser coverage): IEEE 1800-2017
// 32.4/.5 require the typed exact exponent spelling to be converted at the
// destination precision. Repeated enormous positive and negative orders must
// be classified linearly; neither may allocate an exponent-sized integer or
// power-of-ten temporary.

// CHECK-COUNT-2: error: SDF delay is incompatible with target precision
// CHECK-NOT: IOPATH requires 1, 2, 3, 6, or 12 delay values

`timescale 1fs / 1fs
module sdf_resource_leaf(input wire source, output wire destination);
  assign destination = source;
  specify (source => destination) = 1; endspecify
endmodule
module sdf_exponent_resource;
  logic source;
  wire destination;
  sdf_resource_leaf dut(source, destination);
  initial begin
    $sdf_annotate("exponent-resource.sdf", dut);
  end
endmodule
