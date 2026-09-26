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

`timescale 1ns / 1ps

module packed_parallel_path(input wire [3:0] source,
                            output wire [3:0] destination);
  assign destination = source;
  specify
    (source[3:0] => destination[3:0]) = (1, 1);
  endspecify
endmodule

module multisource_full_path(input wire lhs, input wire rhs,
                             output wire destination);
  or (destination, lhs, rhs);
  specify
    (lhs, rhs *> destination) = 0.4;
  endspecify
endmodule

module specify_packed_full_path;
  logic [3:0] source;
  logic lhs;
  logic rhs;
  wire [3:0] packed_delayed;
  wire full_delayed;

  packed_parallel_path packed_path(source, packed_delayed);
  multisource_full_path full_path(lhs, rhs, full_delayed);

  initial begin
    source = 0;
    lhs = 0;
    rhs = 0;
    #0.399;
    if ((full_delayed !== 1'bx && full_delayed !== 1'bz) ||
        (packed_delayed !== 4'bxxxx && packed_delayed !== 4'bzzzz))
      $fatal(1, "initial full/vector values changed early: %b %b",
             full_delayed, packed_delayed);
    #0.002;
    if (full_delayed !== 0)
      $fatal(1, "full path did not use the declared delay");
    #0.599;
    if (packed_delayed !== 0)
      $fatal(1, "packed path did not update as one delayed driver");

    source = 4'h9;
    #0.999;
    if (packed_delayed !== 0)
      $fatal(1, "packed path changed too early");
    #0.002;
    if (packed_delayed !== 4'h9)
      $fatal(1, "packed path did not use the declared delay");

    lhs = 1;
    #0.399 lhs = 0;
    #0.002;
    if (full_delayed !== 0)
      $fatal(1, "full path did not reject an inertial pulse");

    lhs = 1;
    #0.200 rhs = 1;
    #0.201;
    if (full_delayed !== 1)
      $fatal(1, "full path rescheduled the shared transition");

    $display("PASSED");
    $finish;
  end
endmodule

// CHECK: PASSED
// SIM-COUNT-2: obelisk_sim.driver.drive_inertial
