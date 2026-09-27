// RUN: obelisk-opt %s --convert-obelisk-sim-processes-to-llvm-coroutines | FileCheck %s
// RUN: obelisk-opt %s --pass-pipeline='builtin.module(simulation.design(obelisk-sim-build-compute-graph,obelisk-sim-verify-compute-graph,obelisk-sim-specialize-static-state-nba),encode-obelisk-sim-to-bytecode,convert-obelisk-sim-processes-to-llvm-coroutines)' | FileCheck %s --check-prefix=DYNAMIC

module attributes {
  llvm.data_layout = "e-m:e-p:64:64-i64:64-n8:16:32:64-S128",
  llvm.target_triple = "x86_64-unknown-linux-gnu"
} {
  simulation.design @overrides {
    simulation.scope.decl 0
    simulation.code_unit.decl 1 in 0 root_initializer hierarchy "overrides"
    simulation.code_unit.decl 2 in 0 function hierarchy "unrelated_dynamic"
    simulation.storage.decl 0 in 0 : i4 design
    simulation.storage.decl 1 in 0 : i4 design

    simulation.func @root(
        %ctx: !simulation.context
            {simulation.capture_kind = 0 : i32})
        attributes {code_unit_id = 1 : i64, entry_kind = 0 : i32} {
      %storage = simulation.context.storage %ctx[0] :
          !simulation.ref<i4>
      %value = arith.constant 10 : i4
      simulation.override %storage = %value assign false :
          !simulation.ref<i4>, i4
      %owner = simulation.process.current
      simulation.dynamic_override %storage = %value owner %owner
          assign false claim true : !simulation.ref<i4>, i4
      simulation.dynamic_override %storage = %value owner %owner
          assign false claim false : !simulation.ref<i4>, i4
      simulation.release_override %storage assign false :
          !simulation.ref<i4>
      simulation.return
    }

    // Dynamic ownership is absent from the static root proof. Even a root
    // not named by the ordinary override must retain its runtime access path.
    simulation.func @unrelated_dynamic(
        %ctx: !simulation.context {simulation.capture_kind = 0 : i32}) -> i4
        attributes {code_unit_id = 2 : i64, entry_kind = 8 : i32} {
      %storage = simulation.context.storage %ctx[1] : !simulation.ref<i4>
      %value = simulation.ref.load %storage : !simulation.ref<i4> -> i4
      simulation.return %value : i4
    }
  }
}

// CHECK-LABEL: llvm.func @root(
// CHECK: llvm.call @obelisk_rt_v1_native_override
// CHECK-COUNT-2: llvm.call @obelisk_rt_v1_native_dynamic_override
// CHECK: llvm.call @obelisk_rt_v1_native_release_override
// CHECK-NOT: simulation.override
// CHECK-NOT: simulation.dynamic_override
// CHECK-NOT: simulation.release_override

// DYNAMIC-LABEL: llvm.func @unrelated_dynamic(
// DYNAMIC: llvm.call @obelisk_rt_v1_native_state_load_plane
// DYNAMIC: llvm.return
