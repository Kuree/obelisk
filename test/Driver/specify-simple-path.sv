// RUN: obelisk -O0 %s -o %t.o0.native
// RUN: %t.o0.native > %t.o0.native.out
// RUN: obelisk -O0 --execution-tier=bytecode %s -o %t.o0.bytecode
// RUN: %t.o0.bytecode > %t.o0.bytecode.out
// RUN: diff -u %t.o0.native.out %t.o0.bytecode.out
// RUN: obelisk -O3 %s -o %t.o3.native
// RUN: %t.o3.native > %t.o3.native.out
// RUN: obelisk -O3 --execution-tier=bytecode %s -o %t.o3.bytecode
// RUN: %t.o3.bytecode > %t.o3.bytecode.out
// RUN: diff -u %t.o3.native.out %t.o3.bytecode.out
// RUN: diff -u %t.o0.native.out %t.o3.native.out
// RUN: FileCheck %s < %t.o3.native.out
// RUN: obelisk -O0 -emit-sim %s -o %t.sim.mlir
// RUN: FileCheck %s --check-prefix=SIM < %t.sim.mlir

`timescale 1ns / 1ns

module rise_fall_path(input wire source, output wire destination);
  assign destination = source;
  specify
    specparam rise_delay = 1:2:3;
    specparam fall_delay = 2:3:4;
    (source => destination) = (rise_delay, fall_delay);
  endspecify
endmodule

module turnoff_path(input wire turnoff_enable, output wire destination);
  assign destination = turnoff_enable ? 1'bz : 1'b0;
  specify
    (turnoff_enable => destination) = (2, 3, 4);
  endspecify
endmodule

module specify_simple_path;
  logic source;
  logic turnoff_enable;
  wire delayed;
  wire delayed_z;

  rise_fall_path rise_fall(source, delayed);
  turnoff_path turnoff(turnoff_enable, delayed_z);

  initial begin
    source = 0;
    turnoff_enable = 0;
    #3;
    if (delayed !== 0 || delayed_z !== 0)
      $fatal(1, "initial path values were not delayed correctly");
    #1 source = 1;
    #1;
    if (delayed !== 0)
      $fatal(1, "rise path changed too early");
    #1;
    if (delayed !== 1)
      $fatal(1, "rise path did not use the rise delay");
    #1 source = 0;
    #2;
    if (delayed !== 1)
      $fatal(1, "fall path changed too early");
    #1;
    if (delayed !== 0)
      $fatal(1, "fall path did not use the fall delay");
    #1 turnoff_enable = 1;
    #3;
    if (delayed_z !== 0)
      $fatal(1, "turnoff path changed too early");
    #1;
    if (delayed_z !== 1'bz)
      $fatal(1, "turnoff path did not use the turnoff delay");
    #1 turnoff_enable = 0;
    #2;
    if (delayed_z !== 1'bz)
      $fatal(1, "fall from high impedance changed too early");
    #1;
    if (delayed_z !== 0)
      $fatal(1, "fall from high impedance did not use the fall delay");
    $display("PASSED");
    $finish;
  end
endmodule

// CHECK: PASSED
// SIM-COUNT-2: obelisk_sim.driver.drive_inertial
