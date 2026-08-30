// RUN: cd %S && obelisk -emit-slang %s -o %t.mlir
// RUN: FileCheck %s < %t.mlir

module sdf_order_cell(input wire source, output wire destination);
  timeunit 1fs;
  timeprecision 1fs;
  assign destination = source;
  specify (source => destination) = 1; endspecify
endmodule

module sdf_increment_absolute_order;
  timeunit 1fs;
  timeprecision 1fs;
  logic source;
  wire cross_destination, within_destination;
  sdf_order_cell cross_dut(source, cross_destination);
  sdf_order_cell within_dut(source, within_destination);
  initial begin
    // IEEE 1800-2017 32.5/.6: the later ABSOLUTE overwrites the earlier
    // INCREMENT both across files and inside one ordered SDF DELAY section.
    $sdf_annotate("Inputs/sdf-order-increment.sdf", cross_dut);
    $sdf_annotate("Inputs/sdf-order-absolute.sdf", cross_dut);
    $sdf_annotate("Inputs/sdf-order-within.sdf", within_dut);
  end
endmodule

// CHECK-DAG: hierarchical_name = "sdf_increment_absolute_order.cross_dut"{{.*}}timing_delay_fs = array<i64: 7
// CHECK-DAG: hierarchical_name = "sdf_increment_absolute_order.within_dut"{{.*}}timing_delay_fs = array<i64: 9
