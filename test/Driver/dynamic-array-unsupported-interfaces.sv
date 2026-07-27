// RUN: not obelisk -O0 --vpi=read %s -o %t.vpi 2>&1 | FileCheck %s --check-prefix=VPI

module dynamic_array_unsupported_interfaces;
  int array[];
  initial array = '{1};
endmodule

// VPI: VPI dynamic-array, queue, and associative-array marshalling is unsupported
