// RUN: obelisk -O0 --native-scheduler=generic %s -o %t.reference
// RUN: obelisk -O3 --native-scheduler=auto --mlir-timing %s -o %t.auto 2> %t.timing
// RUN: FileCheck %s --check-prefix=TIER < %t.timing
// RUN: %t.reference > %t.reference.out
// RUN: %t.auto > %t.auto.out
// RUN: diff -u %t.reference.out %t.auto.out
// RUN: FileCheck %s < %t.auto.out

// One NBA statement per activation does not bound the NBAs per time slot:
// #0 steps pulse the clock twice in one slot, so `q` goes 0 -> 1 -> 0 before
// the NBA region ends. The change waiter must still wake once, so the root
// keeps its transient mask; only a clock whose writer toggles it once per
// positive delay proves one activation per slot.
module native_nba_change_watch_reentry;
  logic clk = 0;
  logic data = 0;
  logic q = 0;
  int changes = 0;

  always @(q) changes = changes + 1;
  always @(posedge clk) q <= data;

  initial begin
    #1 data = 1; clk = 1;
    #0 clk = 0; data = 0;
    #0 clk = 1;
    #1;
    $display("q=%b changes=%0d", q, changes);
    $finish;
  end
endmodule

// TIER: native eligibility: eligible=1 {{.*}} cost_effective=1
// CHECK: q=0 changes=1
