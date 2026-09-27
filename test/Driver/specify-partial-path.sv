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

module partial_paths(input wire [0:3] source,
                     output wire [3:0] destination);
  // source[0:1] and destination[1:0] have opposite declared directions but
  // identical physical LSB-first positional ordering. Both high destination
  // bits depend on either source[2:3] bit, making full-path broadcast visible.
  assign destination = {{2{^source[2:3]}}, source[0:1]};
  specify
    (source[0:1] => destination[1:0]) = 2;
    (source[2:3] *> destination[3:2]) = 1;
  endspecify
endmodule

module specify_partial_path;
  logic [0:3] source;
  wire [3:0] destination;

  partial_paths dut(source, destination);

  initial begin
    source = '0;
    #3;
    if (destination !== 4'b0000)
      $fatal(1, "initial partial paths did not settle");

    source[1] = 1'b1;
    #1;
    if (destination !== 4'b0000)
      $fatal(1, "reversed parallel path changed too early");
    #1;
    if (destination !== 4'b0001)
      $fatal(1, "reversed parallel path mapped the wrong bit");

    source[2] = 1'b1;
    #1;
    if (destination !== 4'b1101)
      $fatal(1, "full partial path did not broadcast to its destination");

    $display("PASSED");
    $finish;
  end
endmodule

// CHECK: PASSED
// SIM-COUNT-2: simulation.driver.drive_inertial_path
