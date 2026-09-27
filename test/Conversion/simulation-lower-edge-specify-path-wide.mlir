// RUN: obelisk-opt %s --pass-pipeline='builtin.module(simulation.design(simulation.func(obelisk-sim-lower-unit)))' | FileCheck %s

// A 4096-bit destination retains one source snapshot and two qualification
// cells per static edge rule. Lowering emits one packed actor and one inertial
// group per distinct delay; none of these counts scale with destination bits.

!logic4096 = !obelisk.integral<4096, false, true, 4095 : 0, logic>

module {
  simulation.design @edge_specify_wide {
    simulation.scope.decl 0
    simulation.code_unit.decl 9950101 in 0 continuous hierarchy "wide.path"
    simulation.storage.decl 0 in 0 : !simulation.logic<4096> design
    simulation.storage.decl 1 in 0 : i4096 design
    simulation.storage.decl 2 in 0 : i64 design
    simulation.storage.decl 3 in 0 : i4096 design
    simulation.storage.decl 4 in 0 : i64 design
    simulation.storage.decl 5 in 0 : i4096 design
    simulation.storage.decl 6 in 0 : i64 design
    simulation.storage.decl 7 in 0 : i4096 design
    simulation.storage.decl 8 in 0 : i64 design
    simulation.net.decl 0 in 0 : !simulation.logic<4096> design
    simulation.net.decl 1 in 0 : !simulation.logic<4096> design
    simulation.driver.decl 0 in 0 drives 1 : !simulation.logic<4096> design

    simulation.func @path(
        %ctx: !simulation.context {simulation.capture_kind = 0 : i32},
        %out: !simulation.driver<!simulation.logic<4096>>
            {simulation.capture_kind = 5 : i32,
             simulation.descriptor_id = 0 : i64},
        %source: !simulation.net<!simulation.logic<4096>>
            {simulation.capture_kind = 4 : i32,
             simulation.descriptor_id = 0 : i64},
        %snapshot: !simulation.ref<!simulation.logic<4096>>
            {simulation.capture_kind = 3 : i32,
             simulation.descriptor_id = 0 : i64},
        %data: !simulation.net<!simulation.logic<4096>>
            {simulation.capture_kind = 4 : i32,
             simulation.descriptor_id = 1 : i64},
        %pending0: !simulation.ref<i4096>
            {simulation.capture_kind = 3 : i32,
             simulation.descriptor_id = 1 : i64},
        %epoch0: !simulation.ref<i64>
            {simulation.capture_kind = 3 : i32,
             simulation.descriptor_id = 2 : i64},
        %pending1: !simulation.ref<i4096>
            {simulation.capture_kind = 3 : i32,
             simulation.descriptor_id = 3 : i64},
        %epoch1: !simulation.ref<i64>
            {simulation.capture_kind = 3 : i32,
             simulation.descriptor_id = 4 : i64},
        %pending2: !simulation.ref<i4096>
            {simulation.capture_kind = 3 : i32,
             simulation.descriptor_id = 5 : i64},
        %epoch2: !simulation.ref<i64>
            {simulation.capture_kind = 3 : i32,
             simulation.descriptor_id = 6 : i64},
        %pending3: !simulation.ref<i4096>
            {simulation.capture_kind = 3 : i32,
             simulation.descriptor_id = 7 : i64},
        %epoch3: !simulation.ref<i64>
            {simulation.capture_kind = 3 : i32,
             simulation.descriptor_id = 8 : i64})
        attributes {entry_kind = 7 : i32, code_unit_id = 9950101 : i64,
                    simulation.timing_path_rules = [
                      {inputs = ["top.source"], snapshots = ["top.snapshot"],
                       input_lows = array<i64: 0>,
                       input_widths = array<i64: 4096>,
                       input_lsbs = array<i64: 0>, output_low = 0 : i64,
                       output_width = 4096 : i64,
                       output_root_width = 4096 : i64,
                       driver_node_id = 2 : i64, connection_full = false,
                       polarity = 0 : i32, edge_sensitive = true,
                       edge_identifier = 1 : i32, edge_polarity = 0 : i32,
                       edge_pending = "top.pending0", edge_epoch = "top.epoch0",
                       delays = array<i64: 1>, condition_kind = 0 : i32,
                       condition_group = 0 : i32},
                      {inputs = ["top.source"], snapshots = ["top.snapshot"],
                       input_lows = array<i64: 0>,
                       input_widths = array<i64: 4096>,
                       input_lsbs = array<i64: 0>, output_low = 0 : i64,
                       output_width = 4096 : i64,
                       output_root_width = 4096 : i64,
                       driver_node_id = 2 : i64, connection_full = false,
                       polarity = 0 : i32, edge_sensitive = true,
                       edge_identifier = 2 : i32, edge_polarity = 0 : i32,
                       edge_pending = "top.pending1", edge_epoch = "top.epoch1",
                       delays = array<i64: 2>, condition_kind = 0 : i32,
                       condition_group = 1 : i32},
                      {inputs = ["top.source"], snapshots = ["top.snapshot"],
                       input_lows = array<i64: 0>,
                       input_widths = array<i64: 4096>,
                       input_lsbs = array<i64: 0>, output_low = 0 : i64,
                       output_width = 4096 : i64,
                       output_root_width = 4096 : i64,
                       driver_node_id = 2 : i64, connection_full = true,
                       polarity = 0 : i32, edge_sensitive = true,
                       edge_identifier = 3 : i32, edge_polarity = 0 : i32,
                       edge_pending = "top.pending2", edge_epoch = "top.epoch2",
                       delays = array<i64: 3>, condition_kind = 0 : i32,
                       condition_group = 2 : i32},
                      {inputs = ["top.source"], snapshots = ["top.snapshot"],
                       input_lows = array<i64: 0>,
                       input_widths = array<i64: 4096>,
                       input_lsbs = array<i64: 0>, output_low = 0 : i64,
                       output_width = 4096 : i64,
                       output_root_width = 4096 : i64,
                       driver_node_id = 2 : i64, connection_full = false,
                       polarity = 0 : i32, edge_sensitive = true,
                       edge_identifier = 0 : i32, edge_polarity = 0 : i32,
                       edge_pending = "top.pending3", edge_epoch = "top.epoch3",
                       delays = array<i64: 4>, condition_kind = 0 : i32,
                       condition_group = 3 : i32}],
                    simulation.bindings = [
                      #simulation.argument_binding<path = "top.out", argument = 1, kind = lvalue_only, copyOut = false>,
                      #simulation.argument_binding<path = "top.source", argument = 2, kind = direct, copyOut = false>,
                      #simulation.argument_binding<path = "top.snapshot", argument = 3, kind = direct, copyOut = false>,
                      #simulation.argument_binding<path = "top.data", argument = 4, kind = direct, copyOut = false>,
                      #simulation.argument_binding<path = "top.pending0", argument = 5, kind = direct, copyOut = false>,
                      #simulation.argument_binding<path = "top.epoch0", argument = 6, kind = direct, copyOut = false>,
                      #simulation.argument_binding<path = "top.pending1", argument = 7, kind = direct, copyOut = false>,
                      #simulation.argument_binding<path = "top.epoch1", argument = 8, kind = direct, copyOut = false>,
                      #simulation.argument_binding<path = "top.pending2", argument = 9, kind = direct, copyOut = false>,
                      #simulation.argument_binding<path = "top.epoch2", argument = 10, kind = direct, copyOut = false>,
                      #simulation.argument_binding<path = "top.pending3", argument = 11, kind = direct, copyOut = false>,
                      #simulation.argument_binding<path = "top.epoch3", argument = 12, kind = direct, copyOut = false>]} {
      obelisk.sv.expression.assignment attributes {
          assignment_kind = 0 : i32, node_id = 1 : i64,
          semantic_type = !logic4096} {
        obelisk.sv.expression.named_value attributes {
            node_id = 2 : i64, referenced_path = "top.out",
            referenced_symbol = @out, semantic_type = !logic4096} {}
        obelisk.sv.expression.named_value attributes {
            node_id = 3 : i64, referenced_path = "top.data",
            referenced_symbol = @data, semantic_type = !logic4096} {}
      }
      simulation.return
    }
  }
}

// CHECK-COUNT-9: simulation.storage.decl
// CHECK-LABEL: simulation.func @path
// CHECK-COUNT-1: simulation.time.now
// CHECK-COUNT-4: simulation.driver.drive_inertial_path
// CHECK: simulation.suspend.any
