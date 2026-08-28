// RUN: obelisk -fno-lto -O0 %s -o %t.o0.native
// RUN: %t.o0.native > %t.o0.native.out
// RUN: obelisk -fno-lto -O0 --execution-tier=bytecode %s -o %t.o0.bytecode
// RUN: %t.o0.bytecode > %t.o0.bytecode.out
// RUN: diff -u %t.o0.native.out %t.o0.bytecode.out
// RUN: obelisk -fno-lto -O3 %s -o %t.o3.native
// RUN: %t.o3.native > %t.o3.native.out
// RUN: obelisk -fno-lto -O3 --execution-tier=bytecode %s -o %t.o3.bytecode
// RUN: %t.o3.bytecode > %t.o3.bytecode.out
// RUN: diff -u %t.o3.native.out %t.o3.bytecode.out
// RUN: diff -u %t.o0.native.out %t.o3.native.out
// RUN: FileCheck %s < %t.o3.native.out
// RUN: obelisk -O0 -emit-sim %s -o %t.sim.mlir
// RUN: FileCheck %s --check-prefix=SIM < %t.sim.mlir
// RUN: obelisk -O0 -emit-sim --pulse-reject-percent=20 \
// RUN:   --pulse-error-percent=60 --pulse-style=ondetect \
// RUN:   --cancelled-pulses=show %s -o %t.global.sim.mlir
// RUN: FileCheck %s --check-prefix=GLOBAL < %t.global.sim.mlir

`timescale 1ns / 1ps

module filtered_path(input wire source, output wire destination);
  assign destination = source;
  specify
    specparam PATHPULSE$ = (2, 4);
    pulsestyle_ondetect destination;
    (source => destination) = 5;
  endspecify
endmodule

module onevent_path(input wire source, output wire destination);
  assign destination = source;
  specify
    specparam PATHPULSE$ = (2, 4);
    pulsestyle_onevent destination;
    (source => destination) = 5;
  endspecify
endmodule

module negative_path(input wire source, output wire destination);
  assign destination = source;
  specify
    showcancelled destination;
    pulsestyle_ondetect destination;
    (source => destination) = (7, 2);
  endspecify
endmodule

module packed_style_path(input wire [2:0] source,
                         output wire [2:0] destination);
  assign destination = source;
  specify
    pulsestyle_ondetect destination[0];
    showcancelled destination[1];
    (source => destination) = 5;
  endspecify
endmodule

module path_specific_limits(input wire a, b, output wire ya, yb);
  assign ya = a;
  assign yb = b;
  specify
    // IEEE 1800-2017 30.7.1: a path-specific PATHPULSE$ takes precedence
    // over the module-wide declaration for the named path only.
    specparam PATHPULSE$a$ya = (1, 3), PATHPULSE$ = (2, 4);
    (a => ya) = 5;
    (b => yb) = 5;
  endspecify
endmodule

module specify_pulse_control;
  logic filtered_source = 0;
  logic onevent_source = 0;
  logic negative_source = 0;
  logic [2:0] packed_source = 0;
  logic specific_a = 0;
  logic specific_b = 0;
  wire filtered_destination;
  wire onevent_destination;
  wire negative_destination;
  wire [2:0] packed_destination;
  wire specific_ya;
  wire specific_yb;

  filtered_path filtered(filtered_source, filtered_destination);
  onevent_path onevent(onevent_source, onevent_destination);
  negative_path negative(negative_source, negative_destination);
  packed_style_path packed_inst(packed_source, packed_destination);
  path_specific_limits specific(specific_a, specific_b, specific_ya,
                                specific_yb);

  initial begin
    #5.001;
    if (filtered_destination !== 0 || onevent_destination !== 0 ||
        negative_destination !== 0 || packed_destination !== 0)
      $fatal(1, "initial path values did not mature");

    #4.999;
    filtered_source = 1;
    onevent_source = 1;
    negative_source = 1;
    #2 negative_source = 0;
    #0.001;
    if (negative_destination !== 1'bx)
      $fatal(1, "on-detect showcancelled did not expose a negative pulse");

    #0.999;
    filtered_source = 0;
    onevent_source = 0;
    #0.001;
    if (filtered_destination !== 1'bx || onevent_destination !== 0)
      $fatal(1, "on-detect and on-event filtering disagreed at detection");
    #1.999;
    if (onevent_destination !== 1'bx)
      $fatal(1, "on-event filtering did not replace the leading edge");
    #2;
    if (negative_destination !== 0)
      $fatal(1, "negative pulse did not restore its final value");
    #1;
    if (filtered_destination !== 0 || onevent_destination !== 0)
      $fatal(1, "filtered X pulse did not restore its trailing edge");

    #2 filtered_source = 1;
    #1 filtered_source = 0;
    #6;
    if (filtered_destination !== 0)
      $fatal(1, "pulse below the reject limit was not rejected");

    #3 filtered_source = 1;
    #4 filtered_source = 0;
    #1;
    if (filtered_destination !== 1)
      $fatal(1, "pulse at the error limit did not propagate");
    #4;
    if (filtered_destination !== 0)
      $fatal(1, "propagated pulse did not retain its trailing edge");

    $display("PASSED");
    $finish;
  end
endmodule

// CHECK: PASSED
// SIM: pulse_transitions
// SIM: pulse_error = 4000 : i64
// SIM: pulse_on_detect = true
// SIM: pulse_reject = 2000 : i64
// SIM: pulse_show_cancelled = true
// SIM: group 0 of 3 pulse_transitions
// SIM: group 1 of 3 pulse_transitions
// SIM: group 2 of 3 pulse_transitions
// SIM: pulse_error = 3000 : i64
// SIM: pulse_reject = 1000 : i64
// GLOBAL: pulse_error = 4000 : i64
// GLOBAL: pulse_on_detect = true
// GLOBAL: pulse_reject = 2000 : i64
// GLOBAL: pulse_show_cancelled = true
// GLOBAL: pulse_error = 3000 : i64
// GLOBAL: pulse_reject = 1000 : i64
