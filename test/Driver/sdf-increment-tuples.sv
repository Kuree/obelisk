// RUN: cd %S && obelisk -emit-slang %s -o %t.mlir
// RUN: FileCheck %s < %t.mlir

module sdf_tuple_cell(input wire source, output wire destination);
  timeunit 1fs;
  timeprecision 1fs;
  assign destination = source;
  specify (source => destination) = 100; endspecify
endmodule

module sdf_increment_tuples;
  timeunit 1fs;
  timeprecision 1fs;
  logic source;
  wire one_destination;
  wire three_destination;
  wire six_destination;
  wire twelve_destination;
  sdf_tuple_cell one(source, one_destination);
  sdf_tuple_cell three(source, three_destination);
  sdf_tuple_cell six(source, six_destination);
  sdf_tuple_cell twelve(source, twelve_destination);
  initial begin
    // IEEE 1800-2017 32.6 applies each signed/sparse INCREMENT field after
    // the standard Clause 30 transition tuple has been mapped to 12 banks.
    $sdf_annotate("Inputs/sdf-increment-tuples.sdf");
  end
endmodule

// CHECK-DAG: timing_delay_count = 12 : i64, timing_delay_fs = array<i64: 99, 99, 99, 99, 99, 99, 99, 99, 99, 99, 99, 99>
// CHECK-DAG: timing_delay_count = 12 : i64, timing_delay_fs = array<i64: 101, 98, 100, 101, 100, 98, 100, 101, 100, 98, 100, 98>
// CHECK-DAG: timing_delay_count = 12 : i64, timing_delay_fs = array<i64: 99, 102, 97, 104, 95, 106, 97, 104, 95, 106, 97, 104>
// CHECK-DAG: timing_delay_count = 12 : i64, timing_delay_fs = array<i64: 101, 98, 103, 96, 105, 94, 107, 92, 109, 90, 111, 88>
// CHECK-NOT: slang.symbol.sdf
// CHECK-NOT: obelisk.sdf.table
// CHECK-NOT: obelisk_sdf.
