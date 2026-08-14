// RUN: obelisk -fno-lto %s -o %t.native
// RUN: %t.native > %t.native.out
// RUN: obelisk -fno-lto --execution-tier=bytecode %s -o %t.bytecode
// RUN: %t.bytecode > %t.bytecode.out
// RUN: diff -u %t.native.out %t.bytecode.out
// RUN: FileCheck %s < %t.native.out

`timescale 1ns/100ps
module real_delay_trace;
  initial begin
    #0.14 $display("real-014");
    #0.15 $display("real-015");
    #1ns $display("time-literal");
  end
endmodule

`timescale 100ps/1ps
module real_delay_markers;
  initial begin
    #2 $display("marker-02");
    #2 $display("marker-04");
    #8 $display("marker-12");
  end
endmodule

// The first process resumes at 0.1 ns, 0.3 ns, and 1.3 ns. The marker process
// makes those rounded times observable without depending on a $time subset.
// CHECK: real-014
// CHECK-NEXT: marker-02
// CHECK-NEXT: real-015
// CHECK-NEXT: marker-04
// CHECK-NEXT: marker-12
// CHECK-NEXT: time-literal
