// RUN: obelisk -fno-lto -O0 %s -o %t.native
// RUN: %t.native | FileCheck %s
// RUN: obelisk -fno-lto -O0 --execution-tier=bytecode %s -o %t.bytecode
// RUN: %t.bytecode | FileCheck %s
// RUN: obelisk -fno-lto -O3 --native-scheduler=aot %s -o %t.aot
// RUN: %t.aot | FileCheck %s

`timescale 1ns / 1ps

module system_timing_check_condition_event_alias;
  logic data = 0, reference = 0;
  reg bare_notifier = 0, eq_notifier = 0;
  reg not_notifier = 0, eq_zero_notifier = 0;

  specify
    // IEEE 1800-2017 31.7 samples the post-transition conditioning value.
    // When the condition aliases the controlled event, every tier must use
    // its exact post-transition publication rather than a stale live mirror.
    $setup(posedge data, posedge reference &&& reference, 3, bare_notifier);
    $setup(posedge data, posedge reference &&& (reference == 1), 3,
           eq_notifier);
    $setup(posedge data, negedge reference &&& (~reference), 3, not_notifier);
    $setup(posedge data, negedge reference &&& (reference == 0), 3,
           eq_zero_notifier);
  endspecify

  initial begin
    #1 data = 1;
    #2 reference = 1;
    #0.001;
    $display("alias-rise %b %b", bare_notifier, eq_notifier);

    data = 0;
    #1 data = 1;
    #2 reference = 0;
    #0.001;
    $display("alias-fall %b %b", not_notifier, eq_zero_notifier);
    $finish;
  end
endmodule

// CHECK: alias-rise 1 1
// CHECK-NEXT: alias-fall 1 1
