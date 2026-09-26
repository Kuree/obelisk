// RUN: obelisk -O3 --native-scheduler=auto --mlir-timing %s -o %t.auto 2> %t.timing
// RUN: FileCheck %s --check-prefix=TIER < %t.timing
// RUN: obelisk -O3 --native-scheduler=generic %s -o %t.generic
// RUN: %t.auto > %t.auto.out
// RUN: %t.generic > %t.generic.out
// RUN: diff -u %t.generic.out %t.auto.out
// RUN: FileCheck %s < %t.auto.out

// Two posedges of the same clock in one time slot reenter one NBA site.
// The #0 steps make the clock updates and captured data values ordered.
module native_nba_ordered_edge_reentry;
  logic clk = 0;
  logic data = 0;
  logic q = 0;
  int edges = 0;

  always @(posedge q) edges = edges + 1;
  always @(posedge clk) q <= data;

  initial begin
    #1 data = 1; clk = 1;
    #0 clk = 0; data = 0;
    #0 clk = 1;
    #1;
    $display("q=%b edges=%0d", q, edges);
    $finish;
  end
endmodule

// TIER: native eligibility: eligible=1 {{.*}} cost_effective=1
// CHECK: q=0 edges=1
