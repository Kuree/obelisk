// RUN: obelisk-opt %s --pass-pipeline='builtin.module(obelisk_sim.design(obelisk_sim.func(obelisk-sim-lower-unit)))' | FileCheck %s

!logic1 = !obelisk.integral<1, false, true, 0 : 0, logic>

module {
  obelisk_sim.design @sequential_udp_lowering {
    obelisk_sim.scope.decl 0
    obelisk_sim.code_unit.decl 9220001 in 0 continuous hierarchy "top.edge"
    obelisk_sim.storage.decl 0 in 0 : !obelisk_sim.logic<1> design
    obelisk_sim.net.decl 0 in 0 : !obelisk_sim.logic<1> design
    obelisk_sim.driver.decl 0 in 0 drives 0 : !obelisk_sim.logic<1> design

    // CHECK-LABEL: obelisk_sim.func @edge
    // CHECK-NOT: obelisk_sim.udp_metadata
    // The previous input starts at X and is explicitly loop-carried.
    // CHECK: %[[INITIAL_PREV:.*]] = obelisk_sim.logic.constant false, true
    // CHECK: cf.br ^[[LOOP:.*]](%[[INITIAL_PREV]] : !obelisk_sim.logic<1>)
    // CHECK: ^[[LOOP]](%[[PREVIOUS:.*]]: !obelisk_sim.logic<1>):
    // CHECK: %[[CURRENT:.*]] = obelisk_sim.ref.load %arg1
    // CHECK: %[[NORMAL:.*]] = obelisk_sim.logic.binary and %[[CURRENT]]
    // The raw driver is the committed sequential state. Raw Z selects and
    // immediately publishes the declaration initializer.
    // CHECK: %[[RAW:.*]] = obelisk_sim.driver.read %arg3
    // CHECK: %[[UNINIT:.*]] = obelisk_sim.logic.compare case_eq %[[RAW]]
    // CHECK: %[[STATE:.*]] = arith.select %[[UNINIT]]
    // CHECK-NOT: obelisk_sim.initial_driver_x
    // CHECK: obelisk_sim.driver.drive_changed %arg3 = %[[STATE]]
    // CHECK: obelisk_sim.logic.concat %[[NORMAL]], %[[STATE]]
    // Explicit (b?) uses direct known-set predicates plus a real-change test.
    // `p` expands to 0->1, 0->X, and X->1 predicates. The current-state field
    // is concatenated once and every row becomes straight-line selects.
    // CHECK-COUNT-2: arith.ori
    // CHECK: obelisk_sim.logic.compare case_ne
    // CHECK: obelisk_sim.driver.drive %arg3
    // CHECK: obelisk_sim.suspend.change %arg1 to ^[[LOOP]](%[[NORMAL]] : !obelisk_sim.logic<1>)
    obelisk_sim.func @edge(
        %ctx: !obelisk_sim.context {obelisk_sim.capture_kind = 0 : i32},
        %in: !obelisk_sim.ref<!obelisk_sim.logic<1>> {obelisk_sim.capture_kind = 3 : i32, obelisk_sim.descriptor_id = 0 : i64},
        %net: !obelisk_sim.net<!obelisk_sim.logic<1>> {obelisk_sim.capture_kind = 4 : i32, obelisk_sim.descriptor_id = 0 : i64},
        %out: !obelisk_sim.driver<!obelisk_sim.logic<1>> {obelisk_sim.capture_kind = 5 : i32, obelisk_sim.descriptor_id = 0 : i64})
        attributes {entry_kind = 7 : i32, code_unit_id = 9220001 : i64,
                    obelisk_sim.primitive_name = "udp_edge",
                    obelisk_sim.udp_metadata = {
                      init_value = "1'b0", is_edge_sensitive = true,
                      is_sequential = true, name = "udp_edge",
                      port_directions = array<i64: 2, 0>,
                      port_names = ["out", "in"],
                      table_edges = array<i64: 1, 1>,
                      table_inputs = ["(b?)", "p"],
                      table_outputs = array<i64: 49, 49>,
                      table_states = array<i64: 63, 63>},
                    obelisk_sim.bindings = [
                      #obelisk_sim.argument_binding<path = "top.in", argument = 1, kind = direct, copyOut = false>,
                      #obelisk_sim.argument_binding<path = "top.out", argument = 3, kind = lvalue_only, copyOut = false>]} {
      obelisk.sv.expression.assignment attributes {assignment_kind = 0 : i32, node_id = 1 : i64, semantic_type = !logic1} {
        obelisk.sv.expression.named_value attributes {node_id = 2 : i64, referenced_path = "top.out", referenced_symbol = @out, semantic_type = !logic1} {}
        obelisk.sv.expression.empty_argument attributes {node_id = 3 : i64, semantic_type = !logic1} {}
      }
      obelisk.sv.expression.named_value attributes {node_id = 4 : i64, referenced_path = "top.in", referenced_symbol = @in, semantic_type = !logic1} {}
      obelisk_sim.return
    }
  }
}
