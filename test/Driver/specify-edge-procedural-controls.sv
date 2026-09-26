// RUN: obelisk -O0 %s -o %t.native
// RUN: %t.native | FileCheck %s
// RUN: obelisk -O0 --execution-tier=bytecode %s -o %t.bytecode
// RUN: %t.bytecode | FileCheck %s
// RUN: obelisk -O0 -emit-sim %s -o %t.mlir
// RUN: FileCheck %s --check-prefix=SIM < %t.mlir

`timescale 1ns / 1ps

module event_list_control(input wire clock, reset, data, output logic q);
  always @(posedge clock or posedge reset)
    q = reset ? 0 : data;
  specify
    (posedge clock => (q +: data)) = 2;
  endspecify
endmodule

module delayed_control(input wire clock, data, output logic q);
  always @(posedge clock)
    #1 q = data;
  specify
    (posedge clock => (q +: data)) = 2;
  endspecify
endmodule

module specify_edge_procedural_controls;
  logic clock = 0;
  logic reset = 0;
  logic data = 1;
  wire event_q;
  wire delayed_q;

  event_list_control listed(clock, reset, data, event_q);
  delayed_control delayed(clock, data, delayed_q);

  initial begin
    #5 reset = 1;
    #0.001;
    if (event_q !== 0)
      $fatal(1, "unrelated event-list wake incorrectly used the clock path");
    reset = 0;

    #4.999 clock = 1;
    #0.999;
    if (event_q !== 0 || delayed_q !== 1'bx)
      $fatal(1, "procedural path destination matured early");
    #0.002;
    if (delayed_q !== 1'bx)
      $fatal(1, "procedural delay was added to the module-path delay");
    #0.998;
    if (event_q !== 0 || delayed_q !== 1'bx)
      $fatal(1, "procedural path destination matured before source+delay");
    #0.002;
    if (event_q !== 1 || delayed_q !== 1)
      $fatal(1, "event-list or delayed path did not mature");

    #1 reset = 1;
    #0.001;
    if (event_q !== 0)
      $fatal(1, "event-list cancellation write was not immediate");

    $display("PASSED");
    $finish;
  end
endmodule

// CHECK: PASSED
// SIM: obelisk_sim.time.now
// SIM: obelisk_sim.ref.store_inertial_path
