// RUN: obelisk -O0 --vpi=off %s -o %t.o0
// RUN: %t.o0 | FileCheck %s
// RUN: obelisk -O3 --vpi=off %s -o %t.o3
// RUN: %t.o3 | FileCheck %s
// RUN: obelisk -O0 --vpi=off --execution-tier=bytecode %s -o %t.bc0
// RUN: %t.bc0 | FileCheck %s
// RUN: obelisk -O3 --vpi=off --execution-tier=bytecode %s -o %t.bc3
// RUN: %t.bc3 | FileCheck %s

// IEEE 1800-2023 6.8, 11.4.6, 11.4.10, 11.5.1, 12.7.1.
module optimization_lrm_2023;
  logic [15:0] configuration = 16'h12z4;
  int integer_configuration = 'h1234;
  wire #2 delayed_one = 1'b1;
  int sum, descending, modular;
  byte unsigned index;

  initial begin
    sum = 0;
    for (int i = 0; i < 4; i++) sum += i;
    descending = 0;
    for (int i = 5; i > -1; i += -2) descending += i;
    modular = 0;
    for (index = 254; index != 2; index += 2) modular++;
    $display("loops=%0d,%0d,%0d", sum, descending, modular);
    $display("slices=%h,%h", configuration[7:4], integer_configuration[11:4]);
    $display("wildcard=%b shift=%h", configuration ==? 16'hxxxx,
             configuration >> 16);
    #1;
    $display("before=%b", delayed_one);
    #2;
    $display("after=%b", delayed_one);
  end

  // CHECK: loops=6,9,2
  // CHECK: slices=z,23
  // CHECK: wildcard=1 shift=0000
  // CHECK: before=z
  // CHECK: after=1
endmodule
