// RUN: obelisk -O0 %s -o %t.o0
// RUN: obelisk -O3 %s -o %t.o3
// RUN: %t.o0 > %t.o0.out && %t.o3 > %t.o3.out
// RUN: diff -u %t.o0.out %t.o3.out
// RUN: FileCheck %s < %t.o3.out

module native_selective_wait;
  logic watched;
  logic unrelated;
  initial begin
    #1 unrelated = 1;
    #1 watched = 1;
  end
  initial begin
    @(watched);
    $display("woke watched=%b unrelated=%b", watched, unrelated);
  end
endmodule

// CHECK: woke watched=1 unrelated=1
