// RUN: not obelisk -emit-sim %s 2>&1 | FileCheck %s

module unsupported_system_call;
  logic [7:0] value;
  initial $display("value=%0d", value);
endmodule

// CHECK: unsupported semantic node in the first simulation slice
// CHECK-SAME: indirect or system call
