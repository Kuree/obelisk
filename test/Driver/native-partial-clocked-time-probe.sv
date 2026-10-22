// RUN: obelisk -O3 --native-scheduler=auto --mlir-timing %s -o %t.auto 2> %t.timing
// RUN: FileCheck %s --check-prefix=TIER < %t.timing
// RUN: obelisk -O0 --native-scheduler=generic %s -o %t.reference
// RUN: not %t.auto > %t.auto.out 2>&1
// RUN: not %t.reference > %t.reference.out 2>&1
// RUN: diff -u %t.reference.out %t.auto.out
// RUN: FileCheck %s < %t.auto.out

// The clocked kernel reads $time (IEEE 1800-2023 20.3.1) on its hot path to
// decide whether to take the cold $error leaf. The path-known probe that
// guards that leaf evaluates the read as is: time does not advance within one
// activation. A rejected probe would leave the leaf unguarded, and this
// partial design (the fork keeps one process on the lower tier) could no
// longer fall back once packed lowering staged its NBAs.
module native_partial_clocked_time_probe;
  logic clk = 0;
  logic [7:0] a = 0, b = 0, c = 0;
  logic [31:0] wide = 0;

  always #5 clk = ~clk;
  always_ff @(posedge clk) begin
    a <= a + 1;
    if (a == 8'd3 && $time > 20) $error("a=3 at %0t", $time);
  end
  always_ff @(posedge clk) b <= a;
  always_ff @(posedge clk) c <= b ^ a;
  always @(posedge clk) begin
    fork wide[15:8] <= wide[15:8] + 1; join_none
  end

  initial begin
    #100;
    $display("a=%0d b=%0d c=%0d w=%0d", a, b, c, wide[15:8]);
    $finish;
  end
endmodule

// TIER-NOT: eval probe rejected
// TIER-NOT: unguarded runtime leaf
// TIER: obelisk eval executor inventory: {{.*}} runtime_checkpoints=1
// CHECK: simulation time 35: a=3 at 35
// CHECK: a=10 b=9 c=1 w=10
