// RUN: not obelisk -emit-sim %s 2>&1 | FileCheck %s

module unsupported_wait_order;
  event first;
  event second;

  initial
    wait_order (first, second);
endmodule

// CHECK: unsupported semantic node in the first simulation slice
// CHECK-SAME: wait_order occurrence sequencing
