// RUN: obelisk-opt %s --convert-obelisk-sim-processes-to-llvm-coroutines | FileCheck %s

module attributes {
  llvm.data_layout = "e-p:64:64-i64:64-i32:32-i16:16-i8:8",
  llvm.target_triple = "x86_64-unknown-linux-gnu"
} {
  simulation.design @interface_tables {
    simulation.scope.decl 0 hierarchy "top"
    simulation.code_unit.decl 1 in 0 function hierarchy "Concrete.run"

    simulation.class.decl @Runner id 1 {
      is_abstract = true, is_final = false, is_interface = true
    }
    simulation.class.decl @AbstractBase id 2 implements [@Runner] {
      is_abstract = true, is_final = false, is_interface = false
    }
    simulation.class.decl @Concrete id 3 extends @AbstractBase {
      is_abstract = false, is_final = true, is_interface = false
    }

    simulation.class.method @Runner_run of @Runner slot 4294967295
        signature_id 17 interface_ordinal 0 :
      (!simulation.context, !simulation.class_handle<@Runner>) -> i32 {
        is_final = false, is_pure = true, is_static = false,
        is_task = false, is_virtual = true
      }
    simulation.class.method @AbstractBase_run of @AbstractBase slot 0
        signature_id 17 :
      (!simulation.context, !simulation.class_handle<@AbstractBase>) -> i32 {
        is_final = false, is_pure = true, is_static = false,
        is_task = false, is_virtual = true
      }
    simulation.class.method @Concrete_run of @Concrete slot 0 signature_id 17
        implemented_by @concrete_run :
      (!simulation.context, !simulation.class_handle<@Concrete>) -> i32 {
        is_final = true, is_pure = false, is_static = false,
        is_task = false, is_virtual = true
      }

    simulation.func @concrete_run(
        %ctx: !simulation.context {simulation.capture_kind = 0 : i32},
        %this: !simulation.class_handle<@Concrete>
          {simulation.capture_kind = 1 : i32}) -> i32
        attributes {code_unit_id = 1 : i64, entry_kind = 8 : i32} {
      %zero = arith.constant 0 : i32
      simulation.return %zero : i32
    }
  }
}

// The abstract descriptor introduces Runner and its pure method. Concrete
// shares the inherited interface through its base and records only its local
// method override. Interface ordinals index the shared signature table.
// CHECK-LABEL: llvm.mlir.global internal constant @Concrete.__obelisk_class_descriptor
// CHECK: llvm.mlir.addressof @AbstractBase.__obelisk_class_descriptor
// CHECK-NOT: @Concrete.__obelisk_interfaces
// CHECK-LABEL: llvm.mlir.global internal constant @Concrete.__obelisk_methods
// CHECK-SAME: !llvm.array<1 x
// CHECK: llvm.mlir.constant(17 : i64)
// CHECK: llvm.mlir.addressof @Concrete_run.__obelisk_native_thunk
// CHECK-LABEL: llvm.mlir.global internal constant @AbstractBase.__obelisk_interfaces
// CHECK-SAME: !llvm.array<1 x
// CHECK: llvm.mlir.addressof @Runner.__obelisk_interface_signatures
// CHECK-LABEL: llvm.mlir.global internal constant @AbstractBase.__obelisk_methods
// CHECK: llvm.mlir.constant(17 : i64)
// CHECK: llvm.mlir.constant(2 : i32)
// CHECK-LABEL: llvm.mlir.global internal constant @Runner.__obelisk_interface_signatures
// CHECK-SAME: !llvm.array<1 x i64>
// CHECK: llvm.mlir.constant(17 : i64)
