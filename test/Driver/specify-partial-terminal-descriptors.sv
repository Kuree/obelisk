// RUN: obelisk -emit-slang %s -o - | FileCheck %s

module partial_terminal_descriptors(input wire [0:3] source,
                                    output wire [3:0] destination);
  assign destination = {source[2:3], source[0:1]};
  specify
    (source[0:1] => destination[1:0]) = (1, 2, 3);
    (source[2:3] *> destination[3:2]) = 4;
  endspecify
endmodule

// The ascending source declaration gives source[0:1] normalized physical
// offsets 2..3, while destination[1:0] occupies offsets 0..1. Lowering maps
// those two selections positionally instead of comparing numeric indices.
// CHECK: timing_connection_full = false
// CHECK: timing_input_terminals = [
// CHECK-SAME: low = 2 : i64
// CHECK-SAME: root_width = 4 : i64
// CHECK-SAME: width = 2 : i64
// CHECK: timing_output_terminal = {
// CHECK-SAME: low = 0 : i64
// CHECK-SAME: root_width = 4 : i64
// CHECK-SAME: width = 2 : i64
// CHECK: timing_connection_full = true
