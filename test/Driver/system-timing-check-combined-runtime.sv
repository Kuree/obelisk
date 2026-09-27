// RUN: obelisk -emit-sim %s -o - | FileCheck %s --check-prefix=SIM
// RUN: obelisk -O0 %s -o %t.o0.native
// RUN: %t.o0.native 2>&1 | FileCheck %s
// RUN: obelisk -O0 --execution-tier=bytecode %s -o %t.o0.bytecode
// RUN: %t.o0.bytecode 2>&1 | FileCheck %s
// RUN: obelisk -O3 %s -o %t.o3.native
// RUN: %t.o3.native 2>&1 | FileCheck %s
// RUN: obelisk -O3 --native-scheduler=aot --top=combined_timing_checks \
// RUN:   -emit-llvm %s -o - | FileCheck %s --check-prefix=AOT
// RUN: obelisk -O3 --execution-tier=bytecode %s -o %t.o3.bytecode
// RUN: %t.o3.bytecode 2>&1 | FileCheck %s

`timescale 1ns / 1ps

module combined_timing_checks(
    input wire setuphold_reference, setuphold_data,
    input wire recrem_reference, recrem_data,
    input wire zero_reference, zero_data,
    input wire bare_reference, bare_data,
    output reg setuphold_notifier = 0,
    output reg recrem_notifier = 0,
    output reg zero_notifier = 0);
  specify
    $setuphold(posedge setuphold_reference, posedge setuphold_data,
               3, 5, setuphold_notifier);
    $recrem(posedge recrem_reference, posedge recrem_data,
            3, 5, recrem_notifier);
    $setuphold(posedge zero_reference, posedge zero_data,
               0, 0, zero_notifier);
    $recrem(posedge bare_reference, posedge bare_data, 3, 3);
  endspecify
endmodule

module system_timing_check_combined_runtime;
  logic sh_reference = 0, sh_data = 0;
  logic rr_reference = 0, rr_data = 0;
  logic zero_reference = 0, zero_data = 0;
  logic bare_reference = 0, bare_data = 0;
  wire sh_notifier, rr_notifier, zero_notifier;

  combined_timing_checks dut(
      sh_reference, sh_data, rr_reference, rr_data,
      zero_reference, zero_data, bare_reference, bare_data,
      sh_notifier, rr_notifier, zero_notifier);

  initial begin
    #1 sh_data = 1;
    #2 sh_reference = 1;
    #0.001 $display("setuphold-setup-violation %b", sh_notifier);

    sh_data = 0;
    sh_reference = 0;
    #6 sh_data = 1;
    #3 sh_reference = 1;
    #0.001 $display("setuphold-setup-endpoint %b", sh_notifier);

    sh_data = 0;
    sh_reference = 0;
    #6 sh_reference = 1;
    #2 sh_data = 1;
    #0.001 $display("setuphold-hold-violation %b", sh_notifier);

    sh_data = 0;
    sh_reference = 0;
    #6 sh_reference = 1;
    #5 sh_data = 1;
    #0.001 $display("setuphold-hold-endpoint %b", sh_notifier);

    sh_data = 0;
    sh_reference = 0;
    #6;
    sh_reference = 1;
    sh_data = 1;
    #0.001 $display("setuphold-simultaneous-once %b", sh_notifier);

    #1 rr_reference = 1;
    #2 rr_data = 1;
    #0.001 $display("recrem-recovery-violation %b", rr_notifier);

    rr_reference = 0;
    rr_data = 0;
    #6 rr_reference = 1;
    #3 rr_data = 1;
    #0.001 $display("recrem-recovery-endpoint %b", rr_notifier);

    rr_reference = 0;
    rr_data = 0;
    #6 rr_data = 1;
    #2 rr_reference = 1;
    #0.001 $display("recrem-removal-violation %b", rr_notifier);

    rr_reference = 0;
    rr_data = 0;
    #6 rr_data = 1;
    #5 rr_reference = 1;
    #0.001 $display("recrem-removal-endpoint %b", rr_notifier);

    rr_reference = 0;
    rr_data = 0;
    #6;
    rr_reference = 1;
    rr_data = 1;
    #0.001 $display("recrem-simultaneous-once %b", rr_notifier);

    #1;
    zero_reference = 1;
    zero_data = 1;
    #0.001 $display("combined-zero-limits %b", zero_notifier);

    #1;
    bare_reference = 1;
    bare_data = 1;
    #0.001 $display("combined-no-notifier-survived");
    $finish;
  end
endmodule

// The Clause 31.3.3/.6 limit endpoint is excluded, while a simultaneous
// cohort is included and reaches the Clause 31.6 notifier path only once.
// SIM: simulation.suspend.clock_set
// SIM: simulation.assert.clock_occurrence.consume
// SIM-NOT: timing_check_table
// AOT: @__obelisk_aot_schedule_plan_v1
// AOT: call i32 @obelisk_rt_v1_scheduler_install_aot
// AOT: call i32 @obelisk_rt_v1_scheduler_run_aot
// CHECK-DAG: setuphold-setup-violation 1
// CHECK-DAG: setuphold-setup-endpoint 1
// CHECK-DAG: setuphold-hold-violation 0
// CHECK-DAG: setuphold-hold-endpoint 0
// CHECK-DAG: setuphold-simultaneous-once 1
// CHECK-DAG: recrem-recovery-violation 1
// CHECK-DAG: recrem-recovery-endpoint 1
// CHECK-DAG: recrem-removal-violation 0
// CHECK-DAG: recrem-removal-endpoint 0
// CHECK-DAG: recrem-simultaneous-once 1
// CHECK-DAG: combined-zero-limits 0
// CHECK-DAG: warning: system timing check violation
// CHECK-DAG: combined-no-notifier-survived
