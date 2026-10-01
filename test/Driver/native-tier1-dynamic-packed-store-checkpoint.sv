// RUN: obelisk -O3 --native-scheduler=auto -emit-llvm %s -o %t.ll
// RUN: FileCheck %s --check-prefix=LLVM < %t.ll
// RUN: FileCheck %s --check-prefix=NO-CHECKPOINT < %t.ll
// RUN: obelisk -O3 --native-scheduler=auto %s -o %t.auto
// RUN: obelisk -O3 --native-scheduler=generic %s -o %t.generic
// RUN: %t.auto > %t.auto.out
// RUN: %t.generic > %t.generic.out
// RUN: diff -u %t.generic.out %t.auto.out
// RUN: FileCheck %s < %t.auto.out

// Generated indexed stores retain complete source-order transitions and
// bounded selection semantics (IEEE 1800-2023 4.6(a), 9.4.2, 11.5.1).
module native_tier1_dynamic_packed_store_checkpoint;
  logic clk = 0;
  int lane = 0;
  logic [65:0] data = 1;
  logic [2:0][65:0] result;
  always #5 clk = ~clk;
  always @(posedge clk) begin
    lane <= (lane + 1) % 3;
    data <= data + 1;
  end
  always_comb begin
    result = '0;
    result[lane] = data;
  end
  initial begin
    #36;
    $display("%0d %0d %0d", result[0], result[1], result[2]);
    $finish;
  end
endmodule

// LLVM: define {{.*}}i32 @__obelisk_eval_dispatch_v1
// LLVM: call i32 @obelisk_rt_v1_native_state_bind_shared
// NO-CHECKPOINT-NOT: call i32 @obelisk_rt_v1_scheduler_execute_aot_actor
// CHECK: 0 5 0
