// RUN: obelisk-opt %s | obelisk-opt | FileCheck %s

module {
  simulation.design @connectivity {
    simulation.scope.decl 0 hierarchy "top"
    simulation.net.decl 0 in 0 : !simulation.logic<8> design hierarchy "top.wire"
    simulation.net.decl 1 in 0 : !simulation.logic<8> design hierarchy "top.tri" {resolution_kind = 1 : i32}
    simulation.net.connect.decl 0 in 0 0[2] to 1[7] width 4 reversed = true provenance "named"
  }
  simulation.design @mixed_uwire_connectivity {
    simulation.scope.decl 0
    simulation.net.decl 0 in 0 : !simulation.logic<2> design
    simulation.net.decl 1 in 0 : !simulation.logic<2> design {resolution_kind = 2 : i32}
    simulation.net.connect.decl 0 in 0 0[0] to 1[1] width 2 reversed = true rhs_dominates = true
  }
  simulation.design @wired_connectivity {
    simulation.scope.decl 0
    simulation.net.decl 0 in 0 : !simulation.logic<1> design {resolution_kind = 3 : i32}
    simulation.net.decl 1 in 0 : !simulation.logic<1> design {resolution_kind = 4 : i32}
    simulation.net.connect.decl 0 in 0 0[0] to 1[0] width 1 reversed = false rhs_dominates = true
  }
}

// CHECK: simulation.net.decl 0 in 0 : !simulation.logic<8> design hierarchy "top.wire"
// CHECK: simulation.net.decl 1 in 0 : !simulation.logic<8> design hierarchy "top.tri" {resolution_kind = 1 : i32}
// CHECK: simulation.net.connect.decl 0 in 0 0[2] to 1[7] width 4 reversed = true provenance "named"
// CHECK: simulation.design @mixed_uwire_connectivity
// CHECK: simulation.net.connect.decl 0 in 0 0[0] to 1[1] width 2 reversed = true rhs_dominates = true
// CHECK: simulation.design @wired_connectivity
// CHECK: resolution_kind = 3 : i32
// CHECK: resolution_kind = 4 : i32
