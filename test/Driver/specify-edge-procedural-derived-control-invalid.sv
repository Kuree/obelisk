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

module derived_control(input wire clock, enable, data, output logic q);
  always @(posedge (clock & enable))
    q = data;
  specify
    (posedge clock => (q +: data)) = 2;
  endspecify
endmodule

module derived_conditional_control(input wire clock, enable, qualify, data,
                                   output logic q);
  always @(posedge (clock & enable))
    q = data;
  specify
    if (qualify) (posedge clock => (q +: data)) = 2;
  endspecify
endmodule

module specify_edge_procedural_derived_control;
  logic clock = 0;
  logic enable = 0;
  logic data = 0;
  logic q;
  logic conditional_clock = 0;
  logic conditional_enable = 0;
  logic conditional_qualify = 1'bx;
  logic conditional_data = 1;
  logic conditional_q;
  derived_control dut(clock, enable, data, q);
  derived_conditional_control conditional_dut(
      conditional_clock, conditional_enable, conditional_qualify,
      conditional_data, conditional_q);

  initial begin
    // A source edge hidden by the derived primary must not leave stale
    // qualification for a later-time enable-caused wake.
    #1 clock = 1;
    #1 clock = 0;
    #1 data = 1;
    enable = 1;
    #1 clock = 1;
    #1 $display("qualified-pending %b", q);
    #1.001 $display("qualified-done %b", q);

    clock = 0;
    enable = 0;
    data = 0;
    #1 clock = 1;
    #1 enable = 1;
    #0 $display("later-wake-immediate %b", q);

    // IEEE 1800-2017 30.4.3 qualifies from the declared clock edge. The
    // observer records it even though clock&enable does not change; a #0
    // enable transition in the same scheduler epoch consumes that pending
    // qualification and retains the module-path delay.
    clock = 0;
    enable = 0;
    data = 1;
    #1 clock = 1;
    #0 enable = 1;
    #0 $display("same-time-pending %b", q);
    #2.001 $display("same-time-done %b", q);

    // IEEE 1800-2017 30.4.4.1 treats X as true, and 30.5.3 samples that path
    // condition with the source event, not with the later derived wake.
    #1 conditional_clock = 1;
    #0 conditional_qualify = 0;
    conditional_enable = 1;
    #0 $display("sampled-condition-pending %b", conditional_q);
    #2.001 $display("sampled-condition-done %b", conditional_q);
    $finish;
  end
endmodule

// SIM: procedural_wake_kind = 2 : i32
// SIM: simulation.suspend.observe
// SIM-COUNT-1: simulation.ref.store_inertial_path
// SIM: simulation.timing_path_monitor_rules
// CHECK: qualified-pending x
// CHECK-NEXT: qualified-done 1
// CHECK-NEXT: later-wake-immediate 0
// CHECK-NEXT: same-time-pending 0
// CHECK-NEXT: same-time-done 1
// CHECK-NEXT: sampled-condition-pending x
// CHECK-NEXT: sampled-condition-done 1
