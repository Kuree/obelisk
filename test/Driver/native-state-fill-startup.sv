// RUN: obelisk -O0 %s -o %t.o0
// RUN: %t.o0 | FileCheck %s
// RUN: obelisk -O3 -fno-lto --compile-threads=8 %s -o %t.native
// RUN: %t.native | FileCheck %s
// RUN: obelisk -O3 --native-scheduler=generic %s -o %t.generic
// RUN: %t.generic | FileCheck %s
// RUN: obelisk -O3 --execution-tier=bytecode %s -o %t.bytecode
// RUN: %t.bytecode | FileCheck %s
// CHECK: state defaults passed

module native_state_fill_startup;
  bit [12:0] known;
  logic [130:0] uninitialized;
  wire [8:0] undriven;
  tri0 [2:0] pull_zero;
  tri1 [4:0] pull_one;
  supply0 supply_zero;
  supply1 supply_one;
  trireg charge;
  initial begin
    if (known !== '0 || uninitialized !== 'x || undriven !== 'z ||
        pull_zero !== '0 || pull_one !== '1 || supply_zero !== 1'b0 ||
        supply_one !== 1'b1 || charge !== 1'bx)
      $fatal(1, "initial state");
    uninitialized = '1;
    #1;
    if (uninitialized !== '1 || undriven !== 'z || charge !== 1'bx)
      $fatal(1, "state reset after startup");
    $display("state defaults passed");
    $finish;
  end
endmodule
