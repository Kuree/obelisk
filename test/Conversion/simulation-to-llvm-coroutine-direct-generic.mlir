// RUN: obelisk-opt %s \
// RUN:   --pass-pipeline='builtin.module(simulation.design(obelisk-sim-build-compute-graph,obelisk-sim-verify-compute-graph,obelisk-sim-specialize-static-state-nba),convert-obelisk-sim-processes-to-llvm-coroutines)' \
// RUN:   | FileCheck %s

// Direct fixed state is an addressing capability, not an AOT-scheduler
// capability. Exercise a wide root under forced generic scheduling.
module attributes {
  llvm.data_layout = "e-m:e-p:64:64-i64:64-n8:16:32:64-S128",
  llvm.target_triple = "x86_64-unknown-linux-gnu",
  schedule.native_scheduler = 1 : i32
} {
  simulation.design @direct_generic {
    simulation.scope.decl 0
    simulation.code_unit.decl 1 in 0 root_initializer hierarchy "direct_generic.root"
    simulation.code_unit.decl 2 in 0 initial hierarchy "direct_generic.process"
    simulation.storage.decl 0 in 0 : !simulation.logic<128> design

    simulation.func @root(
        %ctx: !simulation.context {simulation.capture_kind = 0 : i32})
        attributes {entry_kind = 0 : i32, code_unit_id = 1 : i64} {
      %storage = simulation.context.storage %ctx[0] :
          !simulation.ref<!simulation.logic<128>>
      %process = simulation.spawn @process(%ctx, %storage) :
          !simulation.context, !simulation.ref<!simulation.logic<128>>
          -> !simulation.process
      simulation.return
    }

    simulation.func @process(
        %ctx: !simulation.context {simulation.capture_kind = 0 : i32},
        %state: !simulation.ref<!simulation.logic<128>>
            {simulation.capture_kind = 3 : i32,
             simulation.descriptor_id = 0 : i64})
        attributes {entry_kind = 1 : i32, code_unit_id = 2 : i64} {
      %value = simulation.ref.load %state :
          !simulation.ref<!simulation.logic<128>> -> !simulation.logic<128>
      simulation.ref.store %value to %state :
          !simulation.logic<128>, !simulation.ref<!simulation.logic<128>>
      simulation.return
    }
  }
}

// CHECK-LABEL: llvm.func @process
// CHECK: llvm.load {{.*}} : !llvm.ptr -> i128
// CHECK: llvm.store
// CHECK-NOT: llvm.call @obelisk_rt_v1_native_state_load_plane
// CHECK-NOT: llvm.call @obelisk_rt_v1_native_state_store_plane
// CHECK-LABEL: llvm.func @process.__obelisk_spawn
