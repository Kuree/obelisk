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

`timescale 1ns/1ps

module native_net_alias_observer_child(output wire value);
  logic drive;

  initial begin
    drive = 0;
    #1 drive = 1;
  end

  assign value = drive;
endmodule

module native_net_alias_computed_observers;
  wire connected;
  logic top_first;
  logic child_first;
  logic top_second;
  logic child_second;
  logic top_woke;
  logic child_woke;

  native_net_alias_observer_child child(connected);

  initial begin
    top_first = 1;
    child_first = 0;
    #0;
    @(posedge (connected & top_first) or
      posedge (child.value & child_first));
    top_woke = 1;
  end

  initial begin
    top_second = 0;
    child_second = 1;
    #0;
    @(posedge (connected & top_second) or
      posedge (child.value & child_second));
    child_woke = 1;
  end

  initial begin
    top_woke = 0;
    child_woke = 0;
    #2;
    $display("alias-clauses=%b%b", top_woke, child_woke);
    $finish;
  end
endmodule

// CHECK: alias-clauses=11
