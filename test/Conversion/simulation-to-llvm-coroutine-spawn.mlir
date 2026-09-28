// RUN: obelisk-opt %s --convert-obelisk-sim-processes-to-llvm-coroutines | FileCheck %s
// RUN: obelisk-opt %s --convert-obelisk-sim-processes-to-llvm-coroutines | FileCheck %s --check-prefix=STATIC

module attributes {
  llvm.data_layout = "e-m:e-p:64:64-i64:64-n8:16:32:64-S128",
  llvm.target_triple = "x86_64-unknown-linux-gnu"
} {
  simulation.design @spawn_lowering {
    simulation.scope.decl 0
    simulation.storage.decl 0 in 0 : i64 design
    simulation.code_unit.decl 1 in 0 initial hierarchy "spawn_lowering.child"
    simulation.code_unit.decl 2 in 0 initial hierarchy "spawn_lowering.parent"
    simulation.code_unit.decl 3 in 0 initial hierarchy "spawn_lowering.static_parent"

    simulation.func @child(
        %ctx: !simulation.context {simulation.capture_kind = 0 : i32},
        %ref: !simulation.ref<i64> {simulation.capture_kind = 1 : i32})
        attributes {entry_kind = 1 : i32, code_unit_id = 1 : i64} {
      %value = simulation.ref.load %ref : !simulation.ref<i64> -> i64
      simulation.return
    }
    simulation.func @static_parent(
        %ctx: !simulation.context {simulation.capture_kind = 0 : i32})
        attributes {entry_kind = 1 : i32, code_unit_id = 3 : i64} {
      %global = simulation.context.storage %ctx[0] : !simulation.ref<i64>
      %child = simulation.spawn @child(%ctx, %global) :
          !simulation.context, !simulation.ref<i64> -> !simulation.process
      simulation.return
    }

    simulation.func @parent(
        %ctx: !simulation.context {simulation.capture_kind = 0 : i32},
        %initial: i64 {simulation.capture_kind = 2 : i32})
        attributes {entry_kind = 1 : i32, code_unit_id = 2 : i64} {
      %local = simulation.ref.alloc %initial :
          i64 -> !simulation.ref<i64>
      %child = simulation.spawn @child(%ctx, %local) :
          !simulation.context, !simulation.ref<i64> -> !simulation.process
      simulation.return
    }
  }
}

// CHECK-DAG: llvm.mlir.global internal thread_local @__obelisk_current_context
// CHECK-DAG: llvm.mlir.global internal @__obelisk_static_specialization_fast_v1
// CHECK-LABEL: llvm.func @child.__obelisk_spawn
// CHECK: llvm.mlir.addressof @child.__obelisk_spawn_plan
// CHECK: llvm.call @obelisk_rt_v1_process_spawn
// CHECK-LABEL: llvm.func @parent
// CHECK: llvm.call @obelisk_rt_v1_native_state_alloc
// CHECK: llvm.call @obelisk_rt_v1_native_state_retain
// CHECK: llvm.call @child.__obelisk_spawn

// Design-lifetime captures require no automatic-state retain. The parent
// above still retains its allocated local before passing it to the child.
// STATIC-LABEL: llvm.func @static_parent(
// STATIC-NOT: llvm.call @obelisk_rt_v1_native_state_retain
// STATIC: llvm.call @child.__obelisk_spawn
// STATIC-NOT: llvm.call @obelisk_rt_v1_native_state_retain
// STATIC: llvm.return
