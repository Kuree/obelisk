// RUN: talos -O3 --native-scheduler=auto --mlir-timing %s -o %t.auto 2> %t.timing
// RUN: FileCheck %s --check-prefix=TIER < %t.timing
// RUN: talos -O0 --native-scheduler=generic %s -o %t.reference
// RUN: not %t.auto > %t.auto.out 2>&1
// RUN: not %t.reference > %t.reference.out 2>&1
// RUN: diff -u %t.reference.out %t.auto.out
// RUN: FileCheck %s < %t.auto.out

// $assertoff/$asserton (IEEE 1800-2023 20.11) make every attempt of `chk`
// read the runtime assertion control state, and a failing attempt latches
// $error's final status (20.10). Neither schedules anything, so the generated
// eval body keeps both inline while the fork keeps one process on the lower
// tier. The attempt at 25 runs while `chk` is off and reports nothing.
module native_partial_assertion_control;
  logic clk = 0;
  logic [7:0] a = 0, b = 0, c = 0;
  logic [31:0] wide = 0;

  always #5 clk = ~clk;
  always_ff @(posedge clk) begin
    a <= a + 1;
    chk: assert (a != 8'd2 && a != 8'd4) else $error("a=%0d at %0t", a, $time);
  end
  always_ff @(posedge clk) b <= a;
  always_ff @(posedge clk) c <= b ^ a;
  always @(posedge clk) begin
    fork wide[15:8] <= wide[15:8] + 1; join_none
  end

  initial begin
    #12 $assertoff(0, native_partial_assertion_control.chk);
    #20 $asserton(0, native_partial_assertion_control.chk);
  end
  initial begin
    #100;
    $display("a=%0d b=%0d c=%0d w=%0d", a, b, c, wide[15:8]);
    $finish;
  end
endmodule

// TIER-NOT: eval disabled
// TIER-NOT: error
// CHECK-NOT: a=2 at
// CHECK: simulation time 45: a=4 at 45
// CHECK: a=10 b=9 c=1 w=10
