// RUN: not obelisk-opt %s --split-input-file --pass-pipeline='builtin.module(obelisk_sim.design(obelisk_sim.func(obelisk-sim-lower-unit)))' 2>&1 | FileCheck %s

!logic1 = !obelisk.integral<1, false, true, 0 : 0, logic>

module {
  obelisk_sim.design @bad_udp_ports {
    obelisk_sim.scope.decl 0
    obelisk_sim.code_unit.decl 1 in 0 continuous hierarchy "top.bad"
    obelisk_sim.storage.decl 0 in 0 : !obelisk_sim.logic<1> design
    obelisk_sim.net.decl 0 in 0 : !obelisk_sim.logic<1> design
    obelisk_sim.driver.decl 0 in 0 drives 0 : !obelisk_sim.logic<1> design
    // CHECK: error: UDP ports must be one output followed by inputs
    obelisk_sim.func @bad(
        %ctx: !obelisk_sim.context {obelisk_sim.capture_kind = 0 : i32},
        %out: !obelisk_sim.driver<!obelisk_sim.logic<1>> {obelisk_sim.capture_kind = 5 : i32, obelisk_sim.descriptor_id = 0 : i64},
        %in: !obelisk_sim.ref<!obelisk_sim.logic<1>> {obelisk_sim.capture_kind = 3 : i32, obelisk_sim.descriptor_id = 0 : i64})
        attributes {entry_kind = 7 : i32, code_unit_id = 1 : i64,
                    schedule.primitive_name = "bad",
                    obelisk_sim.udp_metadata = {
                      is_edge_sensitive = false, is_sequential = false,
                      name = "bad", port_directions = array<i64: 0, 0>,
                      port_names = ["out", "in"], table_edges = array<i64: 0>,
                      table_inputs = ["?"], table_outputs = array<i64: 48>,
                      table_states = array<i64: 0>},
                    obelisk_sim.bindings = [
                      #obelisk_sim.argument_binding<path = "top.out", argument = 1, kind = lvalue_only, copyOut = false>,
                      #obelisk_sim.argument_binding<path = "top.in", argument = 2, kind = direct, copyOut = false>]} {
      obelisk.sv.expression.assignment attributes {assignment_kind = 0 : i32, node_id = 1 : i64, semantic_type = !logic1} {
        obelisk.sv.expression.named_value attributes {node_id = 2 : i64, referenced_path = "top.out", referenced_symbol = @out, semantic_type = !logic1} {}
        obelisk.sv.expression.empty_argument attributes {node_id = 3 : i64, semantic_type = !logic1} {}
      }
      obelisk.sv.expression.named_value attributes {node_id = 4 : i64, referenced_path = "top.in", referenced_symbol = @in, semantic_type = !logic1} {}
      obelisk_sim.return
    }
  }
}

// -----

!logic1_seq = !obelisk.integral<1, false, true, 0 : 0, logic>

module {
  obelisk_sim.design @sequential_udp {
    obelisk_sim.scope.decl 0
    obelisk_sim.code_unit.decl 2 in 0 continuous hierarchy "top.seq"
    obelisk_sim.storage.decl 0 in 0 : !obelisk_sim.logic<1> design
    obelisk_sim.net.decl 0 in 0 : !obelisk_sim.logic<1> design
    obelisk_sim.driver.decl 0 in 0 drives 0 : !obelisk_sim.logic<1> design
    // CHECK: error: sequential UDP initial value must be 0, 1, or x
    obelisk_sim.func @seq(
        %ctx: !obelisk_sim.context {obelisk_sim.capture_kind = 0 : i32},
        %out: !obelisk_sim.driver<!obelisk_sim.logic<1>> {obelisk_sim.capture_kind = 5 : i32, obelisk_sim.descriptor_id = 0 : i64},
        %in: !obelisk_sim.ref<!obelisk_sim.logic<1>> {obelisk_sim.capture_kind = 3 : i32, obelisk_sim.descriptor_id = 0 : i64})
        attributes {entry_kind = 7 : i32, code_unit_id = 2 : i64,
                    schedule.primitive_name = "seq",
                    obelisk_sim.udp_metadata = {
                      init_value = "1'bz", is_edge_sensitive = false,
                      is_sequential = true, name = "seq",
                      port_directions = array<i64: 2, 0>,
                      port_names = ["out", "in"], table_edges = array<i64: 0>,
                      table_inputs = ["0"], table_outputs = array<i64: 48>,
                      table_states = array<i64: 63>},
                    obelisk_sim.bindings = [
                      #obelisk_sim.argument_binding<path = "top.out", argument = 1, kind = lvalue_only, copyOut = false>,
                      #obelisk_sim.argument_binding<path = "top.in", argument = 2, kind = direct, copyOut = false>]} {
      obelisk.sv.expression.assignment attributes {assignment_kind = 0 : i32, node_id = 1 : i64, semantic_type = !logic1_seq} {
        obelisk.sv.expression.named_value attributes {node_id = 2 : i64, referenced_path = "top.out", referenced_symbol = @out, semantic_type = !logic1_seq} {}
        obelisk.sv.expression.empty_argument attributes {node_id = 3 : i64, semantic_type = !logic1_seq} {}
      }
      obelisk.sv.expression.named_value attributes {node_id = 4 : i64, referenced_path = "top.in", referenced_symbol = @in, semantic_type = !logic1_seq} {}
      obelisk_sim.return
    }
  }
}

// -----

!logic1_row = !obelisk.integral<1, false, true, 0 : 0, logic>

module {
  obelisk_sim.design @bad_udp_row {
    obelisk_sim.scope.decl 0
    obelisk_sim.code_unit.decl 3 in 0 continuous hierarchy "top.row"
    obelisk_sim.storage.decl 0 in 0 : !obelisk_sim.logic<1> design
    obelisk_sim.net.decl 0 in 0 : !obelisk_sim.logic<1> design
    obelisk_sim.driver.decl 0 in 0 drives 0 : !obelisk_sim.logic<1> design
    // CHECK: error: malformed UDP table row
    obelisk_sim.func @row(
        %ctx: !obelisk_sim.context {obelisk_sim.capture_kind = 0 : i32},
        %out: !obelisk_sim.driver<!obelisk_sim.logic<1>> {obelisk_sim.capture_kind = 5 : i32, obelisk_sim.descriptor_id = 0 : i64},
        %in: !obelisk_sim.ref<!obelisk_sim.logic<1>> {obelisk_sim.capture_kind = 3 : i32, obelisk_sim.descriptor_id = 0 : i64})
        attributes {entry_kind = 7 : i32, code_unit_id = 3 : i64,
                    schedule.primitive_name = "row",
                    obelisk_sim.udp_metadata = {
                      is_edge_sensitive = false, is_sequential = false,
                      name = "row", port_directions = array<i64: 1, 0>,
                      port_names = ["out", "in"], table_edges = array<i64: 1>,
                      table_inputs = ["0"], table_outputs = array<i64: 45>,
                      table_states = array<i64: 63>},
                    obelisk_sim.bindings = [
                      #obelisk_sim.argument_binding<path = "top.out", argument = 1, kind = lvalue_only, copyOut = false>,
                      #obelisk_sim.argument_binding<path = "top.in", argument = 2, kind = direct, copyOut = false>]} {
      obelisk.sv.expression.assignment attributes {assignment_kind = 0 : i32, node_id = 1 : i64, semantic_type = !logic1_row} {
        obelisk.sv.expression.named_value attributes {node_id = 2 : i64, referenced_path = "top.out", referenced_symbol = @out, semantic_type = !logic1_row} {}
        obelisk.sv.expression.empty_argument attributes {node_id = 3 : i64, semantic_type = !logic1_row} {}
      }
      obelisk.sv.expression.named_value attributes {node_id = 4 : i64, referenced_path = "top.in", referenced_symbol = @in, semantic_type = !logic1_row} {}
      obelisk_sim.return
    }
  }
}
