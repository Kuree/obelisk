// RUN: cd %S && not obelisk -emit-slang %s -o %t.mlir 2> %t.err
// RUN: FileCheck %s < %t.err
// RUN: test ! -e %t.mlir

module sdf_rounded_negative_overflow_cell(input wire source,
                                          output wire destination);
  timeunit 1fs;
  timeprecision 1fs;
  assign destination = source;
  specify (source => destination) = 1; endspecify
endmodule

module sdf_rounded_negative_overflow;
  timeunit 1fs;
  timeprecision 1fs;
  logic source;
  wire destination;
  sdf_rounded_negative_overflow_cell dut(source, destination);
  initial begin
    // IEEE 1800-2017 32.4/.5 and 3.14.1: rounding the half-quantum
    // magnitude away from zero must be diagnosed before signed conversion.
    $sdf_annotate("Inputs/sdf-rounded-negative-overflow.sdf");
  end
endmodule

// CHECK-COUNT-1: error: SDF delay is incompatible with target precision
