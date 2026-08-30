// RUN: cd %S && obelisk -emit-slang %s -o %t.mlir
// RUN: FileCheck %s < %t.mlir

module sdf_decimal_cell(input wire source, output wire destination);
  timeunit 1fs;
  timeprecision 1fs;
  assign destination = source;
  specify (source => destination) = 1; endspecify
endmodule

module sdf_decimal_boundary;
  timeunit 1fs;
  timeprecision 1fs;
  logic source;
  wire destination;
  sdf_decimal_cell dut(source, destination);
  initial begin
    // IEEE 1800-2017 32.4/.5 require exact decimal scaling. This valid
    // long fractional value just below the half-quantum signed boundary
    // rounds to the largest value in the Clause 30 delay bank.
    $sdf_annotate("Inputs/sdf-decimal-boundary.sdf", dut);
  end
endmodule

// CHECK: timing_delay_count = 1 : i64
// CHECK-SAME: timing_delay_fs = array<i64: 9223372036854775807>
