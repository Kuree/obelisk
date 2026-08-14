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
    obelisk_sim.class.field @Node_value of @Node at 1 : i16 {
      is_static = false, is_weak = false,
      obelisk_sim.random_mode_index = 0 : i64,
      obelisk_sim.random_variable_kind = 1 : i32,
      obelisk_sim.random_variable_signed = true
    }
    obelisk_sim.class.field @Node_cycle of @Node at 2 :
        !obelisk_sim.logic<8> {
      is_static = false, is_weak = false,
      obelisk_sim.random_cycle_key_field = @Node_cycle_key,
      obelisk_sim.random_cycle_position_field = @Node_cycle_position,
      obelisk_sim.random_mode_index = 1 : i64,
      obelisk_sim.random_variable_kind = 2 : i32,
      obelisk_sim.random_variable_signed = false
    }
    obelisk_sim.class.field @Node_cycle_key of @Node at 3 : i64 {
      is_static = false, is_weak = false
    }
    obelisk_sim.class.field @Node_cycle_position of @Node at 4 : i64 {
      is_static = false, is_weak = false
    }
    obelisk_sim.class.field @Node_mode of @Node at 5 : i64 {
      is_static = false, is_weak = false
    }
  }
}

// The class header occupies bytes 0..7, the direct rand handle is at byte 8,
// the signed rand value is at byte 16, the four-state randc planes are at
// bytes 18 and 19, randc state is at bytes 24 and 32, and the root rand_mode
// word is at byte 40. These are compiler-emitted ABI records, not executable
// lowering heuristics.
// CHECK-LABEL: llvm.mlir.global internal constant @Node.__obelisk_class_descriptor
// CHECK: llvm.mlir.addressof @Node.__obelisk_random_layout
// CHECK-LABEL: llvm.mlir.global internal constant @Node.__obelisk_random_layout
// CHECK: llvm.mlir.constant(1 : i32)
// CHECK: llvm.mlir.addressof @Node.__obelisk_random_edges
// CHECK: llvm.mlir.constant(1 : i64)
// CHECK: llvm.mlir.addressof @Node.__obelisk_random_variables
// CHECK: llvm.mlir.constant(2 : i64)
// CHECK-LABEL: llvm.mlir.global internal constant @Node.__obelisk_random_variables
// First record: signed ordinary rand i16, with UINT64_MAX randc sentinels.
// CHECK: llvm.mlir.constant(16 : i64)
// CHECK: llvm.mlir.constant(40 : i64)
// CHECK: llvm.mlir.constant(1 : i64)
// CHECK-COUNT-2: llvm.mlir.constant(-1 : i64)
// CHECK: llvm.mlir.constant(16 : i32)
// CHECK: llvm.mlir.constant(2 : i32)
// Second record: four-state randc i8 with key and position offsets.
// CHECK: llvm.mlir.constant(18 : i64)
// CHECK: llvm.mlir.constant(40 : i64)
// CHECK: llvm.mlir.constant(2 : i64)
// CHECK: llvm.mlir.constant(24 : i64)
// CHECK: llvm.mlir.constant(32 : i64)
// CHECK: llvm.mlir.constant(8 : i32)
// CHECK: llvm.mlir.constant(5 : i32)
// CHECK-LABEL: llvm.mlir.global internal constant @Node.__obelisk_random_edges
// CHECK: llvm.mlir.constant(8 : i64)
// CHECK: llvm.mlir.constant(40 : i64)
// CHECK: llvm.mlir.constant(4 : i64)
