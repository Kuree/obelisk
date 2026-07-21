// RUN: not obelisk -emit-sim %s 2>&1 | FileCheck %s

module unsupported_event_iff;
  logic clk;
  logic enable;
  logic value;
  always @(posedge clk iff enable)
    value = enable;
endmodule

// CHECK: unsupported semantic node in the first simulation slice
// CHECK-SAME: event iff condition
