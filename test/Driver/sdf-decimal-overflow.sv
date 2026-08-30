// RUN: cd %S && not obelisk -emit-slang %s -o %t.mlir 2> %t.err
// RUN: FileCheck %s < %t.err
// RUN: test ! -e %t.mlir

module sdf_decimal_cell(input wire source, output wire destination);
  timeunit 1fs;
  timeprecision 1fs;
  assign destination = source;
  specify (source => destination) = 1; endspecify
endmodule

module sdf_decimal_overflow;
  timeunit 1fs;
  timeprecision 1fs;
  logic source;
  wire destination;
  sdf_decimal_cell dut(source, destination);
  initial $sdf_annotate("Inputs/sdf-decimal-overflow.sdf", dut);
endmodule

// Rounding exactly half a femtosecond above INT64_MAX must be rejected before
// the signed Clause 30 delay field is materialized.
// CHECK-COUNT-1: error: SDF delay is incompatible with target precision
// CHECK-NOT: IOPATH requires 1, 2, 3, 6, or 12 delay values
