// RUN: obelisk -O2 --vpi=off -emit-sim %s -o %t.sim.mlir
// RUN: FileCheck %s --check-prefix=LOOP < %t.sim.mlir
// RUN: obelisk -O0 --execution-tier=native %s -o %t.o0
// RUN: %t.o0 | FileCheck %s
// RUN: obelisk -O3 --execution-tier=native %s -o %t.o3
// RUN: %t.o3 | FileCheck %s
// RUN: obelisk -O3 --execution-tier=bytecode %s -o %t.bytecode
// RUN: %t.bytecode | FileCheck %s

// Below O3, keep the induction CFG available to dataflow and scheduling. A
// termination certificate alone must not imply replication of simulation
// stores. O3 separately proves and budgets early replication; the executions
// below must agree with the rolled O0 body and canonical bytecode.
module induction_loop(input bit [7:0] source,
                      output bit [7:0] result [0:3]);
  always_comb
    for (int i = 0; i < 4; i++)
      result[i] = source + i;
endmodule

module native_preserve_induction_loops;
  bit [7:0] source;
  bit [7:0] result [0:3];
  induction_loop worker(source, result);
  initial begin
    source = 0;
    #1 $display("loop %0d %0d %0d %0d", result[0], result[1], result[2], result[3]);
    source = 253;
    #1 $display("loop %0d %0d %0d %0d", result[0], result[1], result[2], result[3]);
    source = 255;
    #1 $display("loop %0d %0d %0d %0d", result[0], result[1], result[2], result[3]);
    $finish;
  end
endmodule

// LOOP: ^[[HEADER:bb[0-9]+]](%[[IV:[0-9]+]]: i32):
// LOOP: %[[CMP:[0-9]+]] = arith.cmpi slt, %[[IV]],
// LOOP-NEXT: cf.cond_br %[[CMP]], ^[[BODY:bb[0-9]+]], ^[[EXIT:bb[0-9]+]]
// LOOP-NEXT: ^[[BODY]]:
// LOOP: simulation.ref.array_element
// LOOP: %[[NEXT:[0-9]+]] = arith.addi %[[IV]],
// LOOP-NEXT: cf.br ^[[HEADER]](%[[NEXT]] : i32)
// LOOP-NEXT: ^[[EXIT]]:
// LOOP-NEXT: simulation.suspend.change
// CHECK: loop 0 1 2 3
// CHECK-NEXT: loop 253 254 255 0
// CHECK-NEXT: loop 255 0 1 2
