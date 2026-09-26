// RUN: obelisk -O0 %s -o %t
// RUN: not %t 2>&1 | FileCheck %s

module top;
  initial $exit;
endmodule

// CHECK: $exit is only valid in a thread owned by a program instance
