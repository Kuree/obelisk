// RUN: obelisk --std=1800-2023 -emit-obelisk %s | obelisk-opt '--lower-obelisk-to-sim=opt-level=0' '--test-obelisk-set-module-attributes=attrs="{llvm.data_layout = \"e-m:e-p:64:64-i64:64-n8:16:32:64-S128\", llvm.target_triple = \"x86_64-unknown-linux-gnu\"}"' | tee %t.lowered.mlir | FileCheck %s
// RUN: obelisk-opt %t.lowered.mlir --canonicalize --cse | FileCheck %s
// RUN: obelisk-opt %t.lowered.mlir --convert-obelisk-sim-processes-to-llvm-coroutines | obelisk-translate --mlir-to-llvmir | opt -S -passes=verify -o /dev/null
// RUN: obelisk-opt %t.lowered.mlir --encode-obelisk-sim-to-bytecode='vpi=off' -o /dev/null
// RUN: not obelisk-opt %t.lowered.mlir '--test-obelisk-edit-attributes=replace-from="!simulation.dynamic_array<i32>" replace-to="!simulation.queue<i32, 0>"' --convert-obelisk-sim-processes-to-llvm-coroutines -o /dev/null 2>&1 | FileCheck %s --check-prefix=INVALID
// RUN: not obelisk-opt %t.lowered.mlir '--test-obelisk-edit-attributes=replace-from="!simulation.dynamic_array<i32>" replace-to="!simulation.queue<i32, 0>"' --encode-obelisk-sim-to-bytecode='vpi=off' -o /dev/null 2>&1 | FileCheck %s --check-prefix=INVALID
// RUN: not obelisk-opt %t.lowered.mlir '--test-obelisk-edit-attributes=replace-from="!simulation.dynamic_array<i32>" replace-to="!simulation.dynamic_array<i64>"' --convert-obelisk-sim-processes-to-llvm-coroutines -o /dev/null 2>&1 | FileCheck %s --check-prefix=INVALID
// RUN: not obelisk-opt %t.lowered.mlir '--test-obelisk-edit-attributes=replace-from="!simulation.queue<i32, 3>" replace-to="!simulation.queue<i32, 4>"' --encode-obelisk-sim-to-bytecode='vpi=off' -o /dev/null 2>&1 | FileCheck %s --check-prefix=INVALID

// IEEE 19.3, 19.8.1 and 13.5: inputs clone before binding. Constructor reads
// preserve the complete managed type; refs read an addressable alias.
// CHECK-DAG: simulation.covergroup.formal_read {{.*}} -> !simulation.dynamic_array<i32>
// CHECK-DAG: simulation.covergroup.formal_read {{.*}} -> !simulation.argument_ref<!simulation.dynamic_array<i32>>
// CHECK-DAG: simulation.covergroup.formal_read {{.*}} -> !simulation.queue<i32, 3>
// CHECK-DAG: simulation.covergroup.formal_read {{.*}} -> !simulation.assoc_array
// CHECK-DAG: simulation.container.clone {{.*}} !simulation.dynamic_array<i32>
// CHECK-DAG: simulation.container.clone {{.*}} !simulation.queue<i32, 3>
// CHECK-DAG: simulation.container.clone {{.*}} !simulation.assoc_array

// Backend schema verification compares complete types even though every
// managed container has the same 64-bit ABI. Retyping the container kind,
// element type or queue bound makes the formal reads disagree with the
// embedded schema.
// INVALID: has a nonexistent, wrong-owner, or type-mismatched constructor FunctionalFormal ID

module top;
  int x;
  covergroup cg(input int a[], input int q[$:3], input int m[string], ref int r[]);
    cp: coverpoint x { bins b = {0}; }
  endgroup
  cg c;
  initial begin
    int v[];
    int w[$:3];
    int u[string];
    c = new(v, w, u, v);
    c.sample();
  end
endmodule
