// RUN: not obelisk -fno-lto -O0 --vpi=read %s -o %t.vpi 2>&1 | FileCheck %s

module associative_array_vpi_unsupported;
  int array[string];
  initial array["key"] = 1;
endmodule

// CHECK: VPI dynamic-array, queue, and associative-array marshalling is unsupported
