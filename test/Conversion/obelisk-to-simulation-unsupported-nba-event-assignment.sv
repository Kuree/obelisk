// RUN: not obelisk -emit-sim %s 2>&1 | FileCheck %s

module unsupported_nba_event_assignment;
  logic clk;
  logic lhs;
  logic rhs;

  initial
    lhs <= @(posedge clk) rhs;
endmodule

// CHECK: unsupported semantic node in the first simulation slice
// CHECK-SAME: scheduler-owned deferred action
