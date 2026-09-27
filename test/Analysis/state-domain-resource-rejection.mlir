// RUN: obelisk-opt %s -o /dev/null --pass-pipeline='builtin.module(test-obelisk-sim-state-domain)' 2>&1 | FileCheck %s
// RUN: obelisk-opt %s -o /dev/null --mlir-disable-threading --pass-pipeline='builtin.module(test-obelisk-sim-state-domain)' 2>&1 | FileCheck %s

// Repeated writes through a formal storage reference reject storage roots as
// a class. Independent net roots retain their proof. A raw user-net driver
// has unknown provenance and must instead reject every candidate resource.
module {
  simulation.design @storage_only {
    simulation.scope.decl 0
    simulation.code_unit.decl 1 in 0 function hierarchy "storage_only.write"
    simulation.storage.decl 0 in 0 : !simulation.logic<8> design
    simulation.storage.decl 1 in 0 : !simulation.logic<8> design
    simulation.net.decl 0 in 0 : !simulation.logic<8> design
    simulation.driver.decl 0 in 0 drives 0 : !simulation.logic<8> design
    simulation.func @write(
        %ctx: !simulation.context {simulation.capture_kind = 0 : i32},
        %dst: !simulation.ref<!simulation.logic<8>> {simulation.capture_kind = 1 : i32})
        attributes {entry_kind = 8 : i32, code_unit_id = 1 : i64} {
      %x = simulation.logic.constant 0 : i8, -1 : i8 : !simulation.logic<8>
      simulation.ref.store %x to %dst : !simulation.logic<8>, !simulation.ref<!simulation.logic<8>>
      simulation.ref.store %x to %dst : !simulation.logic<8>, !simulation.ref<!simulation.logic<8>>
      simulation.return
    }
  }

  simulation.design @unknown_resource {
    simulation.scope.decl 0
    simulation.code_unit.decl 1 in 0 function hierarchy "unknown_resource.write"
    simulation.storage.decl 0 in 0 : !simulation.logic<8> design
    simulation.net.decl 0 in 0 : !simulation.logic<8> design
    simulation.driver.decl 0 in 0 drives 0 : !simulation.logic<8> design
    simulation.func @write(
        %ctx: !simulation.context {simulation.capture_kind = 0 : i32},
        %dst: !simulation.driver<!simulation.logic<8>> {simulation.capture_kind = 1 : i32, simulation.user_net_driver})
        attributes {entry_kind = 8 : i32, code_unit_id = 1 : i64} {
      %x = simulation.logic.constant 0 : i8, -1 : i8 : !simulation.logic<8>
      simulation.driver.drive %dst = %x : !simulation.driver<!simulation.logic<8>>, !simulation.logic<8>
      simulation.driver.drive %dst = %x : !simulation.driver<!simulation.logic<8>>, !simulation.logic<8>
      simulation.return
    }
  }
}

// CHECK-LABEL: state-domain @storage_only
// CHECK-NEXT: root net 0: inductive-two-state
// CHECK-NEXT: func @write
// CHECK-LABEL: state-domain @unknown_resource
// CHECK-NEXT: func @write
