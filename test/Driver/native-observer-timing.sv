// RUN: obelisk -fno-lto %s -o %t.native
// RUN: %t.native > %t.native.out
// RUN: obelisk -fno-lto --execution-tier=bytecode %s -o %t.bytecode
// RUN: %t.bytecode > %t.bytecode.out
// RUN: diff -u %t.native.out %t.bytecode.out
// RUN: FileCheck %s < %t.native.out

`timescale 1ns/1ps
module native_observer_timing;
  logic [7:0] bus;
  logic clock;
  logic enable;
  logic ready;

  initial begin
    bus = 0;
    clock = 0;
    enable = 0;
    ready = 0;
    #1 bus = 8'h80;
    #1 bus = 8'h81;
  end

  initial begin
    @(posedge bus);
    $display("vector-edge=%h", bus);
  end

  initial begin
    #3;
    enable = 1;
    clock = 1;
    enable = 0;
  end

  initial begin
    @(posedge clock iff enable);
    $display("iff-snapshot=%0d", enable);
  end

  initial begin
    #4;
    ready = 1;
    ready = 0;
  end

  initial begin
    wait (ready);
    $display("wait-transient=%0d", ready);
  end

  initial begin
    wait (1)
      $display("wait-true");
  end

  initial begin
    wait (0)
      $display("wait-false-unreachable");
  end
endmodule

// CHECK: wait-true
// CHECK-NEXT: vector-edge=81
// CHECK-NEXT: iff-snapshot=0
// CHECK-NEXT: wait-transient=0
// CHECK-NOT: wait-false-unreachable
