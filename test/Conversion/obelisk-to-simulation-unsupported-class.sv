// RUN: not obelisk -emit-sim %s 2>&1 | FileCheck %s

class unsupported_object;
  int field;
endclass

module unsupported_class_use;
  initial begin
  end
endmodule

// CHECK: unsupported semantic construct in the first simulation slice
