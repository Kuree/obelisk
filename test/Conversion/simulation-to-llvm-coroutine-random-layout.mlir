// RUN: obelisk-opt %s --convert-obelisk-sim-processes-to-llvm-coroutines | FileCheck %s

module attributes {
  llvm.data_layout = "e-p:64:64-i64:64-i32:32-i16:16-i8:8",
  llvm.target_triple = "x86_64-unknown-linux-gnu"
} {
  obelisk_sim.design @random_layout {
    obelisk_sim.scope.decl 0 hierarchy "top"
    obelisk_sim.class.decl @Node id 1 {
      is_abstract = false, is_final = false, is_interface = false,
      obelisk_sim.random_mode_field = @Node_mode
    }
    obelisk_sim.class.field @Node_child of @Node at 0 :
        !obelisk_sim.class_handle<@Node> {
      is_static = false, is_weak = false,
      obelisk_sim.random_mode_index = 2 : i64,
      obelisk_sim.random_object_edge
    }
    obelisk_sim.class.field @Node_mode of @Node at 1 : i64 {
      is_static = false, is_weak = false
    }
  }
}

// The class header occupies bytes 0..7, the direct rand handle is at byte 8,
// and its root-class rand_mode word is at byte 16. Property index two selects
// mask four. These are compiler-emitted ABI records, not executable lowering
// heuristics.
// CHECK-LABEL: llvm.mlir.global internal constant @Node.__obelisk_class_descriptor
// CHECK: llvm.mlir.addressof @Node.__obelisk_random_layout
// CHECK-LABEL: llvm.mlir.global internal constant @Node.__obelisk_random_layout
// CHECK: llvm.mlir.constant(1 : i32)
// CHECK: llvm.mlir.addressof @Node.__obelisk_random_edges
// CHECK: llvm.mlir.constant(1 : i64)
// CHECK-LABEL: llvm.mlir.global internal constant @Node.__obelisk_random_edges
// CHECK: llvm.mlir.constant(8 : i64)
// CHECK: llvm.mlir.constant(16 : i64)
// CHECK: llvm.mlir.constant(4 : i64)
