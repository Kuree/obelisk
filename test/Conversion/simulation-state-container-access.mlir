// RUN: obelisk-opt %s --obelisk-sim-build-compute-graph='vpi=off' --obelisk-sim-specialize-static-state-nba --convert-obelisk-sim-processes-to-llvm-coroutines | FileCheck %s

// A whole 21-bit root uses i32 accesses, and reading the root still returns
// only its 21 value bits.
module attributes {llvm.data_layout = "e-m:e-p:64:64-i64:64-n8:16:32:64-S128", llvm.target_triple = "aarch64-unknown-linux-gnu"} {
  simulation.design @containers {
    simulation.scope.decl 0
    simulation.storage.decl 0 in 0 : !simulation.logic<21> design
    simulation.code_unit.decl 1 in 0 function hierarchy "write"
    simulation.func @write(%ctx: !simulation.context {simulation.capture_kind = 0 : i32},
        %value: !simulation.logic<21> {simulation.capture_kind = 2 : i32}) -> !simulation.logic<21>
        attributes {entry_kind = 8 : i32, code_unit_id = 1 : i64} {
      %root = simulation.context.storage %ctx[0] : !simulation.ref<!simulation.logic<21>>
      simulation.ref.store %value to %root : !simulation.logic<21>, !simulation.ref<!simulation.logic<21>>
      %out = simulation.ref.load %root : !simulation.ref<!simulation.logic<21>> -> !simulation.logic<21>
      simulation.return %out : !simulation.logic<21>
    }
  }
}
// CHECK-LABEL: llvm.func @write(
// CHECK-NOT: : !llvm.ptr -> i24
// CHECK-NOT: : i24, !llvm.ptr
// CHECK: llvm.store {{.*}} : i32, !llvm.ptr
// CHECK: llvm.store {{.*}} : i32, !llvm.ptr
// CHECK: llvm.load {{.*}} : !llvm.ptr -> i32
// CHECK: llvm.trunc {{.*}} : i32 to i21
// CHECK: llvm.return
