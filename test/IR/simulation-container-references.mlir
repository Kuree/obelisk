// RUN: obelisk-opt %s | obelisk-opt | FileCheck %s
// RUN: obelisk-opt %s --convert-obelisk-sim-processes-to-llvm-coroutines | FileCheck %s --check-prefix=NATIVE
// RUN: obelisk-opt %s --encode-obelisk-sim-to-bytecode='vpi=off' | FileCheck %s --check-prefix=BYTECODE

module attributes {
  llvm.data_layout = "e-m:e-p:64:64-i64:64-n8:16:32:64-S128",
  llvm.target_triple = "x86_64-unknown-linux-gnu"
} {
  obelisk_sim.design @container_references {
    obelisk_sim.scope.decl 0 hierarchy "top"
    obelisk_sim.code_unit.decl 1 in 0 initial hierarchy "top.capture"

    obelisk_sim.func @capture(
        %ctx: !obelisk_sim.context {obelisk_sim.capture_kind = 0 : i32},
        %queue: !obelisk_sim.queue<i64, 0> {obelisk_sim.capture_kind = 1 : i32},
        %index: i64 {obelisk_sim.capture_kind = 1 : i32})
        attributes {code_unit_id = 1 : i64, entry_kind = 1 : i32} {
      %path = obelisk_sim.reference_path.index %ctx, %queue[%index] :
        (!obelisk_sim.context, !obelisk_sim.queue<i64, 0>, i64) ->
        !obelisk_sim.reference_path<i64>
      %reference = obelisk_sim.argument_ref.from_path %path :
        !obelisk_sim.reference_path<i64> ->
        !obelisk_sim.argument_ref<i64>
      obelisk_sim.return
    }
  }
}

// CHECK: obelisk_sim.reference_path.index
// CHECK: !obelisk_sim.reference_path<i64>
// CHECK: obelisk_sim.argument_ref.from_path
// NATIVE: llvm.call @obelisk_rt_v1_reference_path_index_create
// BYTECODE: obelisk.bytecode.image
