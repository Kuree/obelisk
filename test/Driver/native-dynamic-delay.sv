// RUN: obelisk %s -o %t.native
// RUN: %t.native > %t.native.out
// RUN: obelisk --execution-tier=bytecode %s -o %t.bytecode
// RUN: %t.bytecode > %t.bytecode.out
// RUN: diff -u %t.native.out %t.bytecode.out
// RUN: FileCheck %s < %t.native.out

`timescale 1ns/1ps
module native_dynamic_delay;
  byte unsigned delay8;
  int unsigned delay32;
  longint unsigned delay64;

  initial begin
    delay8 = 1;
    delay32 = 2;
    delay64 = 3;
    #(delay8) $display("delay8");
    #(delay32) $display("delay32");
    #(delay64) $display("delay64");
  end
endmodule

// CHECK: delay8
// CHECK-NEXT: delay32
// CHECK-NEXT: delay64
