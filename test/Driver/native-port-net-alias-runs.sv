// RUN: obelisk -O0 %s -o %t.o0.native
// RUN: %t.o0.native > %t.o0.native.out
// RUN: obelisk -O0 --execution-tier=bytecode %s -o %t.o0.bytecode
// RUN: %t.o0.bytecode > %t.o0.bytecode.out
// RUN: diff -u %t.o0.native.out %t.o0.bytecode.out
// RUN: obelisk -O3 %s -o %t.o3.native
// RUN: %t.o3.native > %t.o3.native.out
// RUN: obelisk -O3 --execution-tier=bytecode %s -o %t.o3.bytecode
// RUN: %t.o3.bytecode > %t.o3.bytecode.out
// RUN: diff -u %t.o0.native.out %t.o3.native.out
// RUN: FileCheck %s < %t.o3.native.out

// Two independent port aliases whose nets land adjacent in the state layout
// with a matching stride: `w1`/`s.y`+`s.c` and `w2`/`s.z`. Coalescing the
// scalar connectivity edges into runs must stop at the net boundary, or the
// image carries one record spanning two nets and the loader rejects it.

`timescale 1ns/1ps

module port_net_alias_runs_child(
    output wire [31:0] y,
    input wire [31:0] c,
    output wire [31:0] z);
  assign y = 32'd5;
  assign z = c + 32'd1;
endmodule

module port_net_alias_runs;
  wire [31:0] w1;
  wire [31:0] w2;

  port_net_alias_runs_child child(.y(w1), .c(w1), .z(w2));

  initial begin
    #1;
    // CHECK: w1=5 w2=6
    $display("w1=%0d w2=%0d", w1, w2);
    $finish;
  end
endmodule
