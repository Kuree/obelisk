// RUN: obelisk -emit-sim %s -o - | FileCheck %s --check-prefix=SIM
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
// RUN: obelisk -O3 --native-scheduler=aot --top=event_skew_checks \
// RUN:   -emit-llvm %s -o - | FileCheck %s --check-prefix=AOT

`timescale 1ns / 1ps

module event_skew_checks(
    input wire [1:0] ts0_condition,
    input wire ts0_reference, ts0_data,
    input wire ts1_condition, ts1_reference, ts1_data,
    input wire full_reference, full_data,
    input wire [1:0] full0_condition,
    input wire full0_reference, full0_data,
    input wire [1:0] full1_condition,
    input wire full1_reference, full1_data,
    input wire ts_tf_condition, ts_tf_reference, ts_tf_data,
    input wire ts_ft_condition, ts_ft_reference, ts_ft_data,
    input wire full_tf_condition, full_tf_reference, full_tf_data,
    input wire full_ft_condition, full_ft_reference, full_ft_data,
    output reg ts0_notifier = 0,
    output reg ts1_notifier = 0,
    output reg full_notifier = 0,
    output reg full0_notifier = 0,
    output reg full1_notifier = 0,
    output reg ts_tf_notifier = 0,
    output reg ts_ft_notifier = 0,
    output reg full_tf_notifier = 0,
    output reg full_ft_notifier = 0);
  specify
    $timeskew(edge [01, 0x, x1] ts0_reference &&& ts0_condition,
              posedge ts0_data, 2, ts0_notifier, 1 + 1, 2 - 2);
    $timeskew(posedge ts1_reference &&& ts1_condition,
              posedge ts1_data, 2, ts1_notifier, 1, 1);
    $fullskew(posedge full_reference, posedge full_data,
              2, 3, full_notifier, 1, 0);
    $fullskew(posedge full0_reference &&& full0_condition,
              posedge full0_data, 2, 3, full0_notifier, 1, 0);
    $fullskew(posedge full1_reference &&& full1_condition,
              posedge full1_data, 2, 3, full1_notifier, 1, 1);
    $timeskew(posedge ts_tf_reference &&& ts_tf_condition,
              posedge ts_tf_data, 2, ts_tf_notifier, 1, 0);
    $timeskew(posedge ts_ft_reference &&& ts_ft_condition,
              posedge ts_ft_data, 2, ts_ft_notifier, 1, 0);
    $fullskew(posedge full_tf_reference &&& full_tf_condition,
              posedge full_tf_data, 2, 3, full_tf_notifier, 1, 0);
    $fullskew(posedge full_ft_reference &&& full_ft_condition,
              posedge full_ft_data, 2, 3, full_ft_notifier, 1, 0);
  endspecify
endmodule

module system_timing_check_event_skew_runtime;
  logic [1:0] ts0_condition = 1;
  logic ts0_reference = 0, ts0_data = 0;
  logic ts1_condition = 1, ts1_reference = 0, ts1_data = 0;
  logic full_reference = 0, full_data = 0;
  logic [1:0] full0_condition = 1;
  logic full0_reference = 0, full0_data = 0;
  logic [1:0] full1_condition = 1;
  logic full1_reference = 0, full1_data = 0;
  logic ts_tf_condition = 1, ts_tf_reference = 0, ts_tf_data = 0;
  logic ts_ft_condition = 0, ts_ft_reference = 0, ts_ft_data = 0;
  logic full_tf_condition = 1, full_tf_reference = 0, full_tf_data = 0;
  logic full_ft_condition = 0, full_ft_reference = 0, full_ft_data = 0;
  wire ts0_notifier, ts1_notifier, full_notifier;
  wire full0_notifier, full1_notifier;
  wire ts_tf_notifier, ts_ft_notifier;
  wire full_tf_notifier, full_ft_notifier;

  event_skew_checks dut(
      ts0_condition, ts0_reference, ts0_data,
      ts1_condition, ts1_reference, ts1_data,
      full_reference, full_data,
      full0_condition, full0_reference, full0_data,
      full1_condition, full1_reference, full1_data,
      ts_tf_condition, ts_tf_reference, ts_tf_data,
      ts_ft_condition, ts_ft_reference, ts_ft_data,
      full_tf_condition, full_tf_reference, full_tf_data,
      full_ft_condition, full_ft_reference, full_ft_data,
      ts0_notifier, ts1_notifier, full_notifier,
      full0_notifier, full1_notifier,
      ts_tf_notifier, ts_ft_notifier,
      full_tf_notifier, full_ft_notifier);

  initial begin
    // IEEE 1800-2017 31.4.2: equality is safe; event-only mode reports once
    // and becomes dormant until a new true reference timestamp.
    #1 ts0_data = 1;
    #0.001 $display("timeskew-unarmed %b", ts0_notifier);
    ts0_data = 0;
    #1 ts0_reference = 1;
    #2 ts0_data = 1;
    #0.001 $display("timeskew-endpoint %b", ts0_notifier);
    ts0_reference = 0; ts0_data = 0;
    #1 ts0_reference = 1;
    #3 ts0_data = 1;
    #0.001 $display("timeskew-first-late %b", ts0_notifier);
    ts0_data = 0;
    #1 ts0_data = 1;
    #0.001 $display("timeskew-dormant %b", ts0_notifier);
    ts0_reference = 0; ts0_data = 0;
    #1 ts0_reference = 1;
    #0.001 ts0_reference = 0;
    ts0_condition = 2'b10;
    #1 ts0_reference = 1;
    #3 ts0_data = 1;
    #0.001 $display("timeskew-false-reference %b", ts0_notifier);
    ts0_reference = 0; ts0_data = 0; ts0_condition = 1;
    #1 begin ts0_reference = 1; ts0_data = 1; end
    #0.001 $display("timeskew-simultaneous %b", ts0_notifier);

    // remain_active preserves $skew-style repeated reports and ignores a
    // false conditioned reference transition.
    #1 ts1_reference = 1;
    #3 ts1_data = 1;
    #0.001 $display("timeskew-remain-first %b", ts1_notifier);
    ts1_data = 0;
    #1 ts1_data = 1;
    #0.001 $display("timeskew-remain-repeat %b", ts1_notifier);
    ts1_reference = 0; ts1_data = 0; ts1_condition = 0;
    #1 ts1_reference = 1;
    #3 ts1_data = 1;
    #0.001 $display("timeskew-remain-false-reference %b", ts1_notifier);

    // IEEE 1800-2017 31.4.3 uses limit1 in reference->data direction and
    // limit2 in data->reference direction. An in-limit opposite event makes
    // the check dormant; a late one reports and becomes the reversed window.
    #1 full_reference = 1;
    #2 full_data = 1;
    #0.001 $display("fullskew-limit1-endpoint %b", full_notifier);
    full_reference = 0; full_data = 0;
    #1 full_data = 1;
    #3 full_reference = 1;
    #0.001 $display("fullskew-limit2-endpoint %b", full_notifier);
    full_reference = 0; full_data = 0;
    #1 full_reference = 1;
    #3 full_data = 1;
    #0.001 $display("fullskew-late-reversed %b", full_notifier);
    full_reference = 0; full_data = 0;
    #1 full_data = 1;
    #0.001 full_data = 0;
    #4 full_reference = 1;
    #0.001 $display("fullskew-restart-late %b", full_notifier);
    full_reference = 0; full_data = 0;
    // Distinct producer waves at one numeric time remain simultaneous for
    // Clause 31.4.3 even though the occurrence coordinator orders them.
    #1 full_reference = 1;
    #0 full_data = 1;
    #0.001 $display("fullskew-simultaneous %b", full_notifier);

    // A false conditioned timestamp makes event-only mode dormant, while
    // remain_active leaves the open directional window untouched.
    #1 full0_reference = 1;
    #0.001 full0_reference = 0; full0_condition = 2'b10;
    #1 full0_reference = 1;
    #4 full0_data = 1;
    #0.001 $display("fullskew-false-timestamp %b", full0_notifier);

    #1 full1_reference = 1;
    #0.001 full1_reference = 0; full1_condition = 2'b10;
    #1 full1_reference = 1;
    #4 full1_data = 1;
    #0.001 $display("fullskew-remain-false-timestamp %b", full1_notifier);

    // IEEE 1800-2017 31.4.2/.3 apply a false conditioned timestamp in
    // occurrence order. The #0 waves keep both edges at one numeric time:
    // true-then-false ends dormant, while false-then-true starts a window.
    #1 ts_tf_reference = 1;
    #0 ts_tf_reference = 0;
    #0 begin ts_tf_condition = 0; ts_tf_reference = 1; end
    #3 ts_tf_data = 1;
    #0.001 $display("timeskew-same-slot-true-false %b", ts_tf_notifier);

    #1 ts_ft_reference = 1;
    #0 ts_ft_reference = 0;
    #0 begin ts_ft_condition = 1; ts_ft_reference = 1; end
    #3 ts_ft_data = 1;
    #0.001 $display("timeskew-same-slot-false-true %b", ts_ft_notifier);

    #1 full_tf_reference = 1;
    #0 full_tf_reference = 0;
    #0 begin full_tf_condition = 0; full_tf_reference = 1; end
    #3 full_tf_data = 1;
    #0.001 $display("fullskew-same-slot-true-false %b", full_tf_notifier);

    #1 full_ft_reference = 1;
    #0 full_ft_reference = 0;
    #0 begin full_ft_condition = 1; full_ft_reference = 1; end
    #3 full_ft_data = 1;
    #0.001 $display("fullskew-same-slot-false-true %b", full_ft_notifier);
    $finish;
  end
endmodule

// SIM: timing_check_event_based = true
// SIM-SAME: timing_check_kind = 8 : i32
// SIM-SAME: timing_check_remain_active = false
// SIM: simulation.suspend.clock_set
// SIM-SAME: edges [1, 1, 1]
// SIM-SAME: slot_final
// SIM: arith.cmpi ugt
// SIM: timing_check_kind = 9 : i32
// SIM-NOT: timing_check_table
// SIM-NOT: timing_check_timer
// AOT: @__obelisk_aot_schedule_plan_v1
// Coroutine splitting can place the observer body after scheduler installation.
// AOT-DAG: call i64 @obelisk_rt_v1_clock_occurrence_consume
// AOT-DAG: call i32 @obelisk_rt_v1_scheduler_install_aot
// AOT-DAG: call i32 @obelisk_rt_v1_scheduler_run_aot
// CHECK-DAG: timeskew-unarmed 0
// CHECK-DAG: timeskew-endpoint 0
// CHECK-DAG: timeskew-first-late 1
// CHECK-DAG: timeskew-dormant 1
// CHECK-DAG: timeskew-false-reference 1
// CHECK-DAG: timeskew-simultaneous 1
// CHECK-DAG: timeskew-remain-first 1
// CHECK-DAG: timeskew-remain-repeat 0
// CHECK-DAG: timeskew-remain-false-reference 1
// CHECK-DAG: fullskew-limit1-endpoint 0
// CHECK-DAG: fullskew-limit2-endpoint 0
// CHECK-DAG: fullskew-late-reversed 1
// CHECK-DAG: fullskew-restart-late 0
// CHECK-DAG: fullskew-simultaneous 0
// CHECK-DAG: fullskew-false-timestamp 0
// CHECK-DAG: fullskew-remain-false-timestamp 1
// CHECK-DAG: timeskew-same-slot-true-false 0
// CHECK-DAG: timeskew-same-slot-false-true 1
// CHECK-DAG: fullskew-same-slot-true-false 0
// CHECK-DAG: fullskew-same-slot-false-true 1
