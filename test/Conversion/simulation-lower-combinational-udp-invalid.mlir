// RUN: not talos-opt %s --split-input-file --pass-pipeline='builtin.module(talos-sim-prepare-unit-lowering,simulation.design(simulation.func(talos-sim-lower-unit)))' 2>&1 | FileCheck %s

!logic1 = !talos.integral<1, false, true, 0 : 0, logic>

module {
  simulation.design @bad_udp_ports {
    simulation.scope.decl 0
    simulation.code_unit.decl 1 in 0 continuous hierarchy "top.bad"
    simulation.storage.decl 0 in 0 : !simulation.logic<1> design
    simulation.net.decl 0 in 0 : !simulation.logic<1> design
    simulation.driver.decl 0 in 0 drives 0 : !simulation.logic<1> design
    // CHECK: error: UDP ports must be one output followed by inputs
    simulation.func @bad(
        %ctx: !simulation.context {simulation.capture_kind = 0 : i32},
        %out: !simulation.driver<!simulation.logic<1>> {simulation.capture_kind = 5 : i32, simulation.descriptor_id = 0 : i64},
        %in: !simulation.ref<!simulation.logic<1>> {simulation.capture_kind = 3 : i32, simulation.descriptor_id = 0 : i64})
        attributes {entry_kind = 7 : i32, code_unit_id = 1 : i64,
                    schedule.primitive_name = "bad",
                    simulation.udp_metadata = #simulation.udp<name = "bad", ports = ["out", "in"], portDirections = [0, 0], sequential = false, edgeSensitive = false,
                      rows = [#simulation.udp_row<inputs = "?", state = "", output = "0", edge = false>]>,
                    simulation.bindings = [
                      #simulation.argument_binding<path = "top.out", argument = 1, kind = lvalue_only, copyOut = false>,
                      #simulation.argument_binding<path = "top.in", argument = 2, kind = direct, copyOut = false>]} {
      talos.sv.expression.assignment attributes {assignment_kind = 0 : i32, node_id = 1 : i64, semantic_type = !logic1} {
        talos.sv.expression.named_value attributes {node_id = 2 : i64, referenced_path = "top.out", referenced_symbol = @out, semantic_type = !logic1} {}
        talos.sv.expression.empty_argument attributes {node_id = 3 : i64, semantic_type = !logic1} {}
      }
      talos.sv.expression.named_value attributes {node_id = 4 : i64, referenced_path = "top.in", referenced_symbol = @in, semantic_type = !logic1} {}
      simulation.return
    }
  }
}

// -----

!logic1_seq = !talos.integral<1, false, true, 0 : 0, logic>

module {
  simulation.design @sequential_udp {
    simulation.scope.decl 0
    simulation.code_unit.decl 2 in 0 continuous hierarchy "top.seq"
    simulation.storage.decl 0 in 0 : !simulation.logic<1> design
    simulation.net.decl 0 in 0 : !simulation.logic<1> design
    simulation.driver.decl 0 in 0 drives 0 : !simulation.logic<1> design
    // CHECK: error: sequential UDP initial value must be 0, 1, or x
    simulation.func @seq(
        %ctx: !simulation.context {simulation.capture_kind = 0 : i32},
        %out: !simulation.driver<!simulation.logic<1>> {simulation.capture_kind = 5 : i32, simulation.descriptor_id = 0 : i64},
        %in: !simulation.ref<!simulation.logic<1>> {simulation.capture_kind = 3 : i32, simulation.descriptor_id = 0 : i64})
        attributes {entry_kind = 7 : i32, code_unit_id = 2 : i64,
                    schedule.primitive_name = "seq",
                    simulation.udp_metadata = #simulation.udp<name = "seq", ports = ["out", "in"], portDirections = [2, 0], sequential = true, edgeSensitive = false, initValue = "1'bz",
                      rows = [#simulation.udp_row<inputs = "0", state = "?", output = "0", edge = false>]>,
                    simulation.bindings = [
                      #simulation.argument_binding<path = "top.out", argument = 1, kind = lvalue_only, copyOut = false>,
                      #simulation.argument_binding<path = "top.in", argument = 2, kind = direct, copyOut = false>]} {
      talos.sv.expression.assignment attributes {assignment_kind = 0 : i32, node_id = 1 : i64, semantic_type = !logic1_seq} {
        talos.sv.expression.named_value attributes {node_id = 2 : i64, referenced_path = "top.out", referenced_symbol = @out, semantic_type = !logic1_seq} {}
        talos.sv.expression.empty_argument attributes {node_id = 3 : i64, semantic_type = !logic1_seq} {}
      }
      talos.sv.expression.named_value attributes {node_id = 4 : i64, referenced_path = "top.in", referenced_symbol = @in, semantic_type = !logic1_seq} {}
      simulation.return
    }
  }
}

// -----

!logic1_row = !talos.integral<1, false, true, 0 : 0, logic>

module {
  simulation.design @bad_udp_row {
    simulation.scope.decl 0
    simulation.code_unit.decl 3 in 0 continuous hierarchy "top.row"
    simulation.storage.decl 0 in 0 : !simulation.logic<1> design
    simulation.net.decl 0 in 0 : !simulation.logic<1> design
    simulation.driver.decl 0 in 0 drives 0 : !simulation.logic<1> design
    // CHECK: error: malformed UDP table row
    simulation.func @row(
        %ctx: !simulation.context {simulation.capture_kind = 0 : i32},
        %out: !simulation.driver<!simulation.logic<1>> {simulation.capture_kind = 5 : i32, simulation.descriptor_id = 0 : i64},
        %in: !simulation.ref<!simulation.logic<1>> {simulation.capture_kind = 3 : i32, simulation.descriptor_id = 0 : i64})
        attributes {entry_kind = 7 : i32, code_unit_id = 3 : i64,
                    schedule.primitive_name = "row",
                    simulation.udp_metadata = #simulation.udp<name = "row", ports = ["out", "in"], portDirections = [1, 0], sequential = false, edgeSensitive = false,
                      rows = [#simulation.udp_row<inputs = "0", state = "?", output = "-", edge = true>]>,
                    simulation.bindings = [
                      #simulation.argument_binding<path = "top.out", argument = 1, kind = lvalue_only, copyOut = false>,
                      #simulation.argument_binding<path = "top.in", argument = 2, kind = direct, copyOut = false>]} {
      talos.sv.expression.assignment attributes {assignment_kind = 0 : i32, node_id = 1 : i64, semantic_type = !logic1_row} {
        talos.sv.expression.named_value attributes {node_id = 2 : i64, referenced_path = "top.out", referenced_symbol = @out, semantic_type = !logic1_row} {}
        talos.sv.expression.empty_argument attributes {node_id = 3 : i64, semantic_type = !logic1_row} {}
      }
      talos.sv.expression.named_value attributes {node_id = 4 : i64, referenced_path = "top.in", referenced_symbol = @in, semantic_type = !logic1_row} {}
      simulation.return
    }
  }
}
