// RUN: %split-file %s %t
// RUN: %talos -emit-talos --std=1800-2023 %t/input.sv -o %t/input.mlir
// RUN: not talos-opt %t/input.mlir \
// RUN:   '--lower-talos-to-sim=opt-level=0' 2>&1 \
// RUN:   | FileCheck %s

// An exclusion-bearing coverpoint must still satisfy the base transition-bin
// restrictions. In particular, a transition array with goto repetition cannot
// bypass validation merely because its sibling is ignore_bins.

// CHECK: multiple transition bins cannot contain goto or nonconsecutive repetition because these produce unbounded or varying-length sequences
// CHECK-NOT: transition ignore_bins and illegal_bins currently require

//--- input.sv
module top;
  bit [2:0] sampled;
  covergroup cg;
    cp: coverpoint sampled {
      bins unsupported[] = (1 => 2 [-> 2] => 3);
      ignore_bins ignored = (2 => 3);
    }
  endgroup
  cg cov;
  initial cov = new;
endmodule
