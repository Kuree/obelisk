// RUN: obelisk-opt %s --convert-obelisk-sim-processes-to-llvm-coroutines | FileCheck %s

module attributes {
  llvm.data_layout = "e-m:e-p:64:64-i64:64-n8:16:32:64-S128",
  llvm.target_triple = "x86_64-unknown-linux-gnu"
} {
  obelisk_sim.design @overrides {
    obelisk_sim.scope.decl 0
    obelisk_sim.code_unit.decl 1 in 0 root_initializer hierarchy "overrides"
    obelisk_sim.storage.decl 0 in 0 : i4 design

    obelisk_sim.func @root(
        %ctx: !obelisk_sim.context
            {obelisk_sim.capture_kind = 0 : i32})
        attributes {code_unit_id = 1 : i64, entry_kind = 0 : i32} {
      %storage = obelisk_sim.context.storage %ctx[0] :
          !obelisk_sim.ref<i4>
      %value = arith.constant 10 : i4
      obelisk_sim.override %storage = %value assign false :
          !obelisk_sim.ref<i4>, i4
      obelisk_sim.release_override %storage assign false :
          !obelisk_sim.ref<i4>
      obelisk_sim.return
    }
  }
}

// CHECK-LABEL: llvm.func @root(
// CHECK: llvm.call @obelisk_rt_v1_native_override
// CHECK: llvm.call @obelisk_rt_v1_native_release_override
// CHECK-NOT: obelisk_sim.override
// CHECK-NOT: obelisk_sim.release_override
