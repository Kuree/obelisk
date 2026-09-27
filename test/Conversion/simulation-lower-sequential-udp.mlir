// RUN: obelisk-opt %s --pass-pipeline='builtin.module(simulation.design(simulation.func(obelisk-sim-lower-unit)))' | FileCheck %s

!logic1 = !obelisk.integral<1, false, true, 0 : 0, logic>

module {
  simulation.design @sequential_udp_lowering {
    simulation.scope.decl 0
    simulation.code_unit.decl 9220001 in 0 continuous hierarchy "top.edge"
    simulation.storage.decl 0 in 0 : !simulation.logic<1> design
    simulation.net.decl 0 in 0 : !simulation.logic<1> design
    simulation.driver.decl 0 in 0 drives 0 : !simulation.logic<1> design

    // CHECK-LABEL: simulation.func @edge
    // CHECK-NOT: simulation.udp_metadata
    // The previous input starts at X and is explicitly loop-carried.
    // CHECK: %[[INITIAL_PREV:.*]] = simulation.logic.constant false, true
    // CHECK: cf.br ^[[LOOP:.*]](%[[INITIAL_PREV]] : !simulation.logic<1>)
    // CHECK: ^[[LOOP]](%[[PREVIOUS:.*]]: !simulation.logic<1>):
    // CHECK: %[[CURRENT:.*]] = simulation.ref.load %arg1
    // CHECK: %[[NORMAL:.*]] = simulation.logic.binary and %[[CURRENT]]
    // The raw driver is the committed sequential state. Raw Z selects and
    // immediately publishes the declaration initializer.
    // CHECK: %[[RAW:.*]] = simulation.driver.read %arg3
    // CHECK: %[[UNINIT:.*]] = simulation.logic.compare case_eq %[[RAW]]
    // CHECK: %[[STATE:.*]] = arith.select %[[UNINIT]]
    // CHECK-NOT: simulation.initial_driver_x
    // CHECK: simulation.driver.drive_changed %arg3 = %[[STATE]]
    // CHECK: simulation.logic.concat %[[NORMAL]], %[[STATE]]
    // Explicit (b?) uses direct known-set predicates plus a real-change test.
    // `p` expands to 0->1, 0->X, and X->1 predicates. The current-state field
    // is concatenated once and every row becomes straight-line selects.
    // CHECK-COUNT-2: arith.ori
    // CHECK: simulation.logic.compare case_ne
    // CHECK: simulation.driver.drive %arg3
    // CHECK: simulation.suspend.change %arg1 to ^[[LOOP]](%[[NORMAL]] : !simulation.logic<1>)
    simulation.func @edge(
        %ctx: !simulation.context {simulation.capture_kind = 0 : i32},
        %in: !simulation.ref<!simulation.logic<1>> {simulation.capture_kind = 3 : i32, simulation.descriptor_id = 0 : i64},
        %net: !simulation.net<!simulation.logic<1>> {simulation.capture_kind = 4 : i32, simulation.descriptor_id = 0 : i64},
        %out: !simulation.driver<!simulation.logic<1>> {simulation.capture_kind = 5 : i32, simulation.descriptor_id = 0 : i64})
        attributes {entry_kind = 7 : i32, code_unit_id = 9220001 : i64,
                    schedule.primitive_name = "udp_edge",
                    simulation.udp_metadata = {
                      init_value = "1'b0", is_edge_sensitive = true,
                      is_sequential = true, name = "udp_edge",
                      port_directions = array<i64: 2, 0>,
                      port_names = ["out", "in"],
                      table_edges = array<i64: 1, 1>,
                      table_inputs = ["(b?)", "p"],
                      table_outputs = array<i64: 49, 49>,
                      table_states = array<i64: 63, 63>},
                    simulation.bindings = [
                      #simulation.argument_binding<path = "top.in", argument = 1, kind = direct, copyOut = false>,
                      #simulation.argument_binding<path = "top.out", argument = 3, kind = lvalue_only, copyOut = false>]} {
      obelisk.sv.expression.assignment attributes {assignment_kind = 0 : i32, node_id = 1 : i64, semantic_type = !logic1} {
        obelisk.sv.expression.named_value attributes {node_id = 2 : i64, referenced_path = "top.out", referenced_symbol = @out, semantic_type = !logic1} {}
        obelisk.sv.expression.empty_argument attributes {node_id = 3 : i64, semantic_type = !logic1} {}
      }
      obelisk.sv.expression.named_value attributes {node_id = 4 : i64, referenced_path = "top.in", referenced_symbol = @in, semantic_type = !logic1} {}
      simulation.return
    }
  }
}
