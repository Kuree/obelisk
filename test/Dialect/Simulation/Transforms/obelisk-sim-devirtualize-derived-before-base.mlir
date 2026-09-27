// RUN: obelisk-opt %s --obelisk-sim-devirtualize-class-calls | FileCheck %s

module {
  simulation.design @derived_before_base {
    simulation.scope.decl 0 hierarchy "top"
    simulation.code_unit.decl 1 in 0 root_initializer hierarchy "root"
    simulation.code_unit.decl 2 in 0 function hierarchy "Base.get"

    // Declaration order is intentionally opposite inheritance order.
    simulation.class.decl @Derived id 2 extends @Base {
      is_abstract = false, is_final = false, is_interface = false
    }
    simulation.class.decl @Base id 1 {
      is_abstract = false, is_final = false, is_interface = false
    }
    simulation.class.method @Base_get of @Base slot 0 signature_id 17
        implemented_by @base_get :
      (!simulation.context, !simulation.class_handle<@Base>) -> i64 {
        is_final = false, is_pure = false, is_static = false,
        is_task = false, is_virtual = true
      }

    simulation.func private @base_get(
        %ctx: !simulation.context {simulation.capture_kind = 0 : i32},
        %this: !simulation.class_handle<@Base>
          {simulation.capture_kind = 1 : i32}) -> i64
        attributes {code_unit_id = 2 : i64, entry_kind = 8 : i32} {
      %value = arith.constant 1 : i64
      simulation.return %value : i64
    }

    simulation.func @root(
        %ctx: !simulation.context {simulation.capture_kind = 0 : i32})
        attributes {code_unit_id = 1 : i64, entry_kind = 0 : i32} {
      %derived = simulation.class.alloc %ctx :
        !simulation.context -> !simulation.class_handle<@Derived>
      %base = simulation.class.cast %derived :
        !simulation.class_handle<@Derived> to
        !simulation.class_handle<@Base>
      %value = simulation.class.virtual_call
        %base[@Base_get] slot 0 signature_id 17() :
        (!simulation.class_handle<@Base>) -> i64
      simulation.return
    }
  }
}

// CHECK-LABEL: simulation.func @root
// CHECK: simulation.call @base_get
// CHECK-NOT: simulation.class.virtual_call
