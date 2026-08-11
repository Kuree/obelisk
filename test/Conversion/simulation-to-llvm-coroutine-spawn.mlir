// RUN: obelisk-opt %s --convert-obelisk-sim-processes-to-llvm-coroutines | FileCheck %s

module attributes {
  llvm.data_layout = "e-m:e-p:64:64-i64:64-n8:16:32:64-S128",
  llvm.target_triple = "x86_64-unknown-linux-gnu"
} {
  obelisk_sim.design @spawn_lowering {
    obelisk_sim.scope.decl 0
    obelisk_sim.code_unit.decl 1 in 0 initial hierarchy "spawn_lowering.child"
    obelisk_sim.code_unit.decl 2 in 0 initial hierarchy "spawn_lowering.parent"

    obelisk_sim.func @child(
        %ctx: !obelisk_sim.context {obelisk_sim.capture_kind = 0 : i32},
        %ref: !obelisk_sim.ref<i64> {obelisk_sim.capture_kind = 1 : i32})
        attributes {entry_kind = 1 : i32, code_unit_id = 1 : i64} {
      %value = obelisk_sim.ref.load %ref : !obelisk_sim.ref<i64> -> i64
      obelisk_sim.return
    }

    obelisk_sim.func @parent(
        %ctx: !obelisk_sim.context {obelisk_sim.capture_kind = 0 : i32},
        %initial: i64 {obelisk_sim.capture_kind = 2 : i32})
        attributes {entry_kind = 1 : i32, code_unit_id = 2 : i64} {
      %local = obelisk_sim.ref.alloc %initial :
          i64 -> !obelisk_sim.ref<i64>
      %child = obelisk_sim.spawn @child(%ctx, %local) :
          !obelisk_sim.context, !obelisk_sim.ref<i64> -> !obelisk_sim.process
      obelisk_sim.return
    }
  }
}

// CHECK-DAG: llvm.mlir.global internal thread_local @__obelisk_current_context
// CHECK-DAG: llvm.mlir.global internal @__obelisk_static_specialization_fast_v1
// CHECK-LABEL: llvm.func @child.__obelisk_spawn
// CHECK: llvm.call @obelisk_rt_v1_scheduler_process_token
// CHECK: llvm.mlir.constant(-9223372036854775808 : i64)
// CHECK: llvm.or
// CHECK-LABEL: llvm.func @parent
// CHECK: llvm.call @obelisk_rt_v1_native_state_alloc
// CHECK: llvm.call @obelisk_rt_v1_native_state_retain
// CHECK: llvm.call @child.__obelisk_spawn
