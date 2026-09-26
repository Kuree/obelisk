// RUN: obelisk -emit-sim %s -o - | FileCheck %s --check-prefix=SIM
// RUN: obelisk -O0 %s -o %t.native
// RUN: %t.native 2>&1 | FileCheck %s --check-prefix=OUT
// RUN: obelisk -O3 --native-scheduler=eval %s -o %t.aot
// RUN: %t.aot 2>&1 | FileCheck %s --check-prefix=OUT
// RUN: env OBELISK_RT_SIGNAL_DIAGNOSTICS=1 %t.aot > /dev/null 2> %t.diag
// RUN: FileCheck %s --check-prefix=AOTDIAG < %t.diag
// RUN: obelisk -O0 --execution-tier=bytecode %s -o %t.bytecode
// RUN: %t.bytecode 2>&1 | FileCheck %s --check-prefix=OUT

`timescale 1ns / 1ps

module negative_setuphold(
    input logic reference, data,
    output reg notifier = 0);
  specify
    $setuphold(posedge reference, posedge data, -2, 5, notifier);
  endspecify
endmodule

module system_timing_check_negative_runtime;
  logic periodic_clock = 0;
  int periodic_count = 0;
  reg period_notifier = 0;
  logic lower_reference = 0, lower_data = 0;
  logic inside_reference = 0, inside_data = 0;
  logic upper_reference = 0, upper_data = 0;
  wire lower_notifier, inside_notifier, upper_notifier;

  negative_setuphold lower_dut(lower_reference, lower_data, lower_notifier);
  negative_setuphold inside_dut(inside_reference, inside_data,
                                inside_notifier);
  negative_setuphold upper_dut(upper_reference, upper_data, upper_notifier);

  // This unrelated Clause 31.7 coordinator must retain its ordinary static
  // publication path when the Clause 31.9 component becomes hybrid.
  specify
    $period(posedge periodic_clock, 0.1, period_notifier);
  endspecify

  // Keep an unrelated static island available to the actor-local AOT tier.
  always #0.5 periodic_clock = ~periodic_clock;
  always @(posedge periodic_clock)
    periodic_count <= periodic_count + 1;

  initial begin
    #1;
    lower_reference = 1;
    inside_reference = 1;
    upper_reference = 1;
    #2 lower_data = 1;
    #0.001 inside_data = 1;
    #2.999 upper_data = 1;
    #0.001 $display("negative-open-window %b%b%b", lower_notifier,
                    inside_notifier, upper_notifier);
    $display("negative-periodic %0d", periodic_count);
    $finish;
  end
endmodule

// SIM-DAG: obelisk_sim.func private @__obelisk_negative_timing_monitor_
// SIM-DAG: obelisk_sim.suspend.change
// SIM-DAG: obelisk_sim.spawn @__obelisk_negative_timing_monitor_{{[0-9]+}}.$commit
// SIM-DAG: obelisk_sim.suspend.delay
// SIM-DAG: obelisk_sim.suspend.clock_set
// SIM-NOT: timing_check_table
// OUT: negative-open-window 010
// OUT-NEXT: negative-periodic 6
// AOTDIAG: obelisk-signal-diagnostics
// AOTDIAG-SAME: aot_node_executions={{[1-9][0-9]*}}
