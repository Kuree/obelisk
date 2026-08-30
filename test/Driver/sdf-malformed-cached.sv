// RUN: cd %S && not obelisk -emit-slang %s -o %t.mlir 2> %t.err
// RUN: FileCheck %s < %t.err
// RUN: test ! -e %t.mlir

module sdf_decimal_cell(input wire source, output wire destination);
  timeunit 1fs;
  timeprecision 1fs;
  assign destination = source;
  specify (source => destination) = 1; endspecify
endmodule

module sdf_malformed_cached;
  timeunit 1fs;
  timeprecision 1fs;
  logic source;
  wire destination;
  sdf_decimal_cell dut(source, destination);
  initial begin
    // IEEE 1800-2017 32.3 permits repeated annotation. A failed parse is a
    // stable cached result, so identical adversarial input is diagnosed once.
    $sdf_annotate("Inputs/sdf-malformed-cached.sdf", dut);
    $sdf_annotate("Inputs/sdf-malformed-cached.sdf", dut);
  end
endmodule

// CHECK-COUNT-1: error: expected a bounded exact decimal spelling
// CHECK-NOT: IOPATH requires 1, 2, 3, 6, or 12 delay values
