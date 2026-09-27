// RUN: obelisk-opt %s -canonicalize | FileCheck %s

module {
  func.func @ambiguous_matching_z() -> !simulation.logic<1> {
    %x = simulation.logic.constant 0 : i1, 1 : i1 : !simulation.logic<1>
    %z = simulation.logic.constant 1 : i1, 1 : i1 : !simulation.logic<1>
    %result = simulation.logic.mux %x ? %z : %z :
        (!simulation.logic<1>, !simulation.logic<1>,
         !simulation.logic<1>) -> !simulation.logic<1>
    return %result : !simulation.logic<1>
  }
}

// CHECK-LABEL: func.func @ambiguous_matching_z
// CHECK: %[[X:.*]] = simulation.logic.constant false, true
// CHECK-NOT: simulation.logic.mux
// CHECK: return %[[X]]
