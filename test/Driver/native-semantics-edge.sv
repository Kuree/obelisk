// RUN: obelisk -O0 %s -o %t.o0
// RUN: obelisk -O3 %s -o %t.o3
// RUN: %t.o0 > %t.o0.out && %t.o3 > %t.o3.out
// RUN: diff -u %t.o0.out %t.o3.out
// RUN: FileCheck %s < %t.o3.out

module native_edge_wait;
  logic [1:0] watched;
  logic unrelated;
  initial begin
    watched = 2'b00;
    #1 unrelated = 1;
    #1 watched[0] = 1;
    #1 watched[1] = 1;
  end
  initial begin
    @(posedge watched[1]);
    $display("posedge watched=%b", watched[1]);
  end
endmodule

// CHECK: posedge watched=1
