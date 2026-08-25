// RUN: obelisk -emit-obelisk %s -o - | FileCheck %s

`timescale 1ns/1ns

module conditional_path(
    input wire [1:0] source,
    input wire select_slow,
    input wire select_fast,
    output wire [1:0] destination);
  assign destination = source;
  specify
    if (select_slow & ~select_fast)
      (source *> destination) = (5, 6, 7);
    if (select_fast)
      (source +*> destination) = 2;
    ifnone
      (source *> destination) = 9;
  endspecify
endmodule

module specify_conditional_path;
  wire [1:0] source;
  wire select_slow;
  wire select_fast;
  wire [1:0] destination;
  conditional_path dut(source, select_slow, select_fast, destination);
endmodule

// Conditions are retained as ordinary semantic expression subtrees. ifnone
// has no synthetic predicate and is distinguished by frozen path metadata.
// CHECK: timing_condition
// CHECK: obelisk.sv.expression.binary
// CHECK: referenced_path = "specify_conditional_path.dut.select_slow"
// CHECK: referenced_path = "specify_conditional_path.dut.select_fast"
// CHECK: timing_condition
// CHECK: timing_polarity = 1 : i32
// CHECK: timing_ifnone
