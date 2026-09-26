// RUN: obelisk -O3 --native-scheduler=auto --mlir-timing %s -o %t.auto 2> %t.timing
// RUN: FileCheck %s --check-prefix=TIER < %t.timing
// RUN: obelisk -O3 --native-scheduler=generic %s -o %t.generic
// RUN: %t.auto > %t.auto.out
// RUN: %t.generic > %t.generic.out
// RUN: diff -u %t.generic.out %t.auto.out
// RUN: FileCheck %s < %t.auto.out

// NBAs to different variables are still performed in execution order
// (LRM 4.6(b)), and the event expression observes each update (9.4.2). On
// posedge, b is updated before a, so a & ~b never rises; on negedge, a falls
// first. Performing the updates in any other order makes edges nonzero.
module native_nba_ordered_cross_root;
  logic clk = 0;
  logic a = 0;
  logic b = 0;
  int edges = 0;
  int n = 0;

  always #5 clk = ~clk;
  always @(posedge clk) begin
    b <= 1;
    a <= 1;
    n <= n + 1;
    if (n == 100) begin
      #1 $display("a=%0d b=%0d edges=%0d", a, b, edges);
      $finish;
    end
  end
  always @(negedge clk) begin
    a <= 0;
    b <= 0;
  end
  always @(posedge (a & ~b)) edges = edges + 1;
endmodule

// TIER: native eligibility: eligible=1 {{.*}} cost_effective=1
// CHECK: a=1 b=1 edges=0
