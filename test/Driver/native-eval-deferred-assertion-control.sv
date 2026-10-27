// RUN: talos -O3 --vpi=off --mlir-timing %s -o %t.eval 2> %t.timing
// RUN: FileCheck %s --check-prefix=TIER < %t.timing
// RUN: talos -O0 --native-scheduler=generic %s -o %t.reference
// RUN: not %t.reference > %t.reference.out 2>&1
// RUN: not %t.eval > %t.eval.out 2>&1
// RUN: diff -u %t.reference.out %t.eval.out
// RUN: FileCheck %s < %t.eval.out

// A deferred assertion's report is a cold leaf of the always_comb, so its
// generated body needs a path probe. With $assertoff/$asserton in the design
// every attempt reads the assertion control state (IEEE 1800-2023 20.11); the
// probe reads it like any other value instead of disabling generated eval
// for the whole design. The failure at 5 falls inside the off window.
module native_eval_deferred_assertion_control;
  logic clk = 1'b0;
  always #5 clk = ~clk;
  logic [1:0] ptr = 2'd0, n;
  logic add = 1'b0;
  assign n = add ? (ptr == 2'd2 ? 2'd0 : ptr + 2'd1) : ptr;
  always_ff @(posedge clk) begin
    ptr <= n;
    add <= ~add;
  end
  always_comb
    chk: assert final (n != 2'd1) else $error("n=1 at %0t", $time);
  initial begin
    #3 $assertoff(0, native_eval_deferred_assertion_control.chk);
    #10 $asserton(0, native_eval_deferred_assertion_control.chk);
  end
  initial begin
    repeat (8) @(negedge clk) $display("%0t ptr=%0d n=%0d", $time, ptr, n);
    $finish;
  end
endmodule

// TIER-NOT: eval disabled
// CHECK-NOT: n=1 at 5
// CHECK: n=1 at 65
// CHECK: 80 ptr=1 n=1
