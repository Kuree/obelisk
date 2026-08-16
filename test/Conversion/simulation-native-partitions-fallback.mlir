// RUN: obelisk-opt %s --obelisk-sim-plan-native-partitions \
// RUN:   --convert-obelisk-sim-processes-to-llvm-coroutines \
// RUN:   | FileCheck %s
// RUN: obelisk-opt %s --obelisk-sim-plan-native-partitions \
// RUN:   --convert-obelisk-sim-processes-to-llvm-coroutines \
// RUN:   | mlir-translate --mlir-to-llvmir | opt -passes=verify -disable-output

// Native module assembly has textual symbol ownership that the semantic
// planner cannot rewrite safely. Keep compiling on the unsplit path, but do
// not publish a physical manifest that an object emitter could consume.
module attributes {
  llvm.data_layout = "e-m:e-p:64:64-i64:64-n8:16:32:64-S128",
  llvm.module_asm = ["nop"],
  llvm.target_triple = "x86_64-unknown-linux-gnu"
} {
  obelisk_sim.design @fallback {
    obelisk_sim.scope.decl 0 hierarchy "top"
    obelisk_sim.code_unit.decl 1 in 0 root_initializer hierarchy "top.root"
    obelisk_sim.func @root(
        %ctx: !obelisk_sim.context {obelisk_sim.capture_kind = 0 : i32})
        attributes {entry_kind = 0 : i32, code_unit_id = 1 : i64} {
      obelisk_sim.return
    }
  }
}

// CHECK: module attributes {
// CHECK-SAME: llvm.module_asm = ["nop"]
// CHECK-SAME: obelisk.native.partition_manifests
// CHECK-NOT: obelisk.native.physical_partition_manifest
