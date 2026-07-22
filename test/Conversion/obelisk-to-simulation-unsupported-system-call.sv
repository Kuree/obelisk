// RUN: not obelisk -emit-sim %s 2>&1 | FileCheck %s

module unsupported_system_call;
  integer value;
  initial value = $fscanf(value, "%0d", value);
endmodule

// CHECK: unsupported semantic node in the first simulation slice
// CHECK-SAME: unsupported system call $fscanf
