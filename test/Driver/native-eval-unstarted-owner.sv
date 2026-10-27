// RUN: talos -O3 --vpi=off --mlir-timing %s -o %t.eval 2> %t.timing
// RUN: FileCheck %s --check-prefix=TIER < %t.timing
// RUN: talos -O0 --native-scheduler=generic %s -o %t.reference
// RUN: %t.reference > %t.reference.out 2>&1
// RUN: %t.eval > %t.eval.out 2>&1 && diff -u %t.reference.out %t.eval.out
// RUN: FileCheck %s --implicit-check-not=violation < %t.reference.out

// Every process is native, so the eval coordinator runs eagerly whenever an
// owner's ingress is pending. The `always @(x)` owner starts waiting before
// the initial block stores the wide `x` through the runtime, which wakes it.
// The coordinator's generated fanout then reaches the always_comb owner, but
// that owner has not taken its first activation yet (IEEE 1800-2023 9.2.2.2),
// so it is not waiting on anything (9.4.2). Its time-zero activation must
// precede the coordinator (a legal Active order, 4.7); executing it inside the
// coordinator found `z` still X and queued a checkpoint for an actor that had
// never started. Pending violations are flushed when `z <= 0` re-triggers the
// owner (12.4.2.1), and `x` changes at #3 so `r` does not depend on the
// time-zero race.
module native_eval_unstarted_owner;
  logic clk = 1'b0;
  logic [127:0] x;
  logic [1:0] s, z;
  logic [3:0] y, r;
  always @(x)
    unique case (x[127])
      1'b0: s = 2'd1;
      1'b1: s = 2'd2;
    endcase
  initial begin
    x = {128{1'b1}};
    #3 x = {128{1'b0}};
  end
  initial z <= 2'd0;
  always_comb
    unique case (s ^ z)
      2'd0: y = 4'd1;
      2'd1: y = 4'd2;
      2'd2: y = 4'd4;
      2'd3: y = 4'd8;
    endcase
  always #5 clk = ~clk;
  always_ff @(posedge clk) r <= y;
  initial begin
    #32;
    $display("%0t r=%0d", $time, r);
    $finish;
  end
endmodule

// TIER: talos native eligibility: eligible=1 fully_eligible=1
// TIER: talos eval owner: bit={{[0-9]+}} actor={{[0-9]+}} {{.*}}executor=__talos_direct_fragment_
// CHECK: 32 r=2
