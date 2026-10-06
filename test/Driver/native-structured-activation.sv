// RUN: obelisk -O3 --vpi=off -emit-sim %s -o %t.sim.mlir
// RUN: FileCheck %s --check-prefix=FUSION < %t.sim.mlir
// RUN: obelisk -O0 --vpi=off --execution-tier=native %s -o %t.o0
// RUN: %t.o0 | FileCheck %s
// RUN: obelisk -O3 --vpi=off --execution-tier=native %s -o %t.o3
// RUN: %t.o3 | FileCheck %s
// RUN: obelisk -O3 --vpi=off --execution-tier=bytecode %s -o %t.bytecode
// RUN: %t.bytecode | FileCheck %s

// A process-level cycle is a DAG when the copy and assembly loops are
// separate scheduling regions. The externally written upper nibble must
// remain sensitive after removing the internally produced lower nibble.
module native_structured_activation;
  logic clock = 0;
  logic [7:0] address, bank_address, bank_result, result, sampled;
  initial bank_result[7:4] = 0;
  always #1 clock = ~clock;
  always @(posedge clock) sampled <= result;
  always_comb begin
    for (int i = 0; i < 2; i++)
      bank_address[i * 4 +: 4] = address[i * 4 +: 4];
    result = 0;
    for (int j = 0; j < 2; j++)
      result[j * 4 +: 4] = bank_result[j * 4 +: 4];
  end
  always_comb bank_result[3:0] = bank_address + 1;
  initial begin
    #2 $display("result %h sample %h", result, sampled);
    address = 7;
    #2 $display("result %h sample %h", result, sampled);
    address = 'x;
    #2 $display("result %h sample %h", result, sampled);
    address = 'z;
    #2 $display("result %h sample %h", result, sampled);
    address = 255;
    #2 $display("result %h sample %h", result, sampled);
    address = 0;
    bank_result[7:4] = 10;
    #2 $display("result %h sample %h", result, sampled);
    bank_result[7:4] = 3;
    #2 $display("result %h sample %h", result, sampled);
    $finish;
  end
endmodule

// FUSION: simulation.func private @__obelisk_region_kernel_
// CHECK: result 0x sample 0x
// CHECK-NEXT: result 08 sample 08
// CHECK-NEXT: result 0x sample 0x
// CHECK-NEXT: result 0x sample 0x
// CHECK-NEXT: result 00 sample 00
// CHECK-NEXT: result a1 sample a1
// CHECK-NEXT: result 31 sample 31
