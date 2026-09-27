// RUN: obelisk-opt %s --pass-pipeline='builtin.module(simulation.design(symbol-dce,obelisk-sim-devirtualize-class-calls))' | FileCheck %s

module {
  simulation.design @classes {
    simulation.scope.decl 0 hierarchy "top"
    simulation.code_unit.decl 1 in 0 root_initializer hierarchy "root"
    simulation.code_unit.decl 2 in 0 function hierarchy "C.live"
    simulation.code_unit.decl 3 in 0 function hierarchy "C.dead"
    simulation.class.decl @C id 1 {
      is_abstract = false, is_final = false, is_interface = false
    }
    simulation.class.method @C_dead of @C slot 2
        signature_id 40 implemented_by @dead_impl :
      (!simulation.context, !simulation.class_handle<@C>) -> i64 {
        is_final = false, is_pure = false, is_static = false,
        is_task = false, is_virtual = true, sym_visibility = "private"
      }
    simulation.class.method @C_live of @C slot 5
        signature_id 41 implemented_by @live_impl :
      (!simulation.context, !simulation.class_handle<@C>) -> i64 {
        is_final = false, is_pure = false, is_static = false,
        is_task = false, is_virtual = true, sym_visibility = "private"
      }

    simulation.func @root(
        %ctx: !simulation.context {simulation.capture_kind = 0 : i32})
        attributes {code_unit_id = 1 : i64, entry_kind = 0 : i32} {
      simulation.class.dispatch_targets [@C_live]
      simulation.return
    }
    simulation.func private @live_impl(
        %ctx: !simulation.context {simulation.capture_kind = 0 : i32},
        %this: !simulation.class_handle<@C>
          {simulation.capture_kind = 1 : i32}) -> i64
        attributes {code_unit_id = 2 : i64, entry_kind = 8 : i32} {
      %value = arith.constant 7 : i64
      simulation.return %value : i64
    }
    simulation.func private @dead_impl(
        %ctx: !simulation.context {simulation.capture_kind = 0 : i32},
        %this: !simulation.class_handle<@C>
          {simulation.capture_kind = 1 : i32}) -> i64
        attributes {code_unit_id = 3 : i64, entry_kind = 8 : i32} {
      %value = arith.constant 9 : i64
      simulation.return %value : i64
    }
  }
}

// CHECK-NOT: @C_dead
// CHECK-NOT: @dead_impl
// CHECK: simulation.class.method @C_live of @C slot 0
// CHECK-SAME: implemented_by @live_impl
// CHECK-LABEL: simulation.func @root
// CHECK-NOT: class.dispatch_targets
// CHECK: simulation.return
// CHECK-LABEL: simulation.func private @live_impl
