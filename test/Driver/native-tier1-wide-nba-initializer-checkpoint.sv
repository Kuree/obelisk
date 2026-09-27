// RUN: obelisk -O3 --native-scheduler=auto -emit-llvm %s -o %t.ll
// RUN: FileCheck %s --check-prefix=LLVM < %t.ll
// RUN: obelisk -O3 --native-scheduler=auto %s -o %t.auto
// RUN: obelisk -O3 --native-scheduler=generic %s -o %t.generic
// RUN: %t.auto > %t.auto.out
// RUN: %t.generic > %t.generic.out
// RUN: diff -u %t.generic.out %t.auto.out
// RUN: FileCheck %s < %t.auto.out

// A wide merge-safe array still needs ordered ownership when initialization
// and a clocked loop both enqueue updates (IEEE 1800-2023 4.6(b), 10.4.2).
// This is the multiplier pipeline shape that previously passed admission
// only to disable the entire generated eval plan after LLVM lowering.
module native_tier1_wide_nba_initializer_checkpoint;
  logic clk = 0;
  logic [65:0] pipeReg [3];
  logic [65:0] result = 1;
  always #5 clk = ~clk;
  always @(posedge clk) begin
    result <= result + 1;
    pipeReg[0] <= result;
    for (int i = 1; i < 3; i++)
      pipeReg[i] <= pipeReg[i-1];
  end
  initial begin
    for (int i = 0; i < 3; i++)
      pipeReg[i] <= '0;
    #36;
    $display("pipeline %0d %0d %0d",
             pipeReg[0], pipeReg[1], pipeReg[2]);
    $finish;
  end
endmodule

// LLVM: call i32 @obelisk_rt_v1_scheduler_execute_aot_actor
// LLVM: define {{.*}}i32 @__obelisk_eval_dispatch_v1
// LLVM: call i32 @obelisk_rt_v1_native_state_bind_shared
// CHECK: pipeline 4 3 2
