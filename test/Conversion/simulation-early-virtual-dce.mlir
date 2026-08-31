// RUN: obelisk-opt %s --pass-pipeline='builtin.module(obelisk_sim.design(symbol-dce,obelisk-sim-devirtualize-class-calls))' | FileCheck %s

module {
  obelisk_sim.design @classes {
    obelisk_sim.scope.decl 0 hierarchy "top"
    obelisk_sim.code_unit.decl 1 in 0 root_initializer hierarchy "root"
    obelisk_sim.code_unit.decl 2 in 0 function hierarchy "C.live"
    obelisk_sim.code_unit.decl 3 in 0 function hierarchy "C.dead"
    obelisk_sim.class.decl @C id 1 {
      is_abstract = false, is_final = false, is_interface = false
    }
    obelisk_sim.class.method @C_dead of @C slot 2
        signature_id 40 implemented_by @dead_impl :
      (!obelisk_sim.context, !obelisk_sim.class_handle<@C>) -> i64 {
        is_final = false, is_pure = false, is_static = false,
        is_task = false, is_virtual = true, sym_visibility = "private"
      }
    obelisk_sim.class.method @C_live of @C slot 5
        signature_id 41 implemented_by @live_impl :
      (!obelisk_sim.context, !obelisk_sim.class_handle<@C>) -> i64 {
        is_final = false, is_pure = false, is_static = false,
        is_task = false, is_virtual = true, sym_visibility = "private"
      }

    obelisk_sim.func @root(
        %ctx: !obelisk_sim.context {obelisk_sim.capture_kind = 0 : i32})
        attributes {code_unit_id = 1 : i64, entry_kind = 0 : i32} {
      obelisk_sim.class.dispatch_targets [@C_live]
      obelisk_sim.return
    }
    obelisk_sim.func private @live_impl(
        %ctx: !obelisk_sim.context {obelisk_sim.capture_kind = 0 : i32},
        %this: !obelisk_sim.class_handle<@C>
          {obelisk_sim.capture_kind = 1 : i32}) -> i64
        attributes {code_unit_id = 2 : i64, entry_kind = 8 : i32} {
      %value = arith.constant 7 : i64
      obelisk_sim.return %value : i64
    }
    obelisk_sim.func private @dead_impl(
        %ctx: !obelisk_sim.context {obelisk_sim.capture_kind = 0 : i32},
        %this: !obelisk_sim.class_handle<@C>
          {obelisk_sim.capture_kind = 1 : i32}) -> i64
        attributes {code_unit_id = 3 : i64, entry_kind = 8 : i32} {
      %value = arith.constant 9 : i64
      obelisk_sim.return %value : i64
    }
  }
}

// CHECK-NOT: @C_dead
// CHECK-NOT: @dead_impl
// CHECK: obelisk_sim.class.method @C_live of @C slot 0
// CHECK-SAME: implemented_by @live_impl
// CHECK-LABEL: obelisk_sim.func @root
// CHECK-NOT: class.dispatch_targets
// CHECK: obelisk_sim.return
// CHECK-LABEL: obelisk_sim.func private @live_impl
