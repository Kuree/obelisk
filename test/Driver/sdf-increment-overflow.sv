// RUN: cd %S && not obelisk -emit-slang %s -o %t.mlir 2> %t.err
// RUN: FileCheck %s < %t.err
// RUN: test ! -e %t.mlir

module sdf_increment_overflow_cell(input wire source,
                                   output wire destination);
  timeunit 1fs;
  timeprecision 1fs;
  assign destination = source;
  specify (source => destination) = 9223372036854775800; endspecify
endmodule

module sdf_increment_underflow_cell(input wire source,
                                    output wire destination);
  timeunit 1fs;
  timeprecision 1fs;
  assign destination = source;
  specify (source => destination) = 5; endspecify
endmodule

module sdf_increment_overflow;
  timeunit 1fs;
  timeprecision 1fs;
  logic source;
  wire over_destination;
  wire under_destination;
  sdf_increment_overflow_cell over(source, over_destination);
  sdf_increment_underflow_cell under(source, under_destination);
  initial begin
    // IEEE 1800-2017 32.6 updates the currently effective value. Neither
    // signed underflow, including the exactly representable INT64_MIN
    // increment, nor the bounded time representation can be silently wrapped.
    $sdf_annotate("Inputs/sdf-increment-overflow.sdf");
  end
endmodule

// CHECK-COUNT-2: error: SDF INCREMENT produces a negative or overflowing path delay
