// RUN: cd %S/../Inputs/sdf && talos -emit-slang %s -o %t.mlir
// RUN: FileCheck %s < %t.mlir

// IEEE 1800-2017 32.4/.5 exact decimal scaling cannot reject a large
// significand merely because its exponent would previously require a large
// power temporary. Only the 19 result digits and first discarded rounding
// digit are material to this exactly representable femtosecond delay.

// CHECK: timing_delay_count = 1 : i64
// CHECK-SAME: timing_delay_fs = array<i64: 9223372036854775799>

`timescale 1fs / 1fs
module sdf_resource_leaf(input wire source, output wire destination);
  assign destination = source;
  specify (source => destination) = 1; endspecify
endmodule
module sdf_scaled_decimal_boundary;
  logic source;
  wire destination;
  sdf_resource_leaf dut(source, destination);
  initial begin
    $sdf_annotate("scaled-decimal-boundary.sdf", dut);
  end
endmodule
