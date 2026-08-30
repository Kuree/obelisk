// RUN: not obelisk -fno-lto -O0 %s -o %t 2>&1 | FileCheck %s

module system_timing_check_event_expression_invalid(
    input wire data, reference, a, b);
  specify
    // IEEE 1800-2017 31.7 directs multiple conditioners to be combined in a
    // separate signal outside the specify block. Retain a targeted diagnostic
    // instead of installing a runtime expression interpreter.
    $setup(posedge data, posedge reference &&& ((a ^ b) !== 0), 1);
  endspecify
endmodule

// CHECK: error: IEEE 1800-2017 31.7 timing-check condition must be one direct packed signal
// CHECK-SAME: combine multiple conditioning signals outside the specify block
