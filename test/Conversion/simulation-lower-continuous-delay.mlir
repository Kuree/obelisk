// RUN: obelisk-opt %s --pass-pipeline='builtin.module(simulation.design(simulation.func(obelisk-sim-lower-unit)))' | FileCheck %s

!logic4 = !obelisk.integral<4, false, true, 3 : 0, logic>

module {
  simulation.design @continuous_delay {
    simulation.scope.decl 0
    simulation.code_unit.decl 1 in 0 continuous hierarchy "continuous_delay.assign"
    simulation.net.decl 0 in 0 : !simulation.logic<4> design
    simulation.net.decl 1 in 0 : !simulation.logic<4> design
    simulation.driver.decl 0 in 0 drives 0 : !simulation.logic<4> design

    simulation.func @continuous(
        %ctx: !simulation.context {simulation.capture_kind = 0 : i32},
        %out: !simulation.driver<!simulation.logic<4>> {simulation.capture_kind = 5 : i32, simulation.descriptor_id = 0 : i64},
        %input: !simulation.net<!simulation.logic<4>> {simulation.capture_kind = 4 : i32, simulation.descriptor_id = 1 : i64})
        attributes {entry_kind = 7 : i32, code_unit_id = 1 : i64,
                    simulation.propagation_delays = array<i64: 2, 5, 7>,
                    simulation.bindings = [
                      #simulation.argument_binding<path = "top.out", argument = 1, kind = lvalue_only, copyOut = false>,
                      #simulation.argument_binding<path = "top.input", argument = 2, kind = direct, copyOut = false>]} {
      obelisk.sv.expression.assignment attributes {assignment_kind = 0 : i32, node_id = 1 : i64, semantic_type = !logic4} {
        obelisk.sv.expression.named_value attributes {node_id = 2 : i64, referenced_path = "top.out", referenced_symbol = @out, semantic_type = !logic4} {}
        obelisk.sv.expression.named_value attributes {node_id = 3 : i64, referenced_path = "top.input", referenced_symbol = @input, semantic_type = !logic4} {}
      }
      simulation.return
    }
  }
}

// CHECK-LABEL: simulation.func @continuous
// CHECK: %[[INPUT:.*]] = simulation.net.read %arg2
// CHECK-DAG: %[[RISE:.*]] = simulation.time.constant 2
// CHECK-DAG: %[[FALL:.*]] = simulation.time.constant 5
// CHECK-DAG: %[[OFF:.*]] = simulation.time.constant 7
// CHECK: simulation.driver.drive_inertial %arg1 = %[[INPUT]] after[%[[RISE]], %[[FALL]], %[[OFF]]] site 1 : 0 vector = true
// CHECK: simulation.suspend.change %arg2
