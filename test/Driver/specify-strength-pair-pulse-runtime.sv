// RUN: obelisk -fno-lto -O0 %s -o %t.native-o0
// RUN: %t.native-o0 | FileCheck %s
// RUN: obelisk -fno-lto -O3 %s -o %t.native-o3
// RUN: %t.native-o3 | FileCheck %s
// RUN: obelisk -fno-lto -O0 --execution-tier=bytecode %s -o %t.bytecode-o0
// RUN: %t.bytecode-o0 | FileCheck %s
// RUN: obelisk -fno-lto -O3 --execution-tier=bytecode %s -o %t.bytecode-o3
// RUN: %t.bytecode-o3 | FileCheck %s
// RUN: obelisk -O0 -emit-sim %s -o %t.sim.mlir
// RUN: FileCheck %s --check-prefix=SIM < %t.sim.mlir

`timescale 1ns / 1ps

module strength_pulse_cell(input wire data, input wire enable,
                           output wire destination);
  bufif1 (strong1, pull0) gate(destination, data, enable);
  specify
    // IEEE 1800-2017 30.7 and 30.7.4.1: the module-path pulse limits
    // classify the logical primitive output while both strength banks remain
    // one atomic scheduled contribution.
    specparam PATHPULSE$ = (2, 4);
    pulsestyle_ondetect destination;
    (data, enable *> destination) = 5;
  endspecify
endmodule

module specify_strength_pair_pulse_runtime;
  logic data = 0;
  logic enable = 1;
  wire destination;
  strength_pulse_cell dut(data, enable, destination);

  initial begin
    #5.001;
    if (destination !== 0)
      $fatal(1, "initial strength-pair value did not settle");

    #4.999 data = 1;
    #3 data = 0;
    #0.001;
    if (destination !== 1'bx)
      $fatal(1, "pulse error did not publish an atomic unknown pair");
    #4.999;
    if (destination !== 0)
      $fatal(1, "pulse error did not restore the trailing strength pair");

    #2 data = 1;
    #1 data = 0;
    #6;
    if (destination !== 0)
      $fatal(1, "pulse below the reject limit was not rejected");

    #2 data = 1;
    #4 data = 0;
    #1;
    if (destination !== 1)
      $fatal(1, "pulse at the error limit did not propagate");
    #4;
    if (destination !== 0)
      $fatal(1, "propagated pulse did not retain its trailing pair");

    // Keep the passed leading pair live, then reject a reversal of its still
    // pending trailing pair. IEEE 1800-2017 30.7 cancels only that latest
    // leading event; it must not invalidate the older passed event.
    #2 data = 1;
    #4 data = 0;
    #0.5 data = 1;
    #0.501;
    if (destination !== 1)
      $fatal(1, "latest cancellation invalidated an older passed pair");

    $display("PASSED");
    $finish;
  end
endmodule

// CHECK: PASSED
// SIM: obelisk_sim.driver.drive_inertial_path_strength_pair
// SIM-SAME: group 0 of 1 pulse_transitions
// SIM: pulse_error = 4000 : i64
// SIM: pulse_on_detect = true
// SIM: pulse_reject = 2000 : i64
