// RUN: obelisk -O0 %s -o %t.o0.native
// RUN: %t.o0.native 2>&1 | FileCheck %s
// RUN: obelisk -O0 --execution-tier=bytecode %s -o %t.o0.bytecode
// RUN: %t.o0.bytecode 2>&1 | FileCheck %s
// RUN: obelisk -O0 --native-scheduler=aot %s -o %t.o0.aot
// RUN: %t.o0.aot 2>&1 | FileCheck %s
// RUN: obelisk -O3 %s -o %t.o3.native
// RUN: %t.o3.native 2>&1 | FileCheck %s
// RUN: obelisk -O3 --execution-tier=bytecode %s -o %t.o3.bytecode
// RUN: %t.o3.bytecode 2>&1 | FileCheck %s
// RUN: obelisk -O3 --native-scheduler=aot %s -o %t.o3.aot
// RUN: %t.o3.aot 2>&1 | FileCheck %s

`timescale 1ns / 1ps

module system_timing_check_slot_final_runtime;
  logic fail = 0;

  logic reactive_data_first_reference = 0;
  logic reactive_data_first_data = 0;
  logic reactive_data_first_trigger = 0;
  reg reactive_data_first_notifier = 0;
  logic reactive_reference_first_reference = 0;
  logic reactive_reference_first_data = 0;
  logic reactive_reference_first_trigger = 0;
  reg reactive_reference_first_notifier = 0;
  logic reactive_no_reference = 0, reactive_no_data = 0;
  logic reactive_no_trigger = 0;
  reg reactive_no_notifier = 0;

  logic reinactive_data_first_reference = 0;
  logic reinactive_data_first_data = 0;
  logic reinactive_data_first_trigger = 0;
  reg reinactive_data_first_notifier = 0;
  logic reinactive_reference_first_reference = 0;
  logic reinactive_reference_first_data = 0;
  logic reinactive_reference_first_trigger = 0;
  reg reinactive_reference_first_notifier = 0;
  logic reinactive_no_reference = 0, reinactive_no_data = 0;
  logic reinactive_no_trigger = 0;
  reg reinactive_no_notifier = 0;

  logic renba_data_first_reference = 0, renba_data_first_data = 0;
  logic renba_data_first_trigger = 0;
  reg renba_data_first_notifier = 0;
  logic renba_reference_first_reference = 0;
  logic renba_reference_first_data = 0;
  logic renba_reference_first_trigger = 0;
  reg renba_reference_first_notifier = 0;
  logic renba_no_reference = 0, renba_no_data = 0;
  logic renba_no_trigger = 0;
  reg renba_no_notifier = 0;

  logic setup_reference = 0, setup_data = 0, setup_trigger = 0;
  logic hold_reference = 0, hold_data = 0, hold_trigger = 0;
  logic removal_reference = 0, removal_data = 0, removal_trigger = 0;
  logic recovery_reference = 0, recovery_data = 0, recovery_trigger = 0;
  reg setup_notifier = 0, hold_notifier = 0;
  reg removal_notifier = 0, recovery_notifier = 0;
  logic setuphold_data_first_reference = 0;
  logic setuphold_data_first_data = 0;
  logic setuphold_data_first_trigger = 0;
  logic setuphold_reference_first_reference = 0;
  logic setuphold_reference_first_data = 0;
  logic setuphold_reference_first_trigger = 0;
  logic recrem_data_first_reference = 0, recrem_data_first_data = 0;
  logic recrem_data_first_trigger = 0;
  logic recrem_reference_first_reference = 0;
  logic recrem_reference_first_data = 0;
  logic recrem_reference_first_trigger = 0;
  reg setuphold_data_first_notifier = 0;
  reg setuphold_reference_first_notifier = 0;
  reg recrem_data_first_notifier = 0;
  reg recrem_reference_first_notifier = 0;

  specify
    $skew(posedge reactive_data_first_reference,
          posedge reactive_data_first_data, 2,
          reactive_data_first_notifier);
    $skew(posedge reactive_reference_first_reference,
          posedge reactive_reference_first_data, 2,
          reactive_reference_first_notifier);
    $skew(posedge reactive_no_reference, posedge reactive_no_data, 2,
          reactive_no_notifier);
    $skew(posedge reinactive_data_first_reference,
          posedge reinactive_data_first_data, 2,
          reinactive_data_first_notifier);
    $skew(posedge reinactive_reference_first_reference,
          posedge reinactive_reference_first_data, 2,
          reinactive_reference_first_notifier);
    $skew(posedge reinactive_no_reference, posedge reinactive_no_data, 2,
          reinactive_no_notifier);
    $skew(posedge renba_data_first_reference,
          posedge renba_data_first_data, 2, renba_data_first_notifier);
    $skew(posedge renba_reference_first_reference,
          posedge renba_reference_first_data, 2,
          renba_reference_first_notifier);
    $skew(posedge renba_no_reference, posedge renba_no_data, 2,
          renba_no_notifier);
    $setup(posedge setup_data, posedge setup_reference, 5, setup_notifier);
    $hold(posedge hold_reference, posedge hold_data, 5, hold_notifier);
    $removal(posedge removal_reference, posedge removal_data, 5,
             removal_notifier);
    $recovery(posedge recovery_reference, posedge recovery_data, 5,
              recovery_notifier);
    $setuphold(posedge setuphold_data_first_reference,
               posedge setuphold_data_first_data, 5, 5,
               setuphold_data_first_notifier);
    $setuphold(posedge setuphold_reference_first_reference,
               posedge setuphold_reference_first_data, 5, 5,
               setuphold_reference_first_notifier);
    $recrem(posedge recrem_data_first_reference,
            posedge recrem_data_first_data, 5, 5,
            recrem_data_first_notifier);
    $recrem(posedge recrem_reference_first_reference,
            posedge recrem_reference_first_data, 5, 5,
            recrem_reference_first_notifier);
  endspecify

  // IEEE 1800-2017 31.4.1 defines simultaneous by simulation time. These
  // action blocks publish the second event in Reactive, Re-Inactive, or
  // Re-NBA after the first event has already been published in Active.
  assert property (@(posedge reactive_data_first_trigger) fail)
    else reactive_data_first_reference = 1;
  assert property (@(posedge reactive_reference_first_trigger) fail)
    else reactive_reference_first_data = 1;
  assert property (@(posedge reactive_no_trigger) fail)
    else reactive_no_data = 1;

  assert property (@(posedge reinactive_data_first_trigger) fail)
    else #0 reinactive_data_first_reference = 1;
  assert property (@(posedge reinactive_reference_first_trigger) fail)
    else #0 reinactive_reference_first_data = 1;
  assert property (@(posedge reinactive_no_trigger) fail)
    else #0 reinactive_no_data = 1;

  assert property (@(posedge renba_data_first_trigger) fail)
    else renba_data_first_reference <= 1;
  assert property (@(posedge renba_reference_first_trigger) fail)
    else renba_reference_first_data <= 1;
  assert property (@(posedge renba_no_trigger) fail)
    else renba_no_data <= 1;

  // Clause 31.3 open setup/removal endpoints suppress a same-slot event;
  // inclusive hold/recovery endpoints report it even when the timestamp
  // arrives later in Reactive. Combined checks report one violation in both
  // same-slot producer orders (31.3.3 and 31.3.6).
  assert property (@(posedge setup_trigger) fail)
    else setup_reference = 1;
  assert property (@(posedge hold_trigger) fail)
    else hold_reference = 1;
  assert property (@(posedge removal_trigger) fail)
    else removal_reference = 1;
  assert property (@(posedge recovery_trigger) fail)
    else recovery_reference = 1;
  assert property (@(posedge setuphold_data_first_trigger) fail)
    else setuphold_data_first_reference = 1;
  assert property (@(posedge setuphold_reference_first_trigger) fail)
    else setuphold_reference_first_data = 1;
  assert property (@(posedge recrem_data_first_trigger) fail)
    else recrem_data_first_reference = 1;
  assert property (@(posedge recrem_reference_first_trigger) fail)
    else recrem_reference_first_data = 1;

  initial begin
    #1 reactive_data_first_data = 1;
    reactive_data_first_trigger = 1;
    #0.001 $display("reactive-data-first %b",
                    reactive_data_first_notifier);

    #1 reactive_reference_first_reference = 1;
    reactive_reference_first_trigger = 1;
    #0.001 $display("reactive-reference-first %b",
                    reactive_reference_first_notifier);

    #1 reactive_no_reference = 1;
    #0.001 reactive_no_reference = 0;
    #2.999 reactive_no_trigger = 1;
    #0.001 $display("reactive-no-reference %b", reactive_no_notifier);

    #1 reinactive_data_first_data = 1;
    reinactive_data_first_trigger = 1;
    #0.001 $display("reinactive-data-first %b",
                    reinactive_data_first_notifier);

    #1 reinactive_reference_first_reference = 1;
    reinactive_reference_first_trigger = 1;
    #0.001 $display("reinactive-reference-first %b",
                    reinactive_reference_first_notifier);

    #1 reinactive_no_reference = 1;
    #0.001 reinactive_no_reference = 0;
    #2.999 reinactive_no_trigger = 1;
    #0.001 $display("reinactive-no-reference %b", reinactive_no_notifier);

    #1 renba_data_first_data = 1;
    renba_data_first_trigger = 1;
    #0.001 $display("renba-data-first %b", renba_data_first_notifier);

    #1 renba_reference_first_reference = 1;
    renba_reference_first_trigger = 1;
    #0.001 $display("renba-reference-first %b",
                    renba_reference_first_notifier);

    #1 renba_no_reference = 1;
    #0.001 renba_no_reference = 0;
    #2.999 renba_no_trigger = 1;
    #0.001 $display("renba-no-reference %b", renba_no_notifier);

    #1 setup_data = 1;
    setup_trigger = 1;
    #0.001 $display("setup-reactive-simultaneous %b", setup_notifier);
    #1 hold_data = 1;
    hold_trigger = 1;
    #0.001 $display("hold-reactive-simultaneous %b", hold_notifier);
    #1 removal_data = 1;
    removal_trigger = 1;
    #0.001 $display("removal-reactive-simultaneous %b", removal_notifier);
    #1 recovery_data = 1;
    recovery_trigger = 1;
    #0.001 $display("recovery-reactive-simultaneous %b", recovery_notifier);

    #1 setuphold_data_first_data = 1;
    setuphold_data_first_trigger = 1;
    #0.001 $display("setuphold-data-first %b",
                    setuphold_data_first_notifier);
    #1 setuphold_reference_first_reference = 1;
    setuphold_reference_first_trigger = 1;
    #0.001 $display("setuphold-reference-first %b",
                    setuphold_reference_first_notifier);
    #1 recrem_data_first_data = 1;
    recrem_data_first_trigger = 1;
    #0.001 $display("recrem-data-first %b", recrem_data_first_notifier);
    #1 recrem_reference_first_reference = 1;
    recrem_reference_first_trigger = 1;
    #0.001 $display("recrem-reference-first %b",
                    recrem_reference_first_notifier);
    $finish;
  end
endmodule

// CHECK-DAG: reactive-data-first 0
// CHECK-DAG: reactive-reference-first 0
// CHECK-DAG: reactive-no-reference 1
// CHECK-DAG: reinactive-data-first 0
// CHECK-DAG: reinactive-reference-first 0
// CHECK-DAG: reinactive-no-reference 1
// CHECK-DAG: renba-data-first 0
// CHECK-DAG: renba-reference-first 0
// CHECK-DAG: renba-no-reference 1
// CHECK-DAG: setup-reactive-simultaneous 0
// CHECK-DAG: hold-reactive-simultaneous 1
// CHECK-DAG: removal-reactive-simultaneous 0
// CHECK-DAG: recovery-reactive-simultaneous 1
// CHECK-DAG: setuphold-data-first 1
// CHECK-DAG: setuphold-reference-first 1
// CHECK-DAG: recrem-data-first 1
// CHECK-DAG: recrem-reference-first 1
