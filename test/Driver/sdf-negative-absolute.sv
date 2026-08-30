// RUN: cd %S && not obelisk -emit-slang %s -o %t.mlir 2> %t.err
// RUN: FileCheck %s < %t.err
// RUN: test ! -e %t.mlir

module sdf_negative_absolute_cell(input wire source,
                                  output wire destination);
  assign destination = source;
  specify (source => destination) = 1; endspecify
endmodule

module sdf_negative_absolute;
  logic source;
  wire destination;
  sdf_negative_absolute_cell dut(source, destination);
  initial $sdf_annotate("Inputs/sdf-negative-absolute.sdf", dut);
endmodule

// CHECK: Inputs/sdf-negative-absolute.sdf:7:22: error: invalid nonnegative SDF delay value
