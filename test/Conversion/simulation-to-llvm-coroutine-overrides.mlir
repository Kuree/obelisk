// RUN: obelisk-opt %s --convert-obelisk-sim-processes-to-llvm-coroutines | FileCheck %s
// RUN: obelisk-opt %s --pass-pipeline='builtin.module(obelisk_sim.design(obelisk-sim-build-compute-graph,obelisk-sim-verify-compute-graph,obelisk-sim-specialize-static-state-nba),encode-obelisk-sim-to-bytecode,convert-obelisk-sim-processes-to-llvm-coroutines)' | FileCheck %s --check-prefix=DYNAMIC

module attributes {
  llvm.data_layout = "e-m:e-p:64:64-i64:64-n8:16:32:64-S128",
  llvm.target_triple = "x86_64-unknown-linux-gnu"
} {
  obelisk_sim.design @overrides {
    obelisk_sim.scope.decl 0
    obelisk_sim.code_unit.decl 1 in 0 root_initializer hierarchy "overrides"
    obelisk_sim.code_unit.decl 2 in 0 function hierarchy "unrelated_dynamic"
    obelisk_sim.storage.decl 0 in 0 : i4 design
    obelisk_sim.storage.decl 1 in 0 : i4 design

    obelisk_sim.func @root(
        %ctx: !obelisk_sim.context
            {obelisk_sim.capture_kind = 0 : i32})
        attributes {code_unit_id = 1 : i64, entry_kind = 0 : i32} {
      %storage = obelisk_sim.context.storage %ctx[0] :
          !obelisk_sim.ref<i4>
      %value = arith.constant 10 : i4
      obelisk_sim.override %storage = %value assign false :
          !obelisk_sim.ref<i4>, i4
      %owner = obelisk_sim.process.current
      obelisk_sim.dynamic_override %storage = %value owner %owner
          assign false claim true : !obelisk_sim.ref<i4>, i4
      obelisk_sim.dynamic_override %storage = %value owner %owner
          assign false claim false : !obelisk_sim.ref<i4>, i4
      obelisk_sim.release_override %storage assign false :
          !obelisk_sim.ref<i4>
      obelisk_sim.return
    }

    // Dynamic ownership is absent from the static root proof. Even a root
    // not named by the ordinary override must retain its runtime access path.
    obelisk_sim.func @unrelated_dynamic(
        %ctx: !obelisk_sim.context {obelisk_sim.capture_kind = 0 : i32}) -> i4
        attributes {code_unit_id = 2 : i64, entry_kind = 8 : i32} {
      %storage = obelisk_sim.context.storage %ctx[1] : !obelisk_sim.ref<i4>
      %value = obelisk_sim.ref.load %storage : !obelisk_sim.ref<i4> -> i4
      obelisk_sim.return %value : i4
    }
  }
}

// CHECK-LABEL: llvm.func @root(
// CHECK: llvm.call @obelisk_rt_v1_native_override
// CHECK-COUNT-2: llvm.call @obelisk_rt_v1_native_dynamic_override
// CHECK: llvm.call @obelisk_rt_v1_native_release_override
// CHECK-NOT: obelisk_sim.override
// CHECK-NOT: obelisk_sim.dynamic_override
// CHECK-NOT: obelisk_sim.release_override

// DYNAMIC-LABEL: llvm.func @unrelated_dynamic(
// DYNAMIC: llvm.call @obelisk_rt_v1_native_state_load_plane
// DYNAMIC: llvm.return
