// RUN: obelisk -O0 %s -o %t.o0.native
// RUN: obelisk -O3 %s -o %t.o3.native
// RUN: obelisk -O0 --execution-tier=bytecode %s -o %t.o0.bytecode
// RUN: obelisk -O3 --execution-tier=bytecode %s -o %t.o3.bytecode
// RUN: %t.o0.native > %t.o0.native.out
// RUN: %t.o3.native > %t.o3.native.out
// RUN: %t.o0.bytecode > %t.o0.bytecode.out
// RUN: %t.o3.bytecode > %t.o3.bytecode.out
// RUN: diff -u %t.o0.native.out %t.o3.native.out
// RUN: diff -u %t.o0.native.out %t.o0.bytecode.out
// RUN: diff -u %t.o0.native.out %t.o3.bytecode.out
// RUN: FileCheck %s < %t.o0.native.out

// IEEE 1800-2017 10.6: a nonconstant force or procedural continuous
// assignment RHS is continuously reevaluated until release or deassign.
`timescale 1ns/1ps
module native_procedural_override_reevaluation;
  logic [7:0] source;
  logic [7:0] alternate;
  logic [7:0] variable;
  logic [7:0] driver;
  wire [7:0] driven = driver;
  real real_source;
  real real_variable;

  function automatic logic [7:0] transform(input logic [7:0] value);
    return value + 3;
  endfunction

  initial begin
    source = 1;
    alternate = 8'ha5;
    variable = 0;
    driver = 8'h80;

    force variable = source + 1;
    $display("force-initial=%0d", variable);
    source = 5;
    #1;
    $display("force-updated=%0d", variable);
    variable = 99;
    $display("force-masked=%0d", variable);
    release variable;

    real_source = 1.5;
    real_variable = 0.0;
    force real_variable = real_source * 2.0;
    $display("real-initial=%0.1f", real_variable);
    real_source = 2.25;
    #1;
    $display("real-updated=%0.1f", real_variable);
    release real_variable;
    real_source = 4.0;
    #1;
    $display("real-retired=%0.1f", real_variable);
    $display("force-released=%0d", variable);
    source = 7;
    #1;
    $display("force-retired=%0d", variable);

    assign variable = source ^ 8'h03;
    #1;
    $display("assign-initial=%0d", variable);
    source = 9;
    #1;
    $display("assign-updated=%0d", variable);
    variable = 42;
    $display("assign-masked=%0d", variable);
    deassign variable;
    $display("assign-retained=%0d", variable);
    source = 10;
    #1;
    $display("assign-retired=%0d", variable);

    force driven = source;
    $display("net-initial=%0d", driven);
    source = 12;
    #1;
    $display("net-updated=%0d", driven);
    driver = 8'h55;
    $display("net-masked=%0d", driven);
    release driven;
    $display("net-released=%0d", driven);
    source = 13;
    #1;
    $display("net-retired=%0d", driven);

    driver = 8'h8c;
    source = 8'h31;
    alternate = 8'ha5;
    force driven = source;
    force driven[3:0] = alternate[3:0];
    #1;
    $display("overlap-initial=%h", driven);
    source = 8'h42;
    alternate = 8'hb6;
    #1;
    $display("overlap-updated=%h", driven);
    release driven[3:0];
    source = 8'h53;
    alternate = 8'hc7;
    #1;
    $display("overlap-released=%h", driven);
    release driven;

    source = 8'h11;
    force variable = source;
    force variable = 8'h55;
    source = 8'h22;
    #1;
    $display("replacement-retired=%h", variable);
    release variable;

    source = 8'h20;
    force variable = transform(source);
    source = 8'h30;
    #1;
    $display("call-updated=%h", variable);
    release variable;
  end
endmodule

// CHECK: force-initial=2
// CHECK-NEXT: force-updated=6
// CHECK-NEXT: force-masked=6
// CHECK-NEXT: real-initial=3.0
// CHECK-NEXT: real-updated=4.5
// CHECK-NEXT: real-retired=4.5
// CHECK-NEXT: force-released=6
// CHECK-NEXT: force-retired=6
// CHECK-NEXT: assign-initial=4
// CHECK-NEXT: assign-updated=10
// CHECK-NEXT: assign-masked=10
// CHECK-NEXT: assign-retained=10
// CHECK-NEXT: assign-retired=10
// CHECK-NEXT: net-initial=10
// CHECK-NEXT: net-updated=12
// CHECK-NEXT: net-masked=12
// CHECK-NEXT: net-released=128
// CHECK-NEXT: net-retired=85
// CHECK-NEXT: overlap-initial=35
// CHECK-NEXT: overlap-updated=46
// CHECK-NEXT: overlap-released=5c
// CHECK-NEXT: replacement-retired=55
// CHECK-NEXT: call-updated=33
