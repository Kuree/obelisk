// RUN: obelisk-opt %s --convert-obelisk-sim-processes-to-llvm-coroutines | FileCheck %s

// IEEE 1800-2023 8.12: a shallow copy sets an embedded covergroup handle to
// null in the new object. Each class descriptor lists its own embedded group
// fields as byte ranges; a handle of a covergroup declared elsewhere is shared
// and absent.
module attributes {
  llvm.data_layout = "e-p:64:64-i64:64-i32:32-i16:16-i8:8",
  llvm.target_triple = "x86_64-unknown-linux-gnu"
} {
  simulation.design @copy_resets {
    simulation.scope.decl 0 hierarchy "top"
    simulation.covergroup.decl @cg schema 1 debug ""
    simulation.class.decl @Base id 1 {
      is_abstract = false, is_final = false, is_interface = false
    }
    simulation.class.field @Base_flag of @Base at 0 : i8 {
      is_static = false, is_weak = false
    }
    simulation.class.field @Base_group of @Base at 1 :
        !simulation.covergroup_handle<@cg> {
      is_static = false, is_weak = false, simulation.embedded_covergroup
    }
    simulation.class.field @Base_shared of @Base at 2 :
        !simulation.covergroup_handle<@cg> {
      is_static = false, is_weak = false
    }
    simulation.class.decl @Derived id 2 extends @Base {
      is_abstract = false, is_final = false, is_interface = false
    }
    simulation.class.field @Derived_group of @Derived at 0 :
        !simulation.covergroup_handle<@cg> {
      is_static = false, is_weak = false, simulation.embedded_covergroup
    }
  }
}

// Base: header word, i8 flag at byte 8, embedded group at byte 16, shared
// group at byte 24. Derived's own group follows the 32-byte base instance.
// CHECK-LABEL: llvm.mlir.global internal constant @Derived.__obelisk_class_descriptor
// CHECK: llvm.mlir.addressof @Derived.__obelisk_copy_resets
// CHECK-NEXT: llvm.insertvalue {{.*}}[14]
// CHECK-NEXT: llvm.mlir.constant(1 : i64)
// CHECK-NEXT: llvm.insertvalue {{.*}}[15]
// CHECK-LABEL: llvm.mlir.global internal constant @Derived.__obelisk_copy_resets
// CHECK-SAME: !llvm.array<1 x struct<(i64, i64)>>
// CHECK: llvm.mlir.constant(32 : i64)
// CHECK-NEXT: llvm.insertvalue
// CHECK-NEXT: llvm.mlir.constant(8 : i64)
// CHECK-LABEL: llvm.mlir.global internal constant @Base.__obelisk_class_descriptor
// CHECK: llvm.mlir.addressof @Base.__obelisk_copy_resets
// CHECK-NEXT: llvm.insertvalue {{.*}}[14]
// CHECK-NEXT: llvm.mlir.constant(1 : i64)
// CHECK-NEXT: llvm.insertvalue {{.*}}[15]
// CHECK-LABEL: llvm.mlir.global internal constant @Base.__obelisk_copy_resets
// CHECK-SAME: !llvm.array<1 x struct<(i64, i64)>>
// CHECK: llvm.mlir.constant(16 : i64)
// CHECK-NEXT: llvm.insertvalue
// CHECK-NEXT: llvm.mlir.constant(8 : i64)
