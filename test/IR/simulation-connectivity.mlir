// RUN: obelisk-opt %s | obelisk-opt | FileCheck %s

module {
  obelisk_sim.design @connectivity {
    obelisk_sim.scope.decl 0 hierarchy "top"
    obelisk_sim.net.decl 0 in 0 : !obelisk_sim.logic<8> design hierarchy "top.wire"
    obelisk_sim.net.decl 1 in 0 : !obelisk_sim.logic<8> design hierarchy "top.tri" {resolution_kind = 1 : i32}
    obelisk_sim.net.connect.decl 0 in 0 0[2] to 1[7] width 4 reversed = true provenance "named"
  }
}

// CHECK: obelisk_sim.net.decl 0 in 0 : !obelisk_sim.logic<8> design hierarchy "top.wire"
// CHECK: obelisk_sim.net.decl 1 in 0 : !obelisk_sim.logic<8> design hierarchy "top.tri" {resolution_kind = 1 : i32}
// CHECK: obelisk_sim.net.connect.decl 0 in 0 0[2] to 1[7] width 4 reversed = true provenance "named"
