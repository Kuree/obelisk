// RUN: obelisk -emit-sim %s -o - | FileCheck %s --check-prefix=SIM
// RUN: obelisk -O0 %s -o %t.o0.native
// RUN: %t.o0.native 2>&1 | FileCheck %s
// RUN: obelisk -O0 --execution-tier=bytecode %s -o %t.o0.bytecode
// RUN: %t.o0.bytecode 2>&1 | FileCheck %s
// RUN: obelisk -O3 %s -o %t.o3.native
// RUN: %t.o3.native 2>&1 | FileCheck %s
// RUN: obelisk -O3 --native-scheduler=aot --top=clock_control_checks \
// RUN:   -emit-llvm %s -o - | FileCheck %s --check-prefix=AOT
// RUN: obelisk -O3 --native-scheduler=aot %s -o %t.o3.aot
// RUN: %t.o3.aot 2>&1 | FileCheck %s
// RUN: obelisk -O3 --execution-tier=bytecode %s -o %t.o3.bytecode
// RUN: %t.o3.bytecode 2>&1 | FileCheck %s

`timescale 1ns / 1ps

module clock_control_checks(
    input wire enable,
    input wire skew_reference, skew_data,
    input wire zero_skew_reference, zero_skew_data,
    input wire period_signal, zero_period_signal,
    input wire width_signal, negative_width_signal,
    input wire default_width_signal, zero_width_signal,
    output reg skew_notifier = 0,
    output reg zero_skew_notifier = 0,
    output reg period_notifier = 0,
    output reg zero_period_notifier = 0,
    output reg width_notifier = 0,
    output reg negative_width_notifier = 0,
    output reg zero_width_notifier = 0);
  specify
    $skew(posedge skew_reference &&& enable,
          edge [01, 0x, x1] skew_data, 3, skew_notifier);
    $skew(posedge zero_skew_reference, posedge zero_skew_data, 0,
          zero_skew_notifier);
    $period(edge [01, 0x, x1] period_signal &&& enable, 4,
            period_notifier);
    $period(posedge zero_period_signal, 0, zero_period_notifier);
    $width(posedge width_signal &&& enable, 5, 2, width_notifier);
    $width(negedge negative_width_signal, 5, 2,
           negative_width_notifier);
    $width(posedge default_width_signal, 5);
    $width(posedge zero_width_signal, 0, 0, zero_width_notifier);
  endspecify
endmodule

module system_timing_check_clock_control_runtime;
  logic enable = 1;
  logic data_first_trigger = 0, reference_first_trigger = 0;
  logic skew_reference = 0, skew_data = 0;
  logic zero_skew_reference = 0, zero_skew_data = 0;
  logic period_signal = 0, zero_period_signal = 0;
  logic width_signal = 0, negative_width_signal = 1;
  logic default_width_signal = 0, zero_width_signal = 0;
  wire skew_notifier, zero_skew_notifier;
  wire period_notifier, zero_period_notifier;
  wire width_notifier, negative_width_notifier, zero_width_notifier;

  clock_control_checks dut(
      enable, skew_reference, skew_data,
      zero_skew_reference, zero_skew_data,
      period_signal, zero_period_signal,
      width_signal, negative_width_signal,
      default_width_signal, zero_width_signal,
      skew_notifier, zero_skew_notifier,
      period_notifier, zero_period_notifier,
      width_notifier, negative_width_notifier, zero_width_notifier);

  // Publish the two Clause 31.4.1 events from distinct static actors and in
  // both producer orders. Simultaneous means one simulation time, not one
  // scheduler publication cohort.
  initial begin
    @(posedge data_first_trigger);
    skew_data = 1;
  end
  initial begin
    @(posedge data_first_trigger);
    skew_reference = 1;
  end
  initial begin
    @(posedge reference_first_trigger);
    skew_reference = 1;
  end
  initial begin
    @(posedge reference_first_trigger);
    skew_data = 1;
  end

  initial begin
    // Clause 31.4.1: a data event before the first reference is ignored.
    #1 skew_data = 1;
    #0.001 $display("skew-unarmed %b", skew_notifier);
    skew_data = 0;

    #1 skew_reference = 1;
    #3 skew_data = 1;
    #0.001 $display("skew-endpoint %b", skew_notifier);
    skew_reference = 0;
    skew_data = 0;

    // The second reference replaces the first timestamp.
    #1 skew_reference = 1;
    #1 skew_reference = 0;
    #1 skew_reference = 1;
    #2 skew_data = 1;
    #0.001 $display("skew-restart %b", skew_notifier);
    skew_reference = 0;
    skew_data = 0;

    // Every late data event remains live after the first violation.
    #1 skew_reference = 1;
    #4 skew_data = 1;
    #0.001 $display("skew-late-first %b", skew_notifier);
    skew_data = 0;
    #1 skew_data = 1;
    #0.001 $display("skew-late-repeat %b", skew_notifier);
    skew_reference = 0;
    skew_data = 0;

    #1;
    data_first_trigger = 1;
    #0.001 $display("skew-data-first-simultaneous %b", skew_notifier);
    skew_reference = 0;
    skew_data = 0;

    #1;
    reference_first_trigger = 1;
    #0.001 $display("skew-reference-first-simultaneous %b", skew_notifier);
    skew_reference = 0;
    skew_data = 0;

    // NBA-produced reference and data events are also finalized together
    // before the Observed Clause 31.4.1 decision.
    #1;
    skew_data <= 1;
    skew_reference <= 1;
    #0.001 $display("skew-nba-simultaneous %b", skew_notifier);
    skew_reference = 0;
    skew_data = 0;

    #1 skew_reference = 1;
    #0.001 skew_reference = 0;
    #3.999;
    // Separate Inactive producer waves create two actual internal-net
    // posedges at one simulation time; neither may be collapsed to a bit.
    skew_data = 1;
    #0 skew_data = 0;
    #0 skew_data = 1;
    #0.001 $display("skew-repeated-same-time %b", skew_notifier);
    skew_data = 0;

    #1;
    zero_skew_reference = 1;
    zero_skew_data = 1;
    #0.001 $display("skew-zero-simultaneous %b", zero_skew_notifier);
    zero_skew_reference = 0;
    zero_skew_data = 0;
    #1 zero_skew_reference = 1;
    #1 zero_skew_data = 1;
    #0.001 $display("skew-zero-late %b", zero_skew_notifier);

    // Clause 31.4.5: first edge arms, a short period violates, equality and
    // a zero limit are safe.
    #1 period_signal = 1;
    #0.001 period_signal = 0;
    #2.999 period_signal = 1;
    #0.001 $display("period-short %b", period_notifier);
    period_signal = 0;
    #3.999 period_signal = 1;
    #0.001 $display("period-endpoint %b", period_notifier);
    #1 zero_period_signal = 1;
    #0.001 zero_period_signal = 0;
    #0.001 zero_period_signal = 1;
    #0.001 $display("period-zero %b", zero_period_notifier);

    // Clause 31.4.4: both threshold and limit endpoints are excluded.
    enable = 0;
    #1 width_signal = 1;
    #3 width_signal = 0;
    #0.001 $display("width-conditioned-off %b", width_notifier);
    enable = 1;
    #1 width_signal = 1;
    #2 width_signal = 0;
    #0.001 $display("width-threshold %b", width_notifier);
    #1 width_signal = 1;
    #3 width_signal = 0;
    #0.001 $display("width-interior %b", width_notifier);
    #1 width_signal = 1;
    #5 width_signal = 0;
    #0.001 $display("width-limit %b", width_notifier);

    #1 negative_width_signal = 0;
    #3 negative_width_signal = 1;
    #0.001 $display("width-negedge %b", negative_width_notifier);

    // Omitted threshold defaults to zero and still reports a nonzero short
    // pulse even without a notifier. A zero width limit never violates.
    #1 default_width_signal = 1;
    #1 default_width_signal = 0;
    #0.001 $display("width-default-survived");
    #1 zero_width_signal = 1;
    #1 zero_width_signal = 0;
    #0.001 $display("width-zero %b", zero_width_notifier);
    $finish;
  end
endmodule

// SIM: obelisk_sim.suspend.clock_set
// SIM: edges [1, 2]
// SIM: obelisk_sim.assert.clock_occurrence.consume
// SIM-NOT: timing_check_table
// AOT: @__obelisk_aot_schedule_plan_v1
// AOT: call i32 @obelisk_rt_v1_scheduler_install_aot
// AOT: call i32 @obelisk_rt_v1_scheduler_run_aot
// CHECK-DAG: skew-unarmed 0
// CHECK-DAG: skew-endpoint 0
// CHECK-DAG: skew-restart 0
// CHECK-DAG: skew-late-first 1
// CHECK-DAG: skew-late-repeat 0
// CHECK-DAG: skew-data-first-simultaneous 0
// CHECK-DAG: skew-reference-first-simultaneous 0
// CHECK-DAG: skew-nba-simultaneous 0
// CHECK-DAG: skew-repeated-same-time 0
// CHECK-DAG: skew-zero-simultaneous 0
// CHECK-DAG: skew-zero-late 1
// CHECK-DAG: period-short 1
// CHECK-DAG: period-endpoint 1
// CHECK-DAG: period-zero 0
// CHECK-DAG: width-conditioned-off 0
// CHECK-DAG: width-threshold 0
// CHECK-DAG: width-interior 1
// CHECK-DAG: width-limit 1
// CHECK-DAG: width-negedge 1
// CHECK-DAG: warning: system timing check violation
// CHECK-DAG: width-default-survived
// CHECK-DAG: width-zero 0
