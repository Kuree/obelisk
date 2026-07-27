// RUN: not obelisk -emit-sim %s 2>&1 | FileCheck %s

module unsupported_sampled(input logic value);
  initial assert ($sampled(value));
endmodule

// CHECK: $sampled requires concurrent assertion Preponed sampling, which is not executable yet
