// RUN: not obelisk -emit-sim %s 2>&1 | FileCheck %s

module unsupported_fork;
  initial begin
    fork
      #1;
      #2;
    join_any
  end
endmodule

// CHECK: unsupported semantic node in the first simulation slice
// CHECK-SAME: fork/join branch outlining and child-process synchronization
