// RUN: obelisk -emit-sim %s -o - | FileCheck %s --check-prefix=SIM
// RUN: obelisk -O0 %s -o %t.o0.native
// RUN: %t.o0.native | FileCheck %s
// RUN: obelisk -O0 --execution-tier=bytecode %s -o %t.o0.bytecode
// RUN: %t.o0.bytecode | FileCheck %s
// RUN: obelisk -O3 %s -o %t.o3.native
// RUN: %t.o3.native | FileCheck %s
// RUN: obelisk -O3 --execution-tier=bytecode %s -o %t.o3.bytecode
// RUN: %t.o3.bytecode | FileCheck %s

`timescale 1ns / 1ps

module basic_timing_checks(
    input wire setup_data, setup_reference,
    input wire hold_reference, hold_data,
    input wire recovery_reference, recovery_data,
    input wire removal_reference, removal_data,
    output reg setup_notifier = 0,
    output reg hold_notifier = 0,
    output reg recovery_notifier = 0,
    output reg removal_notifier = 0);
  specify
    $setup(posedge setup_data, posedge setup_reference, 3, setup_notifier);
    $hold(posedge hold_reference, posedge hold_data, 3, hold_notifier);
    $recovery(posedge recovery_reference, posedge recovery_data, 3,
              recovery_notifier);
    $removal(posedge removal_reference, posedge removal_data, 3,
             removal_notifier);
  endspecify
endmodule

module notifier_corner_checks(
    input wire reference, data,
    output reg x_notifier = 1'bx,
    output reg z_notifier = 1'bz,
    output reg zero_notifier = 1'b0);
  specify
    $hold(posedge reference, posedge data, 3, x_notifier);
    $hold(posedge reference, posedge data, 3, z_notifier);
    $hold(posedge reference, posedge data, 0, zero_notifier);
  endspecify
endmodule

module system_timing_check_basic_runtime;
  logic setup_data = 0, setup_reference = 0;
  logic hold_reference = 0, hold_data = 0;
  logic recovery_reference = 0, recovery_data = 0;
  logic removal_reference = 0, removal_data = 0;
  logic corner_reference = 0, corner_data = 0;
  wire setup_notifier, hold_notifier, recovery_notifier, removal_notifier;
  wire x_notifier, z_notifier, zero_notifier;

  basic_timing_checks dut(
      setup_data, setup_reference, hold_reference, hold_data,
      recovery_reference, recovery_data, removal_reference, removal_data,
      setup_notifier, hold_notifier, recovery_notifier, removal_notifier);
  notifier_corner_checks corner(
      corner_reference, corner_data,
      x_notifier, z_notifier, zero_notifier);

  initial begin
    #1 setup_data = 1;
    #2 setup_reference = 1;
    #0.001 $display("setup-violation %b", setup_notifier);

    setup_data = 0;
    setup_reference = 0;
    #1 setup_data = 1;
    #3 setup_reference = 1;
    #0.001 $display("setup-endpoint %b", setup_notifier);

    setup_data = 0;
    setup_reference = 0;
    #1;
    setup_data = 1;
    setup_reference = 1;
    #0.001 $display("setup-simultaneous %b", setup_notifier);

    #1 hold_reference = 1;
    #2 hold_data = 1;
    #0.001 $display("hold-violation %b", hold_notifier);

    hold_reference = 0;
    hold_data = 0;
    #1;
    hold_reference = 1;
    hold_data = 1;
    #0.001 $display("hold-simultaneous %b", hold_notifier);

    hold_reference = 0;
    hold_data = 0;
    #1 hold_reference = 1;
    #3 hold_data = 1;
    #0.001 $display("hold-endpoint %b", hold_notifier);

    #1 recovery_reference = 1;
    #2 recovery_data = 1;
    #0.001 $display("recovery-violation %b", recovery_notifier);

    #1 removal_data = 1;
    #2 removal_reference = 1;
    #0.001 $display("removal-violation %b", removal_notifier);

    removal_data = 0;
    removal_reference = 0;
    #1;
    removal_data = 1;
    removal_reference = 1;
    #0.001 $display("removal-simultaneous %b", removal_notifier);

    #1 corner_reference = 1;
    #1 corner_data = 1;
    #0.001 $display("notifier-corners %b %b %b",
                    x_notifier, z_notifier, zero_notifier);
    $finish;
  end
endmodule

// SIM: simulation.suspend.clock_set
// SIM: simulation.assert.clock_occurrence.consume
// SIM-NOT: timing_check_table
// CHECK: setup-violation 1
// CHECK-NEXT: setup-endpoint 1
// CHECK-NEXT: setup-simultaneous 1
// CHECK-NEXT: hold-violation 1
// CHECK-NEXT: hold-simultaneous 0
// CHECK-NEXT: hold-endpoint 0
// CHECK-NEXT: recovery-violation 1
// CHECK-NEXT: removal-violation 1
// CHECK-NEXT: removal-simultaneous 1
// CHECK-NEXT: notifier-corners 0 z 0
