// RUN: obelisk-opt %s --pass-pipeline='builtin.module(obelisk_sim.design(obelisk_sim.func(obelisk-sim-lower-unit)))' | FileCheck %s

!logic4 = !obelisk.integral<4, false, true, 3 : 0, logic>

module {
  obelisk_sim.design @continuous_delay {
    obelisk_sim.scope.decl 0
    obelisk_sim.code_unit.decl 1 in 0 continuous hierarchy "continuous_delay.assign"
    obelisk_sim.net.decl 0 in 0 : !obelisk_sim.logic<4> design
    obelisk_sim.net.decl 1 in 0 : !obelisk_sim.logic<4> design
    obelisk_sim.driver.decl 0 in 0 drives 0 : !obelisk_sim.logic<4> design

    obelisk_sim.func @continuous(
        %ctx: !obelisk_sim.context {obelisk_sim.capture_kind = 0 : i32},
        %out: !obelisk_sim.driver<!obelisk_sim.logic<4>> {obelisk_sim.capture_kind = 5 : i32, obelisk_sim.descriptor_id = 0 : i64},
        %input: !obelisk_sim.net<!obelisk_sim.logic<4>> {obelisk_sim.capture_kind = 4 : i32, obelisk_sim.descriptor_id = 1 : i64})
        attributes {entry_kind = 7 : i32, code_unit_id = 1 : i64,
                    obelisk_sim.propagation_delays = array<i64: 2, 5, 7>,
                    obelisk_sim.bindings = [
                      #obelisk_sim.argument_binding<path = "top.out", argument = 1, kind = lvalue_only, copyOut = false>,
                      #obelisk_sim.argument_binding<path = "top.input", argument = 2, kind = direct, copyOut = false>]} {
      obelisk.sv.expression.assignment attributes {assignment_kind = 0 : i32, node_id = 1 : i64, semantic_type = !logic4} {
        obelisk.sv.expression.named_value attributes {node_id = 2 : i64, referenced_path = "top.out", referenced_symbol = @out, semantic_type = !logic4} {}
        obelisk.sv.expression.named_value attributes {node_id = 3 : i64, referenced_path = "top.input", referenced_symbol = @input, semantic_type = !logic4} {}
      }
      obelisk_sim.return
    }
  }
}

// CHECK-LABEL: obelisk_sim.func @continuous
// CHECK: %[[INPUT:.*]] = obelisk_sim.net.read %arg2
// CHECK-DAG: %[[RISE:.*]] = obelisk_sim.time.constant 2
// CHECK-DAG: %[[FALL:.*]] = obelisk_sim.time.constant 5
// CHECK-DAG: %[[OFF:.*]] = obelisk_sim.time.constant 7
// CHECK: obelisk_sim.driver.drive_inertial %arg1 = %[[INPUT]] after[%[[RISE]], %[[FALL]], %[[OFF]]] site 1 : 0 vector = true
// CHECK: obelisk_sim.suspend.change %arg2
