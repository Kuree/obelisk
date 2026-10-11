// RUN: obelisk --clocked-kernels=immediate -O3 --vpi=off --mlir-timing -emit-llvm %s -o %t.ll 2> %t.timing
// RUN: FileCheck %s --check-prefix=LLVM < %t.ll
// RUN: FileCheck %s --check-prefix=COHORT < %t.timing
// RUN: obelisk --clocked-kernels=immediate -O3 --vpi=off %s -o %t.native
// RUN: obelisk --clocked-kernels=immediate -O0 --native-scheduler=generic %s -o %t.reference
// RUN: obelisk --clocked-kernels=immediate -O3 --execution-tier=bytecode %s -o %t.bytecode
// RUN: %t.native > %t.native.out
// RUN: %t.reference > %t.reference.out
// RUN: %t.bytecode > %t.bytecode.out
// RUN: diff -u %t.reference.out %t.native.out
// RUN: diff -u %t.reference.out %t.bytecode.out
// RUN: FileCheck %s < %t.native.out

// LRM 4.5-4.7, 9.2.2.4, 9.4.2, 10.4.2: preserve exact edge selection,
// NBA barriers and per-process statement order across multiple ready words.
// The async reset and separate clock must not acquire the main cohort's
// activations, including coincident physical edges and X/Z transitions.
module clock_edge_member #(parameter int K = 1);
  logic [7:0] q;
  logic [7:0] temporary;
  // Hierarchical captures deliberately share one physical edge across module
  // instances, beyond the existing per-instance source-process fusion.
  always_ff @(posedge native_clock_edge_cohort.clk) begin
    temporary = q + K;
    if (native_clock_edge_cohort.reset) q <= 0;
    else q <= temporary;
  end
  initial begin
    #40;
    if (q !== 8'(3 * K)) $fatal(1, "member %0d: %0d", K, q);
  end
endmodule

module native_clock_edge_cohort;
  bit clk = 0, other_clk = 0, reset = 1;
  logic four_clk = 0;
  logic [7:0] q[130];
  int other = 0, async_q = 0, four_rises[2] = '{default: 0};
  int four_falls = 0;
  always #5 clk = ~clk;
  always #3 other_clk = ~other_clk;
  for (genvar k = 0; k < 130; k++) begin
    clock_edge_member #(k + 1) member();
  end
  for (genvar k = 0; k < 130; k++) begin
    always_ff @(posedge clk)
      if (reset) q[k] <= 0;
      else q[k] <= q[k] + k + 1;
  end
  always_ff @(posedge other_clk)
    if (reset) other <= 0;
    else other <= other + 1;
  always_ff @(posedge clk or posedge reset)
    if (reset) async_q <= 0;
    else async_q <= async_q + 1;
  for (genvar k = 0; k < 2; k++)
    always_ff @(posedge four_clk) four_rises[k] <= four_rises[k] + 1;
  always_ff @(negedge four_clk) four_falls <= four_falls + 1;
  initial begin
    #7; reset = 0;
    #1; four_clk = 'x;
    #1; four_clk = 'z;
    #1; four_clk = 1;
    #1; four_clk = 0;
    #30;
    for (int i = 0; i < 130; i++)
      if (q[i] !== 8'(3 * (i + 1))) $fatal(1, "lane %0d: %0d", i, q[i]);
    $display("clock cohort: other=%0d async=%0d rises=%0d,%0d falls=%0d",
             other, async_q, four_rises[0], four_rises[1], four_falls);
    $finish;
  end
endmodule

// LLVM: define {{.*}}void @__obelisk_eval_ranked_group_
// COHORT: obelisk ranked groups: {{.*}} clocked_members=132
// CHECK: clock cohort: other=6 async=3 rises=2,2 falls=1
