// RUN: obelisk -emit-obelisk %s -o - | FileCheck %s

`timescale 1ns / 1ps

module system_timing_check_semantic(
    input wire reference, data, condition);
  reg notifier;
  reg delayed_reference;
  reg delayed_data;

  specify
    $setup(posedge data &&& condition, posedge reference, 1, notifier);
    $hold(posedge reference, negedge data, 2, notifier);
    $setuphold(posedge reference, posedge data, 1, 2, notifier,
               , , delayed_reference, delayed_data);
    $recovery(posedge reference, posedge data, 3, notifier);
    $removal(posedge reference, negedge data, 4, notifier);
    $recrem(posedge reference, posedge data, 3, 4, notifier,
            condition, ~condition, delayed_reference, delayed_data);
    $skew(posedge reference, posedge data, 5, notifier);
    $timeskew(posedge reference, posedge data, 6, notifier, 1'b1, 1'b0);
    $fullskew(posedge reference, posedge data, 7, 8, notifier, 1'b0, 1'b1);
    $period(posedge reference &&& condition, 9, notifier);
    $width(edge [01, 0x, x1] reference &&& condition, 10, 2, notifier);
    $nochange(posedge reference, negedge data, -1, 2, notifier);
  endspecify
endmodule

// CHECK-DAG: timing_check_kind = 1 : i32
// CHECK-DAG: timing_check_kind = 2 : i32
// CHECK-DAG: timing_check_kind = 3 : i32
// CHECK-DAG: timing_check_kind = 4 : i32
// CHECK-DAG: timing_check_kind = 5 : i32
// CHECK-DAG: timing_check_kind = 6 : i32
// CHECK-DAG: timing_check_kind = 7 : i32
// CHECK-DAG: timing_check_kind = 8 : i32
// CHECK-DAG: timing_check_kind = 9 : i32
// CHECK-DAG: timing_check_kind = 10 : i32
// CHECK-DAG: timing_check_kind = 11 : i32
// CHECK-DAG: timing_check_kind = 12 : i32
// CHECK-DAG: timing_check_arg_count = 9 : i64
// CHECK-DAG: timing_check_arg_has_expression = array<i64: 1, 1, 1, 1, 1, 1, 1, 1, 1>
// CHECK-DAG: timing_check_arg_has_expression = array<i64: 1, 1, 1, 1, 1, 0, 0, 1, 1>
// CHECK-DAG: timing_check_arg_has_condition = array<i64: 0, 0, 0, 0, 0, 0, 0, 0, 0>
// CHECK-DAG: timing_check_arg_has_condition = array<i64: 1, 0, 0, 0>
// CHECK-DAG: timing_check_arg_edges = [3 : i32, 0 : i32, 0 : i32, 0 : i32]
// CHECK-DAG: timing_check_arg_edge_descriptors = {{.*}}"01"{{.*}}"0x"{{.*}}"x1"
// CHECK-DAG: time_unit_fs = 1000000 : i64
// CHECK-DAG: time_precision_fs = 1000 : i64
// CHECK-DAG: obelisk.sv.expression.unary_op
// CHECK-DAG: referenced_path = "system_timing_check_semantic.condition"
