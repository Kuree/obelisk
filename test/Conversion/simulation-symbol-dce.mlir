// RUN: obelisk-opt %s --pass-pipeline='builtin.module(simulation.design(symbol-dce))' | FileCheck %s

module {
  simulation.design @dce {
    simulation.code_unit.decl 9000001 in 0 function hierarchy "test.dce.unused_definition.9000001"
    simulation.code_unit.decl 9000002 in 0 function hierarchy "test.dce.recursive_a.9000002"
    simulation.code_unit.decl 9000003 in 0 function hierarchy "test.dce.recursive_b.9000003"
    simulation.code_unit.decl 9000004 in 0 initial hierarchy "test.dce.live_process.9000004"
    simulation.code_unit.decl 9000005 in 0 initial hierarchy "test.dce.live_child.9000005"
    simulation.scope.decl 0

    // Keep dead symbols before the first expected symbol so CHECK-NOT covers
    // the entire prefix where they could survive in stable IR order.
    simulation.func private @unused_definition(
        %ctx: !simulation.context {simulation.capture_kind = 0 : i32})
        attributes {entry_kind = 8 : i32, code_unit_id = 9000001 : i64} {
      simulation.return
    }
    simulation.func private @unused_declaration(
        !simulation.context {simulation.capture_kind = 0 : i32})
        attributes {entry_kind = 8 : i32}

    // CHECK-NOT: @unused_definition
    // CHECK-NOT: @unused_declaration
    // CHECK-LABEL: simulation.func @__obelisk_root
    // CHECK: simulation.call @recursive_a
    // CHECK: simulation.spawn @live_process
    simulation.func @__obelisk_root(
        %ctx: !simulation.context {simulation.capture_kind = 0 : i32})
        attributes {entry_kind = 0 : i32} {
      simulation.call @recursive_a(%ctx) : (!simulation.context) -> ()
      %process = simulation.spawn @live_process(%ctx) : !simulation.context -> !simulation.process
      simulation.return
    }

    // The call closure is recursive and must remain complete.
    // CHECK-LABEL: simulation.func private @recursive_a
    // CHECK: simulation.call @recursive_b
    simulation.func private @recursive_a(
        %ctx: !simulation.context {simulation.capture_kind = 0 : i32})
        attributes {entry_kind = 8 : i32, code_unit_id = 9000002 : i64} {
      simulation.call @recursive_b(%ctx) : (!simulation.context) -> ()
      simulation.return
    }

    // CHECK-LABEL: simulation.func private @recursive_b
    // CHECK: simulation.call @recursive_a
    simulation.func private @recursive_b(
        %ctx: !simulation.context {simulation.capture_kind = 0 : i32})
        attributes {entry_kind = 8 : i32, code_unit_id = 9000003 : i64} {
      simulation.call @recursive_a(%ctx) : (!simulation.context) -> ()
      simulation.return
    }

    // Spawn edges root the complete process closure as well.
    // CHECK-LABEL: simulation.func private @live_process
    // CHECK: simulation.spawn @live_child
    simulation.func private @live_process(
        %ctx: !simulation.context {simulation.capture_kind = 0 : i32})
        attributes {entry_kind = 1 : i32, code_unit_id = 9000004 : i64} {
      %child = simulation.spawn @live_child(%ctx) : !simulation.context -> !simulation.process
      simulation.return
    }

    // CHECK-LABEL: simulation.func private @live_child
    simulation.func private @live_child(
        %ctx: !simulation.context {simulation.capture_kind = 0 : i32})
        attributes {entry_kind = 1 : i32, code_unit_id = 9000005 : i64} {
      simulation.return
    }
  }
}
