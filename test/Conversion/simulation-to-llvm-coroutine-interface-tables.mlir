// RUN: obelisk-opt %s --convert-obelisk-sim-processes-to-llvm-coroutines | FileCheck %s

module attributes {
  llvm.data_layout = "e-p:64:64-i64:64-i32:32-i16:16-i8:8",
  llvm.target_triple = "x86_64-unknown-linux-gnu"
} {
  obelisk_sim.design @interface_tables {
    obelisk_sim.scope.decl 0 hierarchy "top"
    obelisk_sim.code_unit.decl 1 in 0 function hierarchy "Concrete.run"

    obelisk_sim.class.decl @Runner id 1 {
      is_abstract = true, is_final = false, is_interface = true
    }
    obelisk_sim.class.decl @AbstractBase id 2 implements [@Runner] {
      is_abstract = true, is_final = false, is_interface = false
    }
    obelisk_sim.class.decl @Concrete id 3 extends @AbstractBase {
      is_abstract = false, is_final = true, is_interface = false
    }

    obelisk_sim.class.method @Runner_run of @Runner slot 4294967295
        signature_id 17 interface_ordinal 0 :
      (!obelisk_sim.context, !obelisk_sim.class_handle<@Runner>) -> i32 {
        is_final = false, is_pure = true, is_static = false,
        is_task = false, is_virtual = true
      }
    obelisk_sim.class.method @AbstractBase_run of @AbstractBase slot 0
        signature_id 17 :
      (!obelisk_sim.context, !obelisk_sim.class_handle<@AbstractBase>) -> i32 {
        is_final = false, is_pure = true, is_static = false,
        is_task = false, is_virtual = true
      }
    obelisk_sim.class.method @Concrete_run of @Concrete slot 0 signature_id 17
        implemented_by @concrete_run :
      (!obelisk_sim.context, !obelisk_sim.class_handle<@Concrete>) -> i32 {
        is_final = true, is_pure = false, is_static = false,
        is_task = false, is_virtual = true
      }

    obelisk_sim.func @concrete_run(
        %ctx: !obelisk_sim.context {obelisk_sim.capture_kind = 0 : i32},
        %this: !obelisk_sim.class_handle<@Concrete>
          {obelisk_sim.capture_kind = 1 : i32}) -> i32
        attributes {code_unit_id = 1 : i64, entry_kind = 8 : i32} {
      %zero = arith.constant 0 : i32
      obelisk_sim.return %zero : i32
    }
  }
}

// The abstract descriptor inherits Runner directly and keeps its pure method
// unresolved. Concrete inherits Runner only through its base and resolves the
// same ordinal after overriding the effective slot.
// CHECK-LABEL: llvm.mlir.global internal constant @Concrete.__obelisk_interfaces
// CHECK: llvm.mlir.constant(1 : i64)
// CHECK: llvm.mlir.addressof @Concrete.__obelisk_interface_0_slots
// CHECK-LABEL: llvm.mlir.global internal constant @Concrete.__obelisk_interface_0_slots
// CHECK: llvm.mlir.constant(0 : i32)
// CHECK-LABEL: llvm.mlir.global internal constant @AbstractBase.__obelisk_interface_0_slots
// CHECK: llvm.mlir.constant(-1 : i32)
