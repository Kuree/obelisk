// RUN: obelisk-opt %s --pass-pipeline='builtin.module(obelisk-sim-prepare-unit-lowering,simulation.design(simulation.func(obelisk-sim-lower-unit)))' | FileCheck %s

!logic1 = !obelisk.integral<1, false, true, 0 : 0, logic>

module {
  simulation.design @combinational_udp {
    simulation.scope.decl 0
    simulation.code_unit.decl 9200001 in 0 continuous hierarchy "top.udp"
    simulation.storage.decl 0 in 0 : !simulation.logic<1> design
    simulation.storage.decl 1 in 0 : !simulation.logic<1> design
    simulation.net.decl 0 in 0 : !simulation.logic<1> design
    simulation.driver.decl 0 in 0 drives 0 : !simulation.logic<1> design

    // CHECK-LABEL: simulation.func @udp
    // CHECK-NOT: simulation.udp_metadata
    // CHECK: %[[A:.*]] = simulation.ref.load %arg2
    // CHECK: %[[B:.*]] = simulation.ref.load %arg3
    // CHECK: %[[PACKED:.*]] = simulation.logic.concat %[[A]], %[[B]] : (!simulation.logic<1>, !simulation.logic<1>) -> !simulation.logic<2>
    // CHECK: %[[ONES:.*]] = simulation.logic.constant -1 : i2, 0 : i2 : !simulation.logic<2>
    // CHECK: %[[NORMAL:.*]] = simulation.logic.binary and %[[PACKED]], %[[ONES]]
    // Reverse row construction preserves source-order first-match behavior.
    // `x0` is pattern data=0, unknown=2, proving the first UDP input occupies
    // the high concatenation bit.
    // CHECK: %[[X0:.*]] = simulation.logic.constant 0 : i2, -2 : i2 : !simulation.logic<2>
    // The `1b` row tests first-input data mask 2 and second-input known mask 1.
    // CHECK: simulation.logic.constant -2 : i2, 0 : i2 : !simulation.logic<2>
    // CHECK: simulation.logic.constant 1 : i2, 0 : i2 : !simulation.logic<2>
    // CHECK: arith.andi
    // CHECK: simulation.driver.drive_inertial %arg1 = %{{.*}} after[%{{.*}}, %{{.*}}, %{{.*}}] site 9200001 : 0 vector = false
    // CHECK: simulation.suspend.any %arg2, %arg3
    simulation.func @udp(
        %ctx: !simulation.context {simulation.capture_kind = 0 : i32},
        %out: !simulation.driver<!simulation.logic<1>> {simulation.capture_kind = 5 : i32, simulation.descriptor_id = 0 : i64},
        %a: !simulation.ref<!simulation.logic<1>> {simulation.capture_kind = 3 : i32, simulation.descriptor_id = 0 : i64},
        %b: !simulation.ref<!simulation.logic<1>> {simulation.capture_kind = 3 : i32, simulation.descriptor_id = 1 : i64})
        attributes {entry_kind = 7 : i32, code_unit_id = 9200001 : i64,
                    schedule.primitive_name = "udp_nonansi",
                    simulation.propagation_delays = array<i64: 2, 3>,
                    simulation.udp_metadata = {
                      is_edge_sensitive = false,
                      is_sequential = false,
                      name = "udp_nonansi",
                      port_directions = array<i64: 1, 0, 0>,
                      port_names = ["out", "a", "b"],
                      table_edges = array<i64: 0, 0, 0, 0>,
                      table_inputs = ["00", "0?", "1b", "x0"],
                      table_outputs = array<i64: 48, 48, 49, 49>,
                      table_states = array<i64: 0, 0, 0, 0>},
                    simulation.bindings = [
                      #simulation.argument_binding<path = "top.out", argument = 1, kind = lvalue_only, copyOut = false>,
                      #simulation.argument_binding<path = "top.a", argument = 2, kind = direct, copyOut = false>,
                      #simulation.argument_binding<path = "top.b", argument = 3, kind = direct, copyOut = false>]} {
      obelisk.sv.expression.assignment attributes {assignment_kind = 0 : i32, node_id = 10 : i64, semantic_type = !logic1} {
        obelisk.sv.expression.named_value attributes {node_id = 11 : i64, referenced_path = "top.out", referenced_symbol = @out, semantic_type = !logic1} {}
        obelisk.sv.expression.empty_argument attributes {node_id = 12 : i64, semantic_type = !logic1} {}
      }
      obelisk.sv.expression.named_value attributes {node_id = 13 : i64, referenced_path = "top.a", referenced_symbol = @a, semantic_type = !logic1} {}
      obelisk.sv.expression.named_value attributes {node_id = 14 : i64, referenced_path = "top.b", referenced_symbol = @b, semantic_type = !logic1} {}
      simulation.return
    }
  }
}
