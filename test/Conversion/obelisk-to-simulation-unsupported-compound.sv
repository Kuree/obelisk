// RUN: not obelisk -emit-sim %s 2>&1 | FileCheck %s

module unsupported_compound;
  logic [7:0] value;
  initial value += 1;
endmodule

// CHECK: unsupported semantic node in the first simulation slice
// CHECK-SAME: compound assignment
