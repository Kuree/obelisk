// RUN: talos --clocked-kernels=immediate -O0 --native-scheduler=aot -emit-llvm %s -o - | FileCheck %s --check-prefix=AOT
// RUN: talos --clocked-kernels=immediate -O0 --native-scheduler=generic -emit-llvm %s -o - | FileCheck %s --check-prefix=GENERIC
// RUN: talos --clocked-kernels=immediate -O2 --native-scheduler=aot --static-specialization=off -emit-llvm %s -o - | FileCheck %s --check-prefix=SPECIALIZATION-OFF
// RUN: talos --clocked-kernels=immediate -O0 --native-scheduler=aot --static-specialization=on -emit-llvm %s -o - | FileCheck %s --check-prefix=SPECIALIZATION-ON
// RUN: talos --clocked-kernels=immediate -O0 --vpi=full --native-scheduler=aot -emit-llvm %s -o - | FileCheck %s --check-prefix=AOT
// RUN: talos --clocked-kernels=immediate -O0 --native-scheduler=eval -emit-llvm %s -o - | FileCheck %s --check-prefix=EVAL
// RUN: not talos --clocked-kernels=immediate -O0 --native-scheduler=eval --static-specialization=off -emit-llvm %s -o %t.eval-off.ll 2>&1 | FileCheck %s --check-prefix=EVAL-DIAG

module native_aot_scheduler;
  bit clock;
  int value;

  always @(posedge clock)
    value <= value + 1;

  initial begin
    #1 clock = 1;
    #1;
    $display("value=%0d", value);
    $finish;
  end
endmodule

// AOT: @__talos_aot_schedule_plan_v1
// AOT: @__talos_aot_schedule_nodes_v1
// AOT-DAG: call i32 @talos_rt_v1_scheduler_install_aot
// AOT-DAG: call i64 @talos_rt_v1_process_spawn
// AOT-DAG: call i32 @talos_rt_v1_scheduler_run_aot_nodes
// AOT-DAG: call i32 @talos_rt_v1_scheduler_run_aot
// GENERIC-NOT: @__talos_aot_schedule_plan_v1
// GENERIC-NOT: call i32 @talos_rt_v1_scheduler_install_aot
// GENERIC: call i64 @talos_rt_v1_process_spawn
// GENERIC: call i32 @talos_rt_v1_scheduler_run
// SPECIALIZATION-OFF-NOT: @__talos_aot_nba_roots_v1
// SPECIALIZATION-OFF: call i32 @talos_rt_v1_scheduler_nba
// SPECIALIZATION-ON: @__talos_aot_nba_roots_v1
// SPECIALIZATION-ON: @__talos_aot_nba_dirty_roots_v1
// SPECIALIZATION-ON: @__talos_aot_nba_dirty_summary_v1
// SPECIALIZATION-ON-NOT: call i32 @talos_rt_v1_static_nba_claim
// Forced eval resolves automatic specialization to the required proof passes,
// independently of O0 code generation. Explicitly disabling those passes still
// rejects missing ownership instead of silently weakening the eval contract.
// EVAL: define {{.*}}@__talos_eval_dispatch_v1(
// EVAL-DIAG: error: eval exact owner miss:
