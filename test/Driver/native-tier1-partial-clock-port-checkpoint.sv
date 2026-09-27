// RUN: obelisk -O3 --native-scheduler=auto -emit-llvm %s -o %t.ll
// RUN: FileCheck %s --check-prefix=LLVM < %t.ll
// RUN: obelisk -O3 --native-scheduler=auto %s -o %t.auto
// RUN: obelisk -O3 --native-scheduler=generic %s -o %t.generic
// RUN: env OBELISK_RT_SIGNAL_DIAGNOSTICS=1 %t.auto > %t.auto.out 2> %t.trace
// RUN: FileCheck %s --check-prefix=TRACE < %t.trace
// RUN: %t.generic > %t.generic.out
// RUN: diff -u %t.generic.out %t.auto.out

module checkpoint_clock_source(output logic clk = 0);
  always #5 clk = ~clk;
endmodule

// The port alias publishes to both generated logic and a runtime testbench.
// Preserve that event's runtime waiter delivery (IEEE 1800-2023 9.4.2) while
// the independent datapath remains in the generated eval closure.
module native_tier1_partial_clock_port_checkpoint;
  logic clk;
  checkpoint_clock_source source(clk);
  logic [7:0] q[32];
  real fraction = 1.25;
  string message;
  for (genvar i = 0; i < 32; i++) begin : g
    initial q[i] = i;
    always @(posedge clk) q[i] <= q[i] + 1;
  end
  initial begin
    message = "clock port";
    repeat (16) @(posedge clk);
    $display("%f %s %0d", fraction, message, q[31]);
    $finish;
  end
endmodule

// LLVM: call i32 @obelisk_rt_v1_scheduler_execute_aot_actor
// LLVM: define {{.*}}i32 @__obelisk_eval_dispatch_v1
// TRACE: eval_dispatches={{[1-9][0-9]*}}
