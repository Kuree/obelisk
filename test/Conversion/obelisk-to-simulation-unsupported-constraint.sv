// RUN: not obelisk -emit-sim %s 2>&1 | FileCheck %s

class constrained;
  rand int value;
  constraint bounds { value > 0; }
endclass

module unsupported_constraint;
endmodule

// CHECK: unsupported semantic construct in the first simulation slice
