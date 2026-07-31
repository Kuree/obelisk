// RUN: obelisk-opt %s \
// RUN:   --pass-pipeline='builtin.module(obelisk_sim.design(obelisk-sim-build-compute-graph,obelisk-sim-verify-compute-graph,obelisk-sim-specialize-static-state-nba),convert-obelisk-sim-processes-to-llvm-coroutines)' \
// RUN:   | FileCheck %s

// Direct fixed state is an addressing capability, not an AOT-scheduler
// capability. Exercise a wide root under forced generic scheduling.
module attributes {
  llvm.data_layout = "e-m:e-p:64:64-i64:64-n8:16:32:64-S128",
  llvm.target_triple = "x86_64-unknown-linux-gnu",
  obelisk.native_scheduler = 1 : i32
} {
  obelisk_sim.design @direct_generic {
    obelisk_sim.scope.decl 0
    obelisk_sim.code_unit.decl 1 in 0 root_initializer hierarchy "direct_generic.root"
    obelisk_sim.code_unit.decl 2 in 0 initial hierarchy "direct_generic.process"
    obelisk_sim.storage.decl 0 in 0 : !obelisk_sim.logic<128> design

    obelisk_sim.func @root(
        %ctx: !obelisk_sim.context {obelisk_sim.capture_kind = 0 : i32})
        attributes {entry_kind = 0 : i32, code_unit_id = 1 : i64} {
      %storage = obelisk_sim.context.storage %ctx[0] :
          !obelisk_sim.ref<!obelisk_sim.logic<128>>
      %process = obelisk_sim.spawn @process(%ctx, %storage) :
          !obelisk_sim.context, !obelisk_sim.ref<!obelisk_sim.logic<128>>
          -> !obelisk_sim.process
      obelisk_sim.return
    }

    obelisk_sim.func @process(
        %ctx: !obelisk_sim.context {obelisk_sim.capture_kind = 0 : i32},
        %state: !obelisk_sim.ref<!obelisk_sim.logic<128>>
            {obelisk_sim.capture_kind = 3 : i32,
             obelisk_sim.descriptor_id = 0 : i64})
        attributes {entry_kind = 1 : i32, code_unit_id = 2 : i64} {
      %value = obelisk_sim.ref.load %state :
          !obelisk_sim.ref<!obelisk_sim.logic<128>> -> !obelisk_sim.logic<128>
      obelisk_sim.ref.store %value to %state :
          !obelisk_sim.logic<128>, !obelisk_sim.ref<!obelisk_sim.logic<128>>
      obelisk_sim.return
    }
  }
}

// CHECK-LABEL: llvm.func @process
// CHECK: llvm.load {{.*}} : !llvm.ptr -> i128
// CHECK: llvm.store
// CHECK-NOT: llvm.call @obelisk_rt_v1_native_state_load_plane
// CHECK-NOT: llvm.call @obelisk_rt_v1_native_state_store_plane
// CHECK-LABEL: llvm.func @process.__obelisk_spawn
