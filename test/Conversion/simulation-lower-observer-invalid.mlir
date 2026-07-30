// RUN: not obelisk-opt %s --pass-pipeline='builtin.module(obelisk_sim.design(obelisk_sim.func(obelisk-sim-lower-unit)))' 2>&1 | FileCheck %s

// Observer result metadata crosses the prepare/unit-lowering boundary. Reject
// unknown values instead of silently treating them as packed-value observers.

!logic8 = !obelisk.integral<8, false, true, 7 : 0, logic>

module {
  obelisk_sim.design @invalid_observer {
    obelisk_sim.code_unit.decl 1 in 0 observer hierarchy "invalid_observer"
    obelisk_sim.scope.decl 0

    // CHECK: error: unknown observer result kind 99
    obelisk_sim.func private @evaluate(
        %ctx: !obelisk_sim.context {obelisk_sim.capture_kind = 0 : i32})
        -> !obelisk_sim.logic<8>
        attributes {
          entry_kind = 14 : i32,
          code_unit_id = 1 : i64,
          obelisk_sim.observer_result = 99 : i32,
          obelisk_sim.observer_width = 8 : i32,
          obelisk_sim.observer_four_state = true
        } {
      obelisk.sv.expression.integer_literal attributes {
          node_id = 1 : i64, constant_value = "8'h5a",
          semantic_type = !logic8} {
      }
      %placeholder = obelisk_sim.logic.constant 0 : i8, 0 : i8
          : !obelisk_sim.logic<8>
      obelisk_sim.return %placeholder : !obelisk_sim.logic<8>
    }
  }
}
