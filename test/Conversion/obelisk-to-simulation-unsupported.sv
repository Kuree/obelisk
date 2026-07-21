// RUN: not obelisk -emit-sim %s 2>&1 | FileCheck %s

module unsupported_assertion(input logic enable);
  assert property (enable);
endmodule

// CHECK: unsupported semantic construct in the first simulation slice
// CHECK-SAME: assertion
