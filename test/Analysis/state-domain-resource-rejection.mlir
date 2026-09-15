// RUN: obelisk-opt %s -o /dev/null --pass-pipeline='builtin.module(test-obelisk-sim-state-domain)' 2>&1 | FileCheck %s
// RUN: obelisk-opt %s -o /dev/null --mlir-disable-threading --pass-pipeline='builtin.module(test-obelisk-sim-state-domain)' 2>&1 | FileCheck %s

// Repeated writes through a formal storage reference reject storage roots as
// a class. Independent net roots retain their proof. A raw user-net driver
// has unknown provenance and must instead reject every candidate resource.
module {
  obelisk_sim.design @storage_only {
    obelisk_sim.scope.decl 0
    obelisk_sim.code_unit.decl 1 in 0 function hierarchy "storage_only.write"
    obelisk_sim.storage.decl 0 in 0 : !obelisk_sim.logic<8> design
    obelisk_sim.storage.decl 1 in 0 : !obelisk_sim.logic<8> design
    obelisk_sim.net.decl 0 in 0 : !obelisk_sim.logic<8> design
    obelisk_sim.driver.decl 0 in 0 drives 0 : !obelisk_sim.logic<8> design
    obelisk_sim.func @write(
        %ctx: !obelisk_sim.context {obelisk_sim.capture_kind = 0 : i32},
        %dst: !obelisk_sim.ref<!obelisk_sim.logic<8>> {obelisk_sim.capture_kind = 1 : i32})
        attributes {entry_kind = 8 : i32, code_unit_id = 1 : i64} {
      %x = obelisk_sim.logic.constant 0 : i8, -1 : i8 : !obelisk_sim.logic<8>
      obelisk_sim.ref.store %x to %dst : !obelisk_sim.logic<8>, !obelisk_sim.ref<!obelisk_sim.logic<8>>
      obelisk_sim.ref.store %x to %dst : !obelisk_sim.logic<8>, !obelisk_sim.ref<!obelisk_sim.logic<8>>
      obelisk_sim.return
    }
  }

  obelisk_sim.design @unknown_resource {
    obelisk_sim.scope.decl 0
    obelisk_sim.code_unit.decl 1 in 0 function hierarchy "unknown_resource.write"
    obelisk_sim.storage.decl 0 in 0 : !obelisk_sim.logic<8> design
    obelisk_sim.net.decl 0 in 0 : !obelisk_sim.logic<8> design
    obelisk_sim.driver.decl 0 in 0 drives 0 : !obelisk_sim.logic<8> design
    obelisk_sim.func @write(
        %ctx: !obelisk_sim.context {obelisk_sim.capture_kind = 0 : i32},
        %dst: !obelisk_sim.driver<!obelisk_sim.logic<8>> {obelisk_sim.capture_kind = 1 : i32, obelisk_sim.user_net_driver})
        attributes {entry_kind = 8 : i32, code_unit_id = 1 : i64} {
      %x = obelisk_sim.logic.constant 0 : i8, -1 : i8 : !obelisk_sim.logic<8>
      obelisk_sim.driver.drive %dst = %x : !obelisk_sim.driver<!obelisk_sim.logic<8>>, !obelisk_sim.logic<8>
      obelisk_sim.driver.drive %dst = %x : !obelisk_sim.driver<!obelisk_sim.logic<8>>, !obelisk_sim.logic<8>
      obelisk_sim.return
    }
  }
}

// CHECK-LABEL: state-domain @storage_only
// CHECK-NEXT: root net 0: inductive-two-state
// CHECK-NEXT: func @write
// CHECK-LABEL: state-domain @unknown_resource
// CHECK-NEXT: func @write
