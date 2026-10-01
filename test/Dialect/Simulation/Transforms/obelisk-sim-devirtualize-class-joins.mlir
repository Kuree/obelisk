// RUN: obelisk-opt %s --obelisk-sim-devirtualize-class-calls -o %t.threaded
// RUN: obelisk-opt %s --mlir-disable-threading --obelisk-sim-devirtualize-class-calls -o %t.serial
// RUN: diff %t.threaded %t.serial
// RUN: obelisk-opt %s --obelisk-sim-devirtualize-class-calls | FileCheck %s
// RUN: obelisk-opt %s --mlir-disable-threading --obelisk-sim-devirtualize-class-calls | FileCheck %s

module {
  simulation.design @joins {
    simulation.scope.decl 0 hierarchy "top"
    simulation.code_unit.decl 1 in 0 function hierarchy "get"
    simulation.code_unit.decl 2 in 0 function hierarchy "same"
    simulation.code_unit.decl 3 in 0 function hierarchy "mixed"
    simulation.code_unit.decl 4 in 0 function hierarchy "nullable"
    simulation.code_unit.decl 5 in 0 function hierarchy "loop"
    simulation.class.decl @Base id 1 {
      is_abstract = false, is_final = false, is_interface = false
    }
    simulation.class.decl @Derived id 2 extends @Base {
      is_abstract = false, is_final = false, is_interface = false
    }
    simulation.class.method @base_get of @Base slot 0 signature_id 1
      implemented_by @get :
      (!simulation.context, !simulation.class_handle<@Base>) -> i64 {
        is_final = false, is_pure = false, is_static = false,
        is_task = false, is_virtual = true
      }
    simulation.class.method @derived_get of @Derived slot 0 signature_id 1
      implemented_by @derived :
      (!simulation.context, !simulation.class_handle<@Derived>) -> i64 {
        is_final = false, is_pure = false, is_static = false,
        is_task = false, is_virtual = true
      }
    simulation.code_unit.decl 6 in 0 function hierarchy "derived"
    simulation.func private @get(
      %ctx: !simulation.context {simulation.capture_kind = 0 : i32},
      %this: !simulation.class_handle<@Base> {simulation.capture_kind = 1 : i32}) -> i64
      attributes {code_unit_id = 1 : i64, entry_kind = 8 : i32} {
      %c = arith.constant 0 : i64
      simulation.return %c : i64
    }
    simulation.func private @derived(
      %ctx: !simulation.context {simulation.capture_kind = 0 : i32},
      %this: !simulation.class_handle<@Derived> {simulation.capture_kind = 1 : i32}) -> i64
      attributes {code_unit_id = 6 : i64, entry_kind = 8 : i32} {
      %c = arith.constant 1 : i64
      simulation.return %c : i64
    }
    // Distinct objects of the same dynamic class have the same dispatch fact.
    // CHECK-LABEL: simulation.func @same
    // CHECK: cf.cond_br
    // CHECK: simulation.call @derived
    // CHECK-NOT: simulation.class.virtual_call
    simulation.func @same(
      %ctx: !simulation.context {simulation.capture_kind = 0 : i32},
      %condition: i1 {simulation.capture_kind = 1 : i32}) -> i64
      attributes {code_unit_id = 2 : i64, entry_kind = 8 : i32} {
      %a = simulation.class.alloc %ctx : !simulation.context -> !simulation.class_handle<@Derived>
      %b = simulation.class.alloc %ctx : !simulation.context -> !simulation.class_handle<@Derived>
      %ab = simulation.class.cast %a : !simulation.class_handle<@Derived> to !simulation.class_handle<@Base>
      %bb = simulation.class.cast %b : !simulation.class_handle<@Derived> to !simulation.class_handle<@Base>
      cf.cond_br %condition, ^join(%ab : !simulation.class_handle<@Base>), ^join(%bb : !simulation.class_handle<@Base>)
    ^join(%object: !simulation.class_handle<@Base>):
      %result = simulation.class.virtual_call %object[@base_get] slot 0 signature_id 1() : (!simulation.class_handle<@Base>) -> i64
      simulation.return %result : i64
    }
    // CHECK-LABEL: simulation.func @mixed
    // CHECK: simulation.class.virtual_call
    simulation.func @mixed(
      %ctx: !simulation.context {simulation.capture_kind = 0 : i32},
      %condition: i1 {simulation.capture_kind = 1 : i32}) -> i64
      attributes {code_unit_id = 3 : i64, entry_kind = 8 : i32} {
      %a = simulation.class.alloc %ctx : !simulation.context -> !simulation.class_handle<@Derived>
      %b = simulation.class.alloc %ctx : !simulation.context -> !simulation.class_handle<@Base>
      %ab = simulation.class.cast %a : !simulation.class_handle<@Derived> to !simulation.class_handle<@Base>
      cf.cond_br %condition, ^join(%ab : !simulation.class_handle<@Base>), ^join(%b : !simulation.class_handle<@Base>)
    ^join(%object: !simulation.class_handle<@Base>):
      %result = simulation.class.virtual_call %object[@base_get] slot 0 signature_id 1() : (!simulation.class_handle<@Base>) -> i64
      simulation.return %result : i64
    }
    // A reachable null edge must prevent an exact-class proof.
    // CHECK-LABEL: simulation.func @nullable
    // CHECK: simulation.class.virtual_call
    simulation.func @nullable(
      %ctx: !simulation.context {simulation.capture_kind = 0 : i32},
      %condition: i1 {simulation.capture_kind = 1 : i32}) -> i64
      attributes {code_unit_id = 4 : i64, entry_kind = 8 : i32} {
      %a = simulation.class.alloc %ctx : !simulation.context -> !simulation.class_handle<@Derived>
      %ab = simulation.class.cast %a : !simulation.class_handle<@Derived> to !simulation.class_handle<@Base>
      %null = simulation.class.null : !simulation.class_handle<@Base>
      cf.cond_br %condition, ^join(%ab : !simulation.class_handle<@Base>), ^join(%null : !simulation.class_handle<@Base>)
    ^join(%object: !simulation.class_handle<@Base>):
      %result = simulation.class.virtual_call %object[@base_get] slot 0 signature_id 1() : (!simulation.class_handle<@Base>) -> i64
      simulation.return %result : i64
    }
    // A forwarding backedge must converge to the initial dynamic class.
    // CHECK-LABEL: simulation.func @loop
    // CHECK: simulation.call @derived
    // CHECK-NOT: simulation.class.virtual_call
    simulation.func @loop(
      %ctx: !simulation.context {simulation.capture_kind = 0 : i32},
      %condition: i1 {simulation.capture_kind = 1 : i32}) -> i64
      attributes {code_unit_id = 5 : i64, entry_kind = 8 : i32} {
      %a = simulation.class.alloc %ctx : !simulation.context -> !simulation.class_handle<@Derived>
      %ab = simulation.class.cast %a : !simulation.class_handle<@Derived> to !simulation.class_handle<@Base>
      cf.br ^loop(%ab : !simulation.class_handle<@Base>)
    ^loop(%object: !simulation.class_handle<@Base>):
      cf.cond_br %condition, ^loop(%object : !simulation.class_handle<@Base>), ^exit
    ^exit:
      %result = simulation.class.virtual_call %object[@base_get] slot 0 signature_id 1() : (!simulation.class_handle<@Base>) -> i64
      simulation.return %result : i64
    }
  }
}
