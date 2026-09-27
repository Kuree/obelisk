// RUN: not obelisk-opt %s --pass-pipeline='builtin.module(simulation.design(simulation.func(obelisk-sim-lower-unit)))' 2>&1 | FileCheck %s

// Observer result metadata crosses the prepare/unit-lowering boundary. Reject
// unknown values instead of silently treating them as packed-value observers.

!logic8 = !obelisk.integral<8, false, true, 7 : 0, logic>

module {
  simulation.design @invalid_observer {
    simulation.code_unit.decl 1 in 0 observer hierarchy "invalid_observer"
    simulation.scope.decl 0

    // CHECK: error: unknown observer result kind 99
    simulation.func private @evaluate(
        %ctx: !simulation.context {simulation.capture_kind = 0 : i32})
        -> !simulation.logic<8>
        attributes {
          entry_kind = 14 : i32,
          code_unit_id = 1 : i64,
          simulation.observer_result = 99 : i32,
          schedule.observer_width = 8 : i32,
          schedule.observer_four_state = true
        } {
      obelisk.sv.expression.integer_literal attributes {
          node_id = 1 : i64, constant_value = "8'h5a",
          semantic_type = !logic8} {
      }
      %placeholder = simulation.logic.constant 0 : i8, 0 : i8
          : !simulation.logic<8>
      simulation.return %placeholder : !simulation.logic<8>
    }
  }
}
