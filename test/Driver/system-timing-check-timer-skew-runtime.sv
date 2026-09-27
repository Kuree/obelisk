// RUN: obelisk -O0 --native-scheduler=generic %s -o %t.o0.native
// RUN: %t.o0.native 2>&1 | FileCheck %s
// RUN: obelisk -O0 --execution-tier=bytecode %s -o %t.o0.bytecode
// RUN: %t.o0.bytecode 2>&1 | FileCheck %s
// RUN: obelisk -O0 --native-scheduler=aot %s -o %t.o0.aot
// RUN: %t.o0.aot 2>&1 | FileCheck %s
// RUN: obelisk -O3 --native-scheduler=generic %s -o %t.o3.native
// RUN: %t.o3.native 2>&1 | FileCheck %s
// RUN: obelisk -O3 --execution-tier=bytecode %s -o %t.o3.bytecode
// RUN: %t.o3.bytecode 2>&1 | FileCheck %s
// RUN: obelisk -O3 --native-scheduler=aot %s -o %t.o3.aot
// RUN: %t.o3.aot 2>&1 | FileCheck %s
// RUN: obelisk -O3 --native-scheduler=aot \
// RUN:   --top=system_timing_check_timer_skew_runtime -emit-llvm %s -o - \
// RUN:   | FileCheck %s --check-prefix=AOT

