// RUN: obelisk-opt %s --pass-pipeline='builtin.module(obelisk-sim-prepare-unit-lowering,simulation.design(simulation.func(obelisk-sim-lower-unit)))' | FileCheck %s

// Clause 30.4.3 edge identifiers qualify a path event from the selected
// source terminal. A vector source samples only its least-significant bit;
// once qualified, the event applies to the complete parallel or full
// destination selection. The data-source expression and its polarity are
// preserved by semantic import but intentionally have no runtime role.

!logic4 = !obelisk.integral<4, false, true, 3 : 0, logic>

module {
  simulation.design @edge_specify_lowering {
    simulation.scope.decl 0
    simulation.code_unit.decl 9950001 in 0 observer
        hierarchy "edge_specify_lowering.condition"
    simulation.code_unit.decl 9950002 in 0 continuous
        hierarchy "edge_specify_lowering.path"
    simulation.storage.decl 0 in 0 : !simulation.logic<8> design
    simulation.storage.decl 1 in 0 : i4 design
    simulation.storage.decl 2 in 0 : i64 design
    simulation.storage.decl 3 in 0 : i4 design
    simulation.storage.decl 4 in 0 : i64 design
    simulation.storage.decl 5 in 0 : i4 design
    simulation.storage.decl 6 in 0 : i64 design
    simulation.storage.decl 7 in 0 : i4 design
    simulation.storage.decl 8 in 0 : i64 design
    simulation.storage.decl 9 in 0 : i4 design
    simulation.storage.decl 10 in 0 : i64 design
    simulation.net.decl 0 in 0 : !simulation.logic<8> design
    simulation.net.decl 1 in 0 : !simulation.logic<4> design
    simulation.net.decl 2 in 0 : !simulation.logic<4> design
    simulation.driver.decl 0 in 0 drives 2 : !simulation.logic<4> design

    simulation.func private @condition(
        %ctx: !simulation.context {simulation.capture_kind = 0 : i32}) -> i1
        attributes {entry_kind = 14 : i32, code_unit_id = 9950001 : i64,
                    simulation.lowered} {
      %true = arith.constant true
      simulation.return %true : i1
    }

    simulation.func @path(
        %ctx: !simulation.context {simulation.capture_kind = 0 : i32},
        %out: !simulation.driver<!simulation.logic<4>>
            {simulation.capture_kind = 5 : i32,
             simulation.descriptor_id = 0 : i64},
        %clock: !simulation.net<!simulation.logic<8>>
            {simulation.capture_kind = 4 : i32,
             simulation.descriptor_id = 0 : i64},
        %snapshot: !simulation.ref<!simulation.logic<8>>
            {simulation.capture_kind = 3 : i32,
             simulation.descriptor_id = 0 : i64},
        %data: !simulation.net<!simulation.logic<4>>
            {simulation.capture_kind = 4 : i32,
             simulation.descriptor_id = 1 : i64},
        %pending0: !simulation.ref<i4>
            {simulation.capture_kind = 3 : i32,
             simulation.descriptor_id = 1 : i64},
        %epoch0: !simulation.ref<i64>
            {simulation.capture_kind = 3 : i32,
             simulation.descriptor_id = 2 : i64},
        %pending1: !simulation.ref<i4>
            {simulation.capture_kind = 3 : i32,
             simulation.descriptor_id = 3 : i64},
        %epoch1: !simulation.ref<i64>
            {simulation.capture_kind = 3 : i32,
             simulation.descriptor_id = 4 : i64},
        %pending2: !simulation.ref<i4>
            {simulation.capture_kind = 3 : i32,
             simulation.descriptor_id = 5 : i64},
        %epoch2: !simulation.ref<i64>
            {simulation.capture_kind = 3 : i32,
             simulation.descriptor_id = 6 : i64},
        %pending3: !simulation.ref<i4>
            {simulation.capture_kind = 3 : i32,
             simulation.descriptor_id = 7 : i64},
        %epoch3: !simulation.ref<i64>
            {simulation.capture_kind = 3 : i32,
             simulation.descriptor_id = 8 : i64},
        %pending4: !simulation.ref<i4>
            {simulation.capture_kind = 3 : i32,
             simulation.descriptor_id = 9 : i64},
        %epoch4: !simulation.ref<i64>
            {simulation.capture_kind = 3 : i32,
             simulation.descriptor_id = 10 : i64})
        attributes {entry_kind = 7 : i32, code_unit_id = 9950002 : i64,
                    simulation.timing_path_rules = [
                      {inputs = ["top.clock"],
                       snapshots = ["top.snapshot"],
                       input_lows = array<i64: 2>,
                       input_widths = array<i64: 4>,
                       input_lsbs = array<i64: 5>,
                       output_low = 0 : i64, output_width = 4 : i64,
                       output_root_width = 4 : i64,
                       driver_node_id = 2 : i64,
                       connection_full = false,
                       polarity = 0 : i32, edge_sensitive = true,
                       edge_identifier = 1 : i32, edge_polarity = 1 : i32,
                       edge_pending = "top.pending0", edge_epoch = "top.epoch0",
                       delays = array<i64: 1>, condition_kind = 0 : i32,
                       condition_group = 0 : i32},
                      {inputs = ["top.clock"],
                       snapshots = ["top.snapshot"],
                       input_lows = array<i64: 2>,
                       input_widths = array<i64: 4>,
                       input_lsbs = array<i64: 5>,
                       output_low = 0 : i64, output_width = 4 : i64,
                       output_root_width = 4 : i64,
                       driver_node_id = 2 : i64,
                       connection_full = false,
                       polarity = 0 : i32, edge_sensitive = true,
                       edge_identifier = 2 : i32, edge_polarity = 2 : i32,
                       edge_pending = "top.pending1", edge_epoch = "top.epoch1",
                       delays = array<i64: 2>, condition_kind = 0 : i32,
                       condition_group = 1 : i32},
                      {inputs = ["top.clock"],
                       snapshots = ["top.snapshot"],
                       input_lows = array<i64: 2>,
                       input_widths = array<i64: 4>,
                       input_lsbs = array<i64: 5>,
                       output_low = 0 : i64, output_width = 4 : i64,
                       output_root_width = 4 : i64,
                       driver_node_id = 2 : i64,
                       connection_full = true,
                       polarity = 1 : i32, edge_sensitive = true,
                       edge_identifier = 3 : i32, edge_polarity = 0 : i32,
                       edge_pending = "top.pending2", edge_epoch = "top.epoch2",
                       delays = array<i64: 3>, condition_kind = 0 : i32,
                       condition_group = 2 : i32},
                      {inputs = ["top.clock"],
                       snapshots = ["top.snapshot"],
                       input_lows = array<i64: 2>,
                       input_widths = array<i64: 4>,
                       input_lsbs = array<i64: 5>,
                       output_low = 0 : i64, output_width = 4 : i64,
                       output_root_width = 4 : i64,
                       driver_node_id = 2 : i64,
                       connection_full = false,
                       polarity = 2 : i32, edge_sensitive = true,
                       edge_identifier = 0 : i32, edge_polarity = 0 : i32,
                       edge_pending = "top.pending3", edge_epoch = "top.epoch3",
                       delays = array<i64: 4>, condition_kind = 0 : i32,
                       condition_group = 3 : i32},
                      {inputs = ["top.clock"],
                       snapshots = ["top.snapshot"],
                       input_lows = array<i64: 2>,
                       input_widths = array<i64: 4>,
                       input_lsbs = array<i64: 5>,
                       output_low = 0 : i64, output_width = 4 : i64,
                       output_root_width = 4 : i64,
                       driver_node_id = 2 : i64,
                       connection_full = false,
                       polarity = 0 : i32, edge_sensitive = true,
                       edge_identifier = 1 : i32, edge_polarity = 0 : i32,
                       edge_pending = "top.pending4", edge_epoch = "top.epoch4",
                       delays = array<i64: 5>, condition_kind = 1 : i32,
                       condition_group = 4 : i32,
                       condition_evaluator = @condition,
                       condition_captures = []}],
                    simulation.bindings = [
                      #simulation.argument_binding<path = "top.out", argument = 1, kind = lvalue_only, copyOut = false>,
                      #simulation.argument_binding<path = "top.clock", argument = 2, kind = direct, copyOut = false>,
                      #simulation.argument_binding<path = "top.snapshot", argument = 3, kind = direct, copyOut = false>,
                      #simulation.argument_binding<path = "top.data", argument = 4, kind = direct, copyOut = false>,
                      #simulation.argument_binding<path = "top.pending0", argument = 5, kind = direct, copyOut = false>,
                      #simulation.argument_binding<path = "top.epoch0", argument = 6, kind = direct, copyOut = false>,
                      #simulation.argument_binding<path = "top.pending1", argument = 7, kind = direct, copyOut = false>,
                      #simulation.argument_binding<path = "top.epoch1", argument = 8, kind = direct, copyOut = false>,
                      #simulation.argument_binding<path = "top.pending2", argument = 9, kind = direct, copyOut = false>,
                      #simulation.argument_binding<path = "top.epoch2", argument = 10, kind = direct, copyOut = false>,
                      #simulation.argument_binding<path = "top.pending3", argument = 11, kind = direct, copyOut = false>,
                      #simulation.argument_binding<path = "top.epoch3", argument = 12, kind = direct, copyOut = false>,
                      #simulation.argument_binding<path = "top.pending4", argument = 13, kind = direct, copyOut = false>,
                      #simulation.argument_binding<path = "top.epoch4", argument = 14, kind = direct, copyOut = false>]} {
      obelisk.sv.expression.assignment attributes {
          assignment_kind = 0 : i32, node_id = 1 : i64,
          semantic_type = !logic4} {
        obelisk.sv.expression.named_value attributes {
            node_id = 2 : i64, referenced_path = "top.out",
            referenced_symbol = @out, semantic_type = !logic4} {}
        obelisk.sv.expression.named_value attributes {
            node_id = 3 : i64, referenced_path = "top.data",
            referenced_symbol = @data, semantic_type = !logic4} {}
      }
      simulation.return
    }
  }
}

// CHECK-LABEL: simulation.func @path
// One packed ordinary difference mask plus four scalar zero/one comparisons
// construct both edge classes once for the shared semantic LSB.
// CHECK-COUNT-1: simulation.logic.case_difference_mask
// The selected vector starts at physical bit two, but its frozen semantic LSB
// is physical bit five (the shape produced by an ascending terminal). Every
// explicit edge kind samples one bit there instead of assuming the low offset.
// CHECK-COUNT-2: simulation.logic.extract {{.*}} from 5
// CHECK-COUNT-4: simulation.logic.compare case_eq
// CHECK-DAG: arith.shrui
// Same-time qualification survives an Active-to-NBA-to-Active derived-output
// update, but expires when the scheduler clock advances.
// CHECK-DAG: simulation.time.now
// CHECK-DAG: simulation.ref.load %arg5
// CHECK-DAG: simulation.ref.load %arg6
// The conditional rule samples its predicate only after the source event is
// classified; suffix and path polarity do not emit data transforms.
// CHECK-DAG: simulation.call @condition(%arg0)
// After the earlier shared source-snapshot store, ten qualification stores
// plus five write-site consumption stores prove pending state is retired
// after pairing.
// CHECK-COUNT-15: simulation.ref.store
// CHECK-COUNT-5: simulation.driver.drive_inertial_path
// CHECK: simulation.suspend.any
