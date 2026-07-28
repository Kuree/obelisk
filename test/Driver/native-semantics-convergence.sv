// RUN: obelisk -O0 %s -o %t.o0
// RUN: obelisk -O3 %s -o %t.o3
// RUN: %t.o0 > %t.o0.out && %t.o3 > %t.o3.out
// RUN: diff -u %t.o0.out %t.o3.out
// RUN: FileCheck %s < %t.o3.out

module native_convergence;
  logic source;
  logic middle;
  logic destination;
  always_comb middle = source;
  always_comb destination = middle;
  initial begin
    source = 1;
    #1;
    $display("converged=%b", destination);
  end
endmodule

// CHECK: converged=1
