// RUN: obelisk-opt %s --obelisk-sim-devirtualize-class-calls | FileCheck %s

module {
  obelisk_sim.design @derived_before_base {
    obelisk_sim.scope.decl 0 hierarchy "top"
    obelisk_sim.code_unit.decl 1 in 0 root_initializer hierarchy "root"
    obelisk_sim.code_unit.decl 2 in 0 function hierarchy "Base.get"

    // Declaration order is intentionally opposite inheritance order.
    obelisk_sim.class.decl @Derived id 2 extends @Base {
      is_abstract = false, is_final = false, is_interface = false
    }
    obelisk_sim.class.decl @Base id 1 {
      is_abstract = false, is_final = false, is_interface = false
    }
    obelisk_sim.class.method @Base_get of @Base slot 0 signature_id 17
        implemented_by @base_get :
      (!obelisk_sim.context, !obelisk_sim.class_handle<@Base>) -> i64 {
        is_final = false, is_pure = false, is_static = false,
        is_task = false, is_virtual = true
      }

    obelisk_sim.func private @base_get(
        %ctx: !obelisk_sim.context {obelisk_sim.capture_kind = 0 : i32},
        %this: !obelisk_sim.class_handle<@Base>
          {obelisk_sim.capture_kind = 1 : i32}) -> i64
        attributes {code_unit_id = 2 : i64, entry_kind = 8 : i32} {
      %value = arith.constant 1 : i64
      obelisk_sim.return %value : i64
    }

    obelisk_sim.func @root(
        %ctx: !obelisk_sim.context {obelisk_sim.capture_kind = 0 : i32})
        attributes {code_unit_id = 1 : i64, entry_kind = 0 : i32} {
      %derived = obelisk_sim.class.alloc %ctx :
        !obelisk_sim.context -> !obelisk_sim.class_handle<@Derived>
      %base = obelisk_sim.class.cast %derived :
        !obelisk_sim.class_handle<@Derived> to
        !obelisk_sim.class_handle<@Base>
      %value = obelisk_sim.class.virtual_call
        %base[@Base_get] slot 0 signature_id 17() :
        (!obelisk_sim.class_handle<@Base>) -> i64
      obelisk_sim.return
    }
  }
}

// CHECK-LABEL: obelisk_sim.func @root
// CHECK: obelisk_sim.call @base_get
// CHECK-NOT: obelisk_sim.class.virtual_call
