// RUN: %talos --std=1800-2023 -emit-talos %s | talos-opt '--lower-talos-to-sim=opt-level=0' | FileCheck %s --check-prefix=PLAN
// REQUIRES: compiled-sampler

// Hundreds of overlapping prefix views have a compact legal relation: value=0.
// Dependency and component-table metadata have a linear work/output budget;
// skipping optional proposals must retain the complete parameterized plan.
// PLAN: simulation.random.solve_wide
// PLAN-SAME: SAMP
// PLAN-SAME: PLAN

`define PREFIX(n) value[n:0] == (n+1)'(0);
`define PREFIX10(n) `PREFIX(n``0) `PREFIX(n``1) `PREFIX(n``2) `PREFIX(n``3) \
  `PREFIX(n``4) `PREFIX(n``5) `PREFIX(n``6) `PREFIX(n``7) `PREFIX(n``8) \
  `PREFIX(n``9)
`define PREFIX100(n) `PREFIX10(n``0) `PREFIX10(n``1) `PREFIX10(n``2) \
  `PREFIX10(n``3) `PREFIX10(n``4) `PREFIX10(n``5) `PREFIX10(n``6) \
  `PREFIX10(n``7) `PREFIX10(n``8) `PREFIX10(n``9)

class C;
  rand bit [383:0] value;
  constraint prefixes {
    `PREFIX100(0) `PREFIX100(1) `PREFIX100(2)
    `PREFIX10(30) `PREFIX10(31) `PREFIX10(32) `PREFIX10(33) `PREFIX10(34)
    `PREFIX10(35) `PREFIX10(36) `PREFIX10(37)
    `PREFIX(380) `PREFIX(381) `PREFIX(382) `PREFIX(383)
  }
endclass

module top;
  C obj = new;
  initial begin
    void'(obj.randomize());
    $finish;
  end
endmodule
