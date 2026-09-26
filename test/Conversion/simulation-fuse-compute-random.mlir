// RUN: %split-file %s %t
// RUN: obelisk -O3 -emit-sim %t/random.sv -o %t/random.mlir
// RUN: FileCheck %s --check-prefix=REJECTED < %t/random.mlir

// Runtime behavior is checked in ../Runtime/simulation-fuse-compute-random.test.

// REJECTED-NOT: __obelisk_fused_

//--- random.sv
module process_random;
  bit clock;
  int a;
  int b;

  always @(posedge clock)
    a = $urandom;

  always @(posedge clock)
    b = $urandom;

  initial begin
    #1 clock = 1;
    #1;
    $display("%0d %0d", a, b);
    $finish;
  end
endmodule
