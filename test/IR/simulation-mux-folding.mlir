// RUN: obelisk-opt %s -canonicalize | FileCheck %s

module {
  func.func @ambiguous_matching_z() -> !obelisk_sim.logic<1> {
    %x = obelisk_sim.logic.constant 0 : i1, 1 : i1 : !obelisk_sim.logic<1>
    %z = obelisk_sim.logic.constant 1 : i1, 1 : i1 : !obelisk_sim.logic<1>
    %result = obelisk_sim.logic.mux %x ? %z : %z :
        (!obelisk_sim.logic<1>, !obelisk_sim.logic<1>,
         !obelisk_sim.logic<1>) -> !obelisk_sim.logic<1>
    return %result : !obelisk_sim.logic<1>
  }
}

// CHECK-LABEL: func.func @ambiguous_matching_z
// CHECK: %[[X:.*]] = obelisk_sim.logic.constant false, true
// CHECK-NOT: obelisk_sim.logic.mux
// CHECK: return %[[X]]
