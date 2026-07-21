// RUN: not obelisk -emit-sim %s 2>&1 | FileCheck %s

module unsupported_power;
  bit [7:0] base;
  bit [7:0] result;
  always_comb result = base ** 2;
endmodule

// CHECK: unsupported semantic node in the first simulation slice
// CHECK-SAME: binary operator
