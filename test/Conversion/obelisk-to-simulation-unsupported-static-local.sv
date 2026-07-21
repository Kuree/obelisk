// RUN: not obelisk -emit-sim %s 2>&1 | FileCheck %s

module unsupported_static_local;
  initial begin
    static logic value = 1'b1;
  end
endmodule

// CHECK: unsupported semantic node in the first simulation slice
// CHECK-SAME: static local initializer
