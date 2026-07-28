// RUN: obelisk -O0 --native-scheduler=aot -emit-llvm %s -o %t.aot.ll
// RUN: FileCheck %s --check-prefix=AOT < %t.aot.ll
// RUN: obelisk -O0 --native-scheduler=generic -emit-llvm %s -o %t.generic.ll
// RUN: FileCheck %s --check-prefix=GENERIC < %t.generic.ll
// RUN: obelisk -O0 --vpi=full --native-scheduler=aot -emit-llvm %s \
// RUN:   -o %t.vpi.ll
// RUN: FileCheck %s --check-prefix=AOT < %t.vpi.ll
// RUN: obelisk --native-scheduler=aot %s -o %t.native
// RUN: obelisk --native-scheduler=aot --execution-tier=bytecode %s \
// RUN:   -o %t.bytecode
// RUN: %t.native > %t.native.out
// RUN: %t.bytecode > %t.bytecode.out
// RUN: diff -u %t.native.out %t.bytecode.out

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
// AOT-DAG: call i32 @obelisk_rt_v1_scheduler_install_aot
// AOT-DAG: call i32 @obelisk_rt_v1_scheduler_add_aot
// AOT-DAG: call i32 @obelisk_rt_v1_scheduler_run_aot
// GENERIC-NOT: @__obelisk_aot_schedule_plan_v1
// GENERIC-NOT: call i32 @obelisk_rt_v1_scheduler_install_aot
// GENERIC: call i32 @obelisk_rt_v1_scheduler_add_planned
// GENERIC: call i32 @obelisk_rt_v1_scheduler_run
