// RUN: obelisk-opt %s --test-obelisk-sim-state-domain -o /dev/null 2>&1 | FileCheck %s
module {
  simulation.design @partial_knownness {
    simulation.scope.decl 0
    simulation.code_unit.decl 9000001 in 0 initial hierarchy "test.partial_knownness"
    simulation.func @rules(
        %ctx: !simulation.context {simulation.capture_kind = 0 : i32},
        %input: !simulation.logic<8> {simulation.capture_kind = 2 : i32})
        attributes {entry_kind = 1 : i32, code_unit_id = 9000001 : i64} {
      %zero = simulation.logic.constant 0 : i8, 0 : i8 : !simulation.logic<8>
      %one = simulation.logic.constant 1 : i8, 0 : i8 : !simulation.logic<8>
      %mask = simulation.logic.constant 15 : i8, 0 : i8 : !simulation.logic<8>
      %masked = simulation.logic.binary and %input, %mask : !simulation.logic<8>
      %known_high = simulation.logic.extract %masked from 4 : !simulation.logic<8> -> !simulation.logic<4>
      %concat = simulation.logic.concat %one, %input : (!simulation.logic<8>, !simulation.logic<8>) -> !simulation.logic<16>
      %known_field = simulation.logic.extract %concat from 8 : !simulation.logic<16> -> !simulation.logic<8>
      %any = simulation.logic.reduction or %concat : !simulation.logic<16> -> !simulation.logic<1>
      %unknown_cond = simulation.logic.constant false, true : !simulation.logic<1>
      %same_arms = simulation.logic.mux %unknown_cond ? %one : %one : (!simulation.logic<1>, !simulation.logic<8>, !simulation.logic<8>) -> !simulation.logic<8>
      simulation.return
    }
  }
}

// CHECK-LABEL: func @rules
// CHECK: bb0.op4.result0: two-state (partial-known-bits)
// CHECK: bb0.op6.result0: two-state (partial-known-bits)
// CHECK: bb0.op7.result0: two-state (partial-known-bits)
// CHECK: bb0.op9.result0: two-state (partial-known-bits)
