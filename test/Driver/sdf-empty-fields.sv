// RUN: cd %S && obelisk -emit-slang %s -o %t.mlir
// RUN: FileCheck %s < %t.mlir

`timescale 1ns / 1ns

module sdf_empty_cell(input wire source, output wire destination);
  assign destination = source;
  specify
    (source => destination) =
        (101, 102, 103, 104, 105, 106,
         107, 108, 109, 110, 111, 112);
  endspecify
endmodule

module sdf_empty_fields;
  logic source;
  wire destination;
  sdf_empty_cell one(source, destination);
  sdf_empty_cell two(source, destination);
  sdf_empty_cell three(source, destination);
  sdf_empty_cell six(source, destination);
  sdf_empty_cell twelve(source, destination);
  initial $sdf_annotate("Inputs/sdf-empty-fields.sdf");
endmodule

// IEEE 1800-2017 32.3: empty fields retain all preannotation banks that
// cannot be derived solely from annotated fields, for every accepted tuple.
// CHECK-DAG: timing_delay_fs = array<i64: 101000000, 102000000, 103000000, 104000000, 105000000, 106000000, 107000000, 108000000, 109000000, 110000000, 111000000, 112000000>
// CHECK-DAG: timing_delay_fs = array<i64: 2000000, 102000000, 2000000, 2000000, 105000000, 106000000, 2000000, 2000000, 109000000, 110000000, 111000000, 112000000>
// CHECK-DAG: timing_delay_fs = array<i64: 2000000, 102000000, 4000000, 2000000, 4000000, 106000000, 2000000, 2000000, 109000000, 110000000, 4000000, 112000000>
// CHECK-DAG: timing_delay_fs = array<i64: 2000000, 102000000, 4000000, 104000000, 6000000, 106000000, 2000000, 108000000, 109000000, 110000000, 6000000, 112000000>
// CHECK-DAG: timing_delay_fs = array<i64: 2000000, 102000000, 4000000, 104000000, 6000000, 106000000, 8000000, 108000000, 10000000, 110000000, 12000000, 112000000>
