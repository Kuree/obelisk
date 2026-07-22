// RUN: obelisk-opt %s --pass-pipeline='builtin.module(obelisk_sim.design(symbol-dce))' | FileCheck %s

module {
  obelisk_sim.design @dce {
    obelisk_sim.code_unit.decl 9000001 in 0 function hierarchy "test.dce.unused_definition.9000001"
    obelisk_sim.code_unit.decl 9000002 in 0 function hierarchy "test.dce.recursive_a.9000002"
    obelisk_sim.code_unit.decl 9000003 in 0 function hierarchy "test.dce.recursive_b.9000003"
    obelisk_sim.code_unit.decl 9000004 in 0 initial hierarchy "test.dce.live_process.9000004"
    obelisk_sim.code_unit.decl 9000005 in 0 initial hierarchy "test.dce.live_child.9000005"
    obelisk_sim.scope.decl 0

    // Keep dead symbols before the first expected symbol so CHECK-NOT covers
    // the entire prefix where they could survive in stable IR order.
    obelisk_sim.func private @unused_definition(
        %ctx: !obelisk_sim.context {obelisk_sim.capture_kind = 0 : i32})
        attributes {entry_kind = 8 : i32, code_unit_id = 9000001 : i64} {
      obelisk_sim.return
    }
    obelisk_sim.func private @unused_declaration(
        !obelisk_sim.context {obelisk_sim.capture_kind = 0 : i32})
        attributes {entry_kind = 8 : i32}

    // CHECK-NOT: @unused_definition
    // CHECK-NOT: @unused_declaration
    // CHECK-LABEL: obelisk_sim.func @__obelisk_root
    // CHECK: obelisk_sim.call @recursive_a
    // CHECK: obelisk_sim.spawn @live_process
    obelisk_sim.func @__obelisk_root(
        %ctx: !obelisk_sim.context {obelisk_sim.capture_kind = 0 : i32})
        attributes {entry_kind = 0 : i32} {
      obelisk_sim.call @recursive_a(%ctx) : (!obelisk_sim.context) -> ()
      %process = obelisk_sim.spawn @live_process(%ctx) : !obelisk_sim.context -> !obelisk_sim.process
      obelisk_sim.return
    }

    // The call closure is recursive and must remain complete.
    // CHECK-LABEL: obelisk_sim.func private @recursive_a
    // CHECK: obelisk_sim.call @recursive_b
    obelisk_sim.func private @recursive_a(
        %ctx: !obelisk_sim.context {obelisk_sim.capture_kind = 0 : i32})
        attributes {entry_kind = 8 : i32, code_unit_id = 9000002 : i64} {
      obelisk_sim.call @recursive_b(%ctx) : (!obelisk_sim.context) -> ()
      obelisk_sim.return
    }

    // CHECK-LABEL: obelisk_sim.func private @recursive_b
    // CHECK: obelisk_sim.call @recursive_a
    obelisk_sim.func private @recursive_b(
        %ctx: !obelisk_sim.context {obelisk_sim.capture_kind = 0 : i32})
        attributes {entry_kind = 8 : i32, code_unit_id = 9000003 : i64} {
      obelisk_sim.call @recursive_a(%ctx) : (!obelisk_sim.context) -> ()
      obelisk_sim.return
    }

    // Spawn edges root the complete process closure as well.
    // CHECK-LABEL: obelisk_sim.func private @live_process
    // CHECK: obelisk_sim.spawn @live_child
    obelisk_sim.func private @live_process(
        %ctx: !obelisk_sim.context {obelisk_sim.capture_kind = 0 : i32})
        attributes {entry_kind = 1 : i32, code_unit_id = 9000004 : i64} {
      %child = obelisk_sim.spawn @live_child(%ctx) : !obelisk_sim.context -> !obelisk_sim.process
      obelisk_sim.return
    }

    // CHECK-LABEL: obelisk_sim.func private @live_child
    obelisk_sim.func private @live_child(
        %ctx: !obelisk_sim.context {obelisk_sim.capture_kind = 0 : i32})
        attributes {entry_kind = 1 : i32, code_unit_id = 9000005 : i64} {
      obelisk_sim.return
    }
  }
}
