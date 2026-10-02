// RUN: obelisk-opt %s --pass-pipeline='builtin.module(obelisk-sim-prepare-unit-lowering,simulation.design(simulation.func(obelisk-sim-lower-unit)))' | FileCheck %s

// Minimal MLIR-level coverage for a derived procedural event-primary monitor.
// IEEE 1800-2017 9.4.2 reevaluates clock&enable on either dependency, while
// 30.4.3 qualifies the path from the independently classified clock edge.

!logic1 = !obelisk.integral<1, false, true, 0 : 0, logic>

module {
  simulation.design @derived_procedural_path {
    simulation.scope.decl 0
    simulation.code_unit.decl 9960001 in 0 observer
        hierarchy "derived_procedural_path.primary"
    simulation.net.decl 0 in 0 : !simulation.logic<1> design
    simulation.net.decl 1 in 0 : !simulation.logic<1> design
    simulation.storage.decl 0 in 0 : !simulation.logic<1> design
    simulation.storage.decl 1 in 0 : i4 design
    simulation.storage.decl 2 in 0 : i64 design

    simulation.func private @primary(
        %ctx: !simulation.context {simulation.capture_kind = 0 : i32},
        %clock: !simulation.net<!simulation.logic<1>>
            {simulation.capture_kind = 4 : i32,
             simulation.descriptor_id = 0 : i64},
        %enable: !simulation.net<!simulation.logic<1>>
            {simulation.capture_kind = 4 : i32,
             simulation.descriptor_id = 1 : i64},
        %snapshot: !simulation.ref<!simulation.logic<1>>
            {simulation.capture_kind = 3 : i32,
             simulation.descriptor_id = 0 : i64},
        %pending: !simulation.ref<i4>
            {simulation.capture_kind = 3 : i32,
             simulation.descriptor_id = 1 : i64},
        %epoch: !simulation.ref<i64>
            {simulation.capture_kind = 3 : i32,
             simulation.descriptor_id = 2 : i64}) -> !simulation.logic<1>
        attributes {
          entry_kind = 14 : i32, code_unit_id = 9960001 : i64,
          simulation.observer_result = 1 : i32,
          simulation.timing_path_monitor_rules = [{
            inputs = ["top.clock"], snapshots = ["top.snapshot"],
            input_lows = array<i64: 0>, input_widths = array<i64: 1>,
            input_lsbs = array<i64: 0>, output_low = 1 : i64,
            output_width = 2 : i64, output_root_width = 4 : i64,
            edge_identifier = 1 : i32, edge_pending = "top.pending",
            edge_epoch = "top.epoch", condition_kind = 0 : i32,
            condition_group = 0 : i32}, {
            inputs = ["top.clock"], snapshots = ["top.snapshot"],
            input_lows = array<i64: 0>, input_widths = array<i64: 1>,
            input_lsbs = array<i64: 0>, output_low = 3 : i64,
            output_width = 1 : i64, output_root_width = 4 : i64,
            edge_identifier = 0 : i32, edge_pending = "top.pending",
            edge_epoch = "top.epoch", condition_kind = 0 : i32,
            condition_group = 0 : i32}],
          simulation.bindings = [
            #simulation.argument_binding<path = "top.clock", argument = 1, kind = direct, copyOut = false>,
            #simulation.argument_binding<path = "top.enable", argument = 2, kind = direct, copyOut = false>,
            #simulation.argument_binding<path = "top.snapshot", argument = 3, kind = direct, copyOut = false>,
            #simulation.argument_binding<path = "top.pending", argument = 4, kind = direct, copyOut = false>,
            #simulation.argument_binding<path = "top.epoch", argument = 5, kind = direct, copyOut = false>]}
        {
      obelisk.sv.expression.binary_op attributes {
          node_id = 1 : i64, operator_kind = 5 : i32,
          semantic_type = !logic1} {
        obelisk.sv.expression.named_value attributes {
            node_id = 2 : i64, referenced_path = "top.clock",
            referenced_symbol = @clock, semantic_type = !logic1} {}
        obelisk.sv.expression.named_value attributes {
            node_id = 3 : i64, referenced_path = "top.enable",
            referenced_symbol = @enable, semantic_type = !logic1} {}
      }
      %dummy = simulation.logic.constant false, false :
          !simulation.logic<1>
      simulation.return %dummy : !simulation.logic<1>
    }
  }
}

// CHECK-LABEL: simulation.func private @primary
// One four-state classifier is shared by the posedge and omitted-edge rules
// for a source/LSB pair. IEEE 1800-2017 30.4.3 makes an omitted edge match
// every logical transition, including transitions involving X or Z.
// CHECK-COUNT-2: simulation.logic.extract
// CHECK-COUNT-5: simulation.logic.compare
// CHECK: simulation.time.now
// CHECK: simulation.ref.load %arg4
// CHECK: simulation.ref.load %arg5
// CHECK: arith.select
// CHECK: simulation.ref.store {{.*}} to %arg4
// CHECK: simulation.ref.store {{.*}} to %arg5
// CHECK: simulation.ref.store {{.*}} to %arg3
// CHECK: simulation.logic.binary and
// CHECK: simulation.return
// CHECK-NOT: simulation.driver.drive_inertial_path
