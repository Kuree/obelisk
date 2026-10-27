// RUN: talos -O3 --vpi=off --native-scheduler=eval %s -o %t
// RUN: not %t > %t.out 2> %t.err
// RUN: FileCheck %s < %t.err
// RUN: FileCheck %s --check-prefix=OUT < %t.out

// IEEE 1800-2023 20.10: $error reports a run-time error and the simulation
// continues. The generated eval coordinator must keep running after the
// first error instead of treating the latched exit status as a stop.

// CHECK: simulation time 10: value 1 too large
// CHECK-NEXT: simulation time 20: value 1 too large
// CHECK-NEXT: simulation time 30: value 1 too large
// CHECK-NEXT: simulation time 40: value 1 too large
// CHECK-NEXT: $finish: {{.*}} simulation time 45
// OUT: done at 45
module native_eval_error_continues;
  bit clk;
  logic [3:0] count = 0;
  always #5 clk = ~clk;
  always @(posedge clk) count <= count + 1;
  always_ff @(negedge clk)
    if (count != 0)
      assert (count < 1) else $error("value %0d too large", count > 0);
  initial begin
    #45 $display("done at %0t", $time);
    $finish;
  end
endmodule
