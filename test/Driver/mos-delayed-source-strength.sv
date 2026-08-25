// RUN: obelisk -fno-lto -O0 --vpi=off %s -o %t.o0.native
// RUN: %t.o0.native | FileCheck %s
// RUN: obelisk -fno-lto -O0 --vpi=off --execution-tier=bytecode %s -o %t.o0.bytecode
// RUN: %t.o0.bytecode | FileCheck %s
// RUN: obelisk -fno-lto -O3 --vpi=off %s -o %t.o3.native
// RUN: %t.o3.native | FileCheck %s
// RUN: obelisk -fno-lto -O3 --vpi=off --execution-tier=bytecode %s -o %t.o3.bytecode
// RUN: %t.o3.bytecode | FileCheck %s

`timescale 1ns/1ns

// Exact source binding and delay-tuple conformance. The hand-authored
// Simulation test covers the scheduler and frozen-edge contract directly.
module mos_delayed_source_strength;
  logic supply_enabled = 1;
  logic source_value = 1;
  logic control = 1'bx;
  wire source;
  assign (supply1, supply0) source = supply_enabled ? source_value : 1'bz;
  assign (pull1, pull0) source = source_value;

  wire out, resistive_out, one_delay, two_delay, zero_delay, blocked;
  nmos #(2, 3, 5) m0(out, source, control);
  rnmos #(2, 3, 5) m1(resistive_out, source, control);
  nmos #(2) m_one(one_delay, source, control);
  nmos #(2, 3) m_two(two_delay, source, control);
  nmos #(0, 0, 0) m_zero(zero_delay, source, control);
  assign (supply0, supply1) blocked = 1'b0;
  nmos #(2, 3, 5) one_way(blocked, source, control);

  initial begin
    #1 control = 1;
    #1 supply_enabled = 0;
    #1;
    $display("t3 pending %v %v", out, blocked);
    #1;
    $display("t4 strength %v %v %v", source, out, resistive_out);
    #1 source_value = 0;
    #1 source_value = 1;
    #1 control = 0;
    #1;
    $display("t8 forms-pending %v %v %v %v", out, one_delay, two_delay,
             zero_delay);
    #3;
    $display("t11 turnoff-pending %v", out);
    #1;
    $display("t12 off %v %v %v %v", out, one_delay, two_delay, blocked);
  end
endmodule

// CHECK: t3 pending HiZ Su0
// CHECK: t4 strength Pu1 Pu1 We1
// CHECK: t8 forms-pending Pu1 Pu1 Pu1 HiZ
// CHECK: t11 turnoff-pending Pu1
// CHECK: t12 off HiZ HiZ HiZ Su0
