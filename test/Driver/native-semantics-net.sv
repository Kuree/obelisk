// RUN: obelisk -O0 %s -o %t.o0
// RUN: obelisk -O3 %s -o %t.o3
// RUN: %t.o0 > %t.o0.out && %t.o3 > %t.o3.out
// RUN: diff -u %t.o0.out %t.o3.out
// RUN: FileCheck %s < %t.o3.out

module native_net;
  logic first;
  logic second;
  wire destination;
  assign destination = first;
  assign destination = second;
  initial begin
    first = 0;
    second = 1;
    #1;
    $display("net=%b", destination);
  end
endmodule

// CHECK: net=x