`timescale 1ns / 1ps

module system_timing_check_timer_skew_runtime;
  logic fail = 0;

  logic ts_expire_ref = 0, ts_expire_data = 0;
  logic ts_equal_ref = 0, ts_equal_data = 0;
  logic ts_restart_ref = 0, ts_restart_data = 0;
  logic ts_false_ref = 0, ts_false_data = 0, ts_false_cond = 1;
  logic ts_remain_ref = 0, ts_remain_data = 0, ts_remain_cond = 1;
  logic ts_zero_ref = 0, ts_zero_data = 0;
  logic ts_zero_both_ref = 0, ts_zero_both_data = 0;
  logic ts_reactive_ref = 0, ts_reactive_data = 0, ts_reactive_trigger = 0;
  logic ts_renba_ref = 0, ts_renba_data = 0, ts_renba_trigger = 0;
  logic ts_inactive_ref = 0, ts_inactive_data = 0;
  logic ts_nba_ref = 0, ts_nba_data = 0;
  logic ts_reinactive_ref = 0, ts_reinactive_data = 0;
  logic ts_reinactive_trigger = 0;
  logic ts_in_limit_ref = 0, ts_in_limit_data = 0;
  logic ts_late_ref = 0, ts_late_data = 0;
  logic ts_stress_ref = 0, ts_stress_data = 0;
  reg ts_expire_n = 0, ts_equal_n = 0, ts_restart_n = 0;
  reg ts_false_n = 0, ts_remain_n = 0, ts_zero_n = 0;
  reg ts_zero_both_n = 0, ts_reactive_n = 0, ts_renba_n = 0;
  reg ts_inactive_n = 0, ts_nba_n = 0, ts_reinactive_n = 0;
  reg ts_in_limit_n = 0, ts_late_n = 0, ts_stress_n = 0;

  logic fs_ref_expire_ref = 0, fs_ref_expire_data = 0;
  logic fs_data_expire_ref = 0, fs_data_expire_data = 0;
  logic fs_equal0_ref = 0, fs_equal0_data = 0;
  logic fs_equal1_ref = 0, fs_equal1_data = 0;
  logic fs_restart_ref = 0, fs_restart_data = 0;
  logic fs_false_ref = 0, fs_false_data = 0, fs_false_cond = 1;
  logic fs_remain_ref = 0, fs_remain_data = 0, fs_remain_cond = 1;
  logic fs_both_ref = 0, fs_both_data = 0;
  logic fs_default_ref = 0, fs_default_data = 0;
  logic fs_data_cond_ref = 0, fs_data_cond_data = 0;
  logic fs_data_cond = 1;
  reg fs_ref_expire_n = 0, fs_data_expire_n = 0;
  reg fs_equal0_n = 0, fs_equal1_n = 0, fs_restart_n = 0;
  reg fs_false_n = 0, fs_remain_n = 0, fs_both_n = 0;
  reg fs_default_n = 0, fs_data_cond_n = 0;

  specify
    $timeskew(posedge ts_expire_ref, posedge ts_expire_data, 2,
              ts_expire_n);
    $timeskew(posedge ts_equal_ref, posedge ts_equal_data, 2,
              ts_equal_n, 0, 0);
    $timeskew(posedge ts_restart_ref, posedge ts_restart_data, 2,
              ts_restart_n, 0, 0);
    $timeskew(posedge ts_false_ref &&& ts_false_cond,
              posedge ts_false_data, 2, ts_false_n, 0, 0);
    $timeskew(posedge ts_remain_ref &&& ts_remain_cond,
              posedge ts_remain_data, 2, ts_remain_n, 0, 1);
    $timeskew(posedge ts_zero_ref, posedge ts_zero_data, 0,
              ts_zero_n, 0, 0);
    $timeskew(posedge ts_zero_both_ref, posedge ts_zero_both_data, 0,
              ts_zero_both_n, 0, 0);
    $timeskew(posedge ts_reactive_ref, posedge ts_reactive_data, 2,
              ts_reactive_n, 0, 0);
    $timeskew(posedge ts_renba_ref, posedge ts_renba_data, 2,
              ts_renba_n, 0, 0);
    $timeskew(posedge ts_inactive_ref, posedge ts_inactive_data, 2,
              ts_inactive_n, 0, 0);
    $timeskew(posedge ts_nba_ref, posedge ts_nba_data, 2,
              ts_nba_n, 0, 0);
    $timeskew(posedge ts_reinactive_ref, posedge ts_reinactive_data, 2,
              ts_reinactive_n, 0, 0);
    $timeskew(posedge ts_in_limit_ref, posedge ts_in_limit_data, 2,
              ts_in_limit_n, 0, 0);
    $timeskew(posedge ts_late_ref, posedge ts_late_data, 2,
              ts_late_n, 0, 0);
    $timeskew(posedge ts_stress_ref, posedge ts_stress_data, 2,
              ts_stress_n, 0, 0);

    $fullskew(posedge fs_ref_expire_ref, posedge fs_ref_expire_data,
              2, 3, fs_ref_expire_n, 0, 0);
    $fullskew(posedge fs_data_expire_ref, posedge fs_data_expire_data,
              2, 3, fs_data_expire_n, 0, 0);
    $fullskew(posedge fs_equal0_ref, posedge fs_equal0_data,
              2, 3, fs_equal0_n, 0, 0);
    $fullskew(posedge fs_equal1_ref, posedge fs_equal1_data,
              2, 3, fs_equal1_n, 0, 0);
    $fullskew(posedge fs_restart_ref, posedge fs_restart_data,
              2, 3, fs_restart_n, 0, 0);
    $fullskew(posedge fs_false_ref &&& fs_false_cond,
              posedge fs_false_data, 2, 3, fs_false_n, 0, 0);
    $fullskew(posedge fs_remain_ref &&& fs_remain_cond,
              posedge fs_remain_data, 2, 3, fs_remain_n, 0, 1);
    $fullskew(posedge fs_both_ref, posedge fs_both_data,
              0, 0, fs_both_n, 0, 0);
    // IEEE 1800-2017 31.4.3: omitted flags select timer mode and the
    // remain-active default.  Exercise the data-side condition separately
    // from the reference-side conditioned checks above.
    $fullskew(posedge fs_default_ref, posedge fs_default_data,
              2, 3, fs_default_n);
    $fullskew(posedge fs_data_cond_ref,
              posedge fs_data_cond_data &&& fs_data_cond,
              2, 3, fs_data_cond_n, 0, 0);
  endspecify

  // A deadline-equal timecheck produced in Reactive or Re-NBA cancels before
  // the single slot-final coordinator validates the helper's Re-NBA maturity.
  assert property (@(posedge ts_reactive_trigger) fail)
    else ts_reactive_data = 1;
  assert property (@(posedge ts_renba_trigger) fail)
    else ts_renba_data <= 1;
  assert property (@(posedge ts_reinactive_trigger) fail)
    else #0 ts_reinactive_data = 1;

  initial begin
    // IEEE 1800-2017 31.4.2: expiry reports, equality cancels, a replacement
    // deadline makes the old delayed event stale, and zero expires this slot.
    #1 ts_expire_ref = 1;
    #2.001 $display("timer-timeskew-expire %b", ts_expire_n);

    #1 ts_equal_ref = 1;
    #2 ts_equal_data = 1;
    #0.001 $display("timer-timeskew-equality %b", ts_equal_n);

    #1 ts_restart_ref = 1;
    #1 ts_restart_ref = 0;
    #0 ts_restart_ref = 1;
    #1.001 $display("timer-timeskew-stale %b", ts_restart_n);
    #1.000 $display("timer-timeskew-restart %b", ts_restart_n);

    #1 ts_false_ref = 1;
    #0.5 begin ts_false_ref = 0; ts_false_cond = 0; end
    #0.5 ts_false_ref = 1;
    #1.001 $display("timer-timeskew-false %b", ts_false_n);

    #1 ts_remain_ref = 1;
    #0.5 begin ts_remain_ref = 0; ts_remain_cond = 0; end
    #0.5 ts_remain_ref = 1;
    #1.001 $display("timer-timeskew-remain %b", ts_remain_n);

    #1 ts_zero_ref = 1;
    #0.001 $display("timer-timeskew-zero %b", ts_zero_n);
    #1 ts_zero_both_ref = 1;
    #0 ts_zero_both_data = 1;
    #0.001 $display("timer-timeskew-zero-both %b", ts_zero_both_n);

    #1 ts_reactive_ref = 1;
    #2 ts_reactive_trigger = 1;
    #0.001 $display("timer-timeskew-reactive-equality %b", ts_reactive_n);
    #1 ts_renba_ref = 1;
    #2 ts_renba_trigger = 1;
    #0.001 $display("timer-timeskew-renba-equality %b", ts_renba_n);

    // Equality in Inactive, NBA, and Re-Inactive is still inside the open
    // violation boundary.  The replaceable maturity itself runs in Re-NBA,
    // after all three producer regions (IEEE 1800-2017 4.4 and 31.4.2).
    #1 ts_inactive_ref = 1;
    #2 #0 ts_inactive_data = 1;
    #0.001 $display("timer-timeskew-inactive-equality %b", ts_inactive_n);
    #1 ts_nba_ref = 1;
    #2 ts_nba_data <= 1;
    #0.001 $display("timer-timeskew-nba-equality %b", ts_nba_n);
    #1 ts_reinactive_ref = 1;
    #2 ts_reinactive_trigger = 1;
    #0.001 $display("timer-timeskew-reinactive-equality %b", ts_reinactive_n);

    #1 ts_in_limit_ref = 1;
    #1.999 ts_in_limit_data = 1;
    #0.002 $display("timer-timeskew-strictly-in-limit %b", ts_in_limit_n);

    // Once a directional timeskew window expires, a late data event does not
    // create a new window or notifier occurrence (31.4.2 dormancy).
    #1 ts_late_ref = 1;
    #2.001 ts_late_data = 1;
    #0.001 $display("timer-timeskew-late-data-dormant %b", ts_late_n);

    // Restarting a long window repeatedly must replace one pending maturity,
    // rather than accumulating stale delayed events.
    #1 ts_stress_ref = 1;
    repeat (1000) begin
      #0 ts_stress_ref = 0;
      #0.001 ts_stress_ref = 1;
    end
    #0.001 ts_stress_data = 1;
    #1.001 $display("timer-timeskew-restart-stress %b", ts_stress_n);

    // IEEE 1800-2017 31.4.3 selects limit1 for reference-first and limit2
    // for data-first windows. Opposite equality cancels in both directions.
    #1 fs_ref_expire_ref = 1;
    #2.001 $display("timer-fullskew-reference-expire %b", fs_ref_expire_n);
    #1 fs_data_expire_data = 1;
    #3.001 $display("timer-fullskew-data-expire %b", fs_data_expire_n);

    #1 fs_equal0_ref = 1;
    #2 fs_equal0_data = 1;
    #0.001 $display("timer-fullskew-limit1-equality %b", fs_equal0_n);
    #1 fs_equal1_data = 1;
    #3 fs_equal1_ref = 1;
    #0.001 $display("timer-fullskew-limit2-equality %b", fs_equal1_n);

    #1 fs_restart_ref = 1;
    #1 fs_restart_ref = 0;
    #0 fs_restart_ref = 1;
    #1.001 $display("timer-fullskew-stale %b", fs_restart_n);
    #1.000 $display("timer-fullskew-restart %b", fs_restart_n);

    #1 fs_false_ref = 1;
    #0.5 begin fs_false_ref = 0; fs_false_cond = 0; end
    #0.5 fs_false_ref = 1;
    #1.001 $display("timer-fullskew-false %b", fs_false_n);
    #1 fs_remain_ref = 1;
    #0.5 begin fs_remain_ref = 0; fs_remain_cond = 0; end
    #0.5 fs_remain_ref = 1;
    #1.001 $display("timer-fullskew-remain %b", fs_remain_n);

    #1 fs_both_ref = 1;
    #0 fs_both_data = 1;
    #0.001 $display("timer-fullskew-simultaneous-zero %b", fs_both_n);

    #1 fs_default_ref = 1;
    #2.001 $display("timer-fullskew-default %b", fs_default_n);

    #1 fs_data_cond_data = 1;
    #0.5 begin fs_data_cond_data = 0; fs_data_cond = 0; end
    #0.5 fs_data_cond_data = 1;
    #2.001 $display("timer-fullskew-data-condition %b", fs_data_cond_n);
    $finish;
  end
endmodule

// AOT: @__obelisk_aot_schedule_plan_v1
// Coroutine splitting can place the timer body after scheduler installation.
// AOT-DAG: call void @obelisk_rt_v1_scheduler_event_replace_after
// AOT-DAG: call i32 @obelisk_rt_v1_scheduler_install_aot
// AOT-DAG: call i32 @obelisk_rt_v1_scheduler_run_aot
// CHECK-DAG: timer-timeskew-expire 1
// CHECK-DAG: timer-timeskew-equality 0
// CHECK-DAG: timer-timeskew-stale 0
// CHECK-DAG: timer-timeskew-restart 1
// CHECK-DAG: timer-timeskew-false 0
// CHECK-DAG: timer-timeskew-remain 1
// CHECK-DAG: timer-timeskew-zero 1
// CHECK-DAG: timer-timeskew-zero-both 0
// CHECK-DAG: timer-timeskew-reactive-equality 0
// CHECK-DAG: timer-timeskew-renba-equality 0
// CHECK-DAG: timer-timeskew-inactive-equality 0
// CHECK-DAG: timer-timeskew-nba-equality 0
// CHECK-DAG: timer-timeskew-reinactive-equality 0
// CHECK-DAG: timer-timeskew-strictly-in-limit 0
// CHECK-DAG: timer-timeskew-late-data-dormant 1
// CHECK-DAG: timer-timeskew-restart-stress 0
// CHECK-DAG: timer-fullskew-reference-expire 1
// CHECK-DAG: timer-fullskew-data-expire 1
// CHECK-DAG: timer-fullskew-limit1-equality 0
// CHECK-DAG: timer-fullskew-limit2-equality 0
// CHECK-DAG: timer-fullskew-stale 0
// CHECK-DAG: timer-fullskew-restart 1
// CHECK-DAG: timer-fullskew-false 0
// CHECK-DAG: timer-fullskew-remain 1
// CHECK-DAG: timer-fullskew-simultaneous-zero 0
// CHECK-DAG: timer-fullskew-default 1
// CHECK-DAG: timer-fullskew-data-condition 0
