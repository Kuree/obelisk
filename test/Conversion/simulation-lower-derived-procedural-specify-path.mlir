// RUN: obelisk-opt %s --pass-pipeline='builtin.module(obelisk_sim.design(obelisk_sim.func(obelisk-sim-lower-unit)))' | FileCheck %s

// Minimal MLIR-level coverage for a derived procedural event-primary monitor.
// IEEE 1800-2017 9.4.2 reevaluates clock&enable on either dependency, while
// 30.4.3 qualifies the path from the independently classified clock edge.

!logic1 = !obelisk.integral<1, false, true, 0 : 0, logic>

module {
  obelisk_sim.design @derived_procedural_path {
    obelisk_sim.scope.decl 0
    obelisk_sim.code_unit.decl 9960001 in 0 observer
        hierarchy "derived_procedural_path.primary"
    obelisk_sim.net.decl 0 in 0 : !obelisk_sim.logic<1> design
    obelisk_sim.net.decl 1 in 0 : !obelisk_sim.logic<1> design
    obelisk_sim.storage.decl 0 in 0 : !obelisk_sim.logic<1> design
    obelisk_sim.storage.decl 1 in 0 : i4 design
    obelisk_sim.storage.decl 2 in 0 : i64 design

    obelisk_sim.func private @primary(
        %ctx: !obelisk_sim.context {obelisk_sim.capture_kind = 0 : i32},
        %clock: !obelisk_sim.net<!obelisk_sim.logic<1>>
            {obelisk_sim.capture_kind = 4 : i32,
             obelisk_sim.descriptor_id = 0 : i64},
        %enable: !obelisk_sim.net<!obelisk_sim.logic<1>>
            {obelisk_sim.capture_kind = 4 : i32,
             obelisk_sim.descriptor_id = 1 : i64},
        %snapshot: !obelisk_sim.ref<!obelisk_sim.logic<1>>
            {obelisk_sim.capture_kind = 3 : i32,
             obelisk_sim.descriptor_id = 0 : i64},
        %pending: !obelisk_sim.ref<i4>
            {obelisk_sim.capture_kind = 3 : i32,
             obelisk_sim.descriptor_id = 1 : i64},
        %epoch: !obelisk_sim.ref<i64>
            {obelisk_sim.capture_kind = 3 : i32,
             obelisk_sim.descriptor_id = 2 : i64}) -> !obelisk_sim.logic<1>
        attributes {
          entry_kind = 14 : i32, code_unit_id = 9960001 : i64,
          obelisk_sim.observer_result = 1 : i32,
          obelisk_sim.timing_path_monitor_rules = [{
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
          obelisk_sim.bindings = [
            #obelisk_sim.argument_binding<path = "top.clock", argument = 1, kind = direct, copyOut = false>,
            #obelisk_sim.argument_binding<path = "top.enable", argument = 2, kind = direct, copyOut = false>,
            #obelisk_sim.argument_binding<path = "top.snapshot", argument = 3, kind = direct, copyOut = false>,
            #obelisk_sim.argument_binding<path = "top.pending", argument = 4, kind = direct, copyOut = false>,
            #obelisk_sim.argument_binding<path = "top.epoch", argument = 5, kind = direct, copyOut = false>]}
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
      %dummy = obelisk_sim.logic.constant false, false :
          !obelisk_sim.logic<1>
      obelisk_sim.return %dummy : !obelisk_sim.logic<1>
    }
  }
}

// CHECK-LABEL: obelisk_sim.func private @primary
// One four-state classifier is shared by the posedge and omitted-edge rules
// for a source/LSB pair. IEEE 1800-2017 30.4.3 makes an omitted edge match
// every logical transition, including transitions involving X or Z.
// CHECK-COUNT-2: obelisk_sim.logic.extract
// CHECK-COUNT-5: obelisk_sim.logic.compare
// CHECK: obelisk_sim.time.now
// CHECK: obelisk_sim.ref.load %arg4
// CHECK: obelisk_sim.ref.load %arg5
// CHECK: arith.select
// CHECK: obelisk_sim.ref.store {{.*}} to %arg4
// CHECK: obelisk_sim.ref.store {{.*}} to %arg5
// CHECK: obelisk_sim.ref.store {{.*}} to %arg3
// CHECK: obelisk_sim.logic.binary and
// CHECK: obelisk_sim.return
// CHECK-NOT: obelisk_sim.driver.drive_inertial_path
