// RUN: obelisk-opt %s --obelisk-sim-plan-native-partitions \
// RUN:   --convert-obelisk-sim-processes-to-llvm-coroutines \
// RUN:   | FileCheck %s

// Module-level symbol users such as global constructors need explicit
// partition ownership and ordering rules. Until those exist, their presence
// keeps the native module on the unsplit path.
module attributes {
  llvm.data_layout = "e-m:e-p:64:64-i64:64-n8:16:32:64-S128",
  llvm.target_triple = "x86_64-unknown-linux-gnu"
} {
  llvm.func @ctor() {
    llvm.return
  }
  llvm.mlir.global_ctors ctors = [@ctor], priorities = [0 : i32],
      data = [#llvm.zero]

  simulation.design @fallback {
    simulation.scope.decl 0 hierarchy "top"
    simulation.code_unit.decl 1 in 0 root_initializer hierarchy "top.root"
    simulation.func @root(
        %ctx: !simulation.context {simulation.capture_kind = 0 : i32})
        attributes {entry_kind = 0 : i32, code_unit_id = 1 : i64} {
      simulation.return
    }
  }
}

// CHECK: module attributes {
// CHECK-SAME: obelisk.native.partition_manifests
// CHECK-NOT: obelisk.native.physical_partition_manifest
// CHECK: llvm.mlir.global_ctors
