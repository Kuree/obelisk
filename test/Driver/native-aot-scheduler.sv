// RUN: obelisk -O0 --native-scheduler=aot -emit-llvm %s -o %t.aot.ll
// RUN: FileCheck %s --check-prefix=AOT < %t.aot.ll
// RUN: obelisk -O0 --native-scheduler=generic -emit-llvm %s -o %t.generic.ll
// RUN: FileCheck %s --check-prefix=GENERIC < %t.generic.ll
// RUN: obelisk -O2 --native-scheduler=aot --static-specialization=off \
// RUN:   -emit-llvm %s -o %t.specialization-off.ll
// RUN: FileCheck %s --check-prefix=SPECIALIZATION-OFF \
// RUN:   < %t.specialization-off.ll
// RUN: obelisk -O0 --native-scheduler=aot --static-specialization=on \
// RUN:   -emit-llvm %s -o %t.specialization-on.ll
// RUN: FileCheck %s --check-prefix=SPECIALIZATION-ON \
// RUN:   < %t.specialization-on.ll
// RUN: obelisk -O0 --vpi=full --native-scheduler=aot -emit-llvm %s \
// RUN:   -o %t.vpi.ll
// RUN: FileCheck %s --check-prefix=AOT < %t.vpi.ll
// RUN: obelisk -fno-lto --native-scheduler=aot %s -o %t.native
// RUN: obelisk -fno-lto --native-scheduler=aot --execution-tier=bytecode %s \
// RUN:   -o %t.bytecode
// RUN: %t.native > %t.native.out
// RUN: %t.bytecode > %t.bytecode.out
// RUN: diff -u %t.native.out %t.bytecode.out
// RUN: not obelisk -O0 --native-scheduler=eval -emit-llvm %s -o %t.eval.ll \
// RUN:   2>&1 | FileCheck %s --check-prefix=EVAL-DIAG

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

// AOT: @__obelisk_aot_schedule_plan_v1
// AOT: @__obelisk_aot_schedule_nodes_v1
// AOT-DAG: call i32 @obelisk_rt_v1_scheduler_install_aot
// AOT-DAG: call i32 @obelisk_rt_v1_scheduler_add_aot
// AOT-DAG: call i32 @obelisk_rt_v1_scheduler_run_aot_nodes
// AOT-DAG: call i32 @obelisk_rt_v1_scheduler_run_aot
// GENERIC-NOT: @__obelisk_aot_schedule_plan_v1
// GENERIC-NOT: call i32 @obelisk_rt_v1_scheduler_install_aot
// GENERIC: call i32 @obelisk_rt_v1_scheduler_add_planned
// GENERIC: call i32 @obelisk_rt_v1_scheduler_run
// SPECIALIZATION-OFF-NOT: @__obelisk_aot_nba_roots_v1
// SPECIALIZATION-OFF: call i32 @obelisk_rt_v1_scheduler_nba
// SPECIALIZATION-ON: @__obelisk_aot_nba_roots_v1
// SPECIALIZATION-ON: @__obelisk_aot_nba_dirty_roots_v1
// SPECIALIZATION-ON: @__obelisk_aot_nba_dirty_summary_v1
// SPECIALIZATION-ON-NOT: call i32 @obelisk_rt_v1_static_nba_claim
// EVAL-DIAG: cannot materialize generated eval loop: clocks=0
