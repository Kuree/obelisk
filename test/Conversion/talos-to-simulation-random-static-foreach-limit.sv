// RUN: %talos --std=1800-2023 -emit-talos %s | not talos-opt --lower-talos-to-sim=opt-level=0 -o /dev/null 2>&1 | FileCheck %s

// A static foreach constraint is expanded into one constraint instance per
// element of the exact sampling plan (IEEE 1800-2023 18.5.7.1). Plan
// construction and counting grow faster than linearly in the instances, so
// expansions beyond the supported size are diagnosed.
class C;
  rand bit [7:0] mem[257];
  // CHECK: [[@LINE+1]]:{{[0-9]+}}: error: static foreach constraint expands to 257 instances; exact sampling plans support at most 256
  constraint c { foreach (mem[i]) mem[i] != 0; }
endclass

module top;
  C c = new;
  initial void'(c.randomize());
endmodule
