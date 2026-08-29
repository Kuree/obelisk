// RUN: cd %S && not obelisk -emit-slang %s -o /dev/null 2>&1 | FileCheck %s

`timescale 1ns / 1ns

module sdf_invalid_delay_cell(input wire source, output wire destination);
  assign destination = source;
  specify (source => destination) = 1; endspecify
endmodule

module sdf_invalid_delay_count;
  logic source;
  wire destination;
  sdf_invalid_delay_cell dut(source, destination);
  initial $sdf_annotate("Inputs/sdf-invalid-delay-count.sdf", dut);
endmodule

// CHECK: Inputs/sdf-invalid-delay-count.sdf:9:9: error: IOPATH requires 1, 2, 3, 6, or 12 delay values
