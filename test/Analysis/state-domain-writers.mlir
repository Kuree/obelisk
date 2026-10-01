// RUN: obelisk-opt %s -o /dev/null --pass-pipeline='builtin.module(test-obelisk-sim-state-domain)' 2>&1 | FileCheck %s
// RUN: obelisk-opt %s -o /dev/null --mlir-disable-threading --pass-pipeline='builtin.module(test-obelisk-sim-state-domain)' 2>&1 | FileCheck %s

module {
  simulation.design @audit_writers {
    simulation.scope.decl 0
    simulation.code_unit.decl 1 in 0 initial hierarchy "audit_writers.update"
    simulation.storage.decl 0 in 0 : !simulation.logic<8> design
    simulation.storage.decl 1 in 0 : !simulation.logic<8> design
    simulation.storage.decl 2 in 0 : !simulation.logic<8> design
    simulation.storage.decl 3 in 0 : !simulation.logic<8> design
    simulation.storage.decl 4 in 0 : !simulation.logic<8> design
    simulation.storage.decl 5 in 0 : !simulation.logic<8> design
    simulation.net.decl 0 in 0 : !simulation.logic<8> design
    simulation.driver.decl 0 in 0 drives 0 : !simulation.logic<8> design
    simulation.net.decl 1 in 0 : !simulation.logic<8> design
    simulation.driver.decl 1 in 0 drives 1 : !simulation.logic<8> design {strength0 = 0 : i32}
    simulation.net.decl 2 in 0 : !simulation.logic<8> design {resolution_kind = 9 : i32, charge_strength = 2 : i32, propagation_delays = array<i64: 0, 0, 5>}
    simulation.driver.decl 2 in 0 drives 2 : !simulation.logic<8> design
    simulation.func @update(
        %ctx: !simulation.context {simulation.capture_kind = 0 : i32},
        %copied: !simulation.ref<!simulation.logic<8>> {simulation.capture_kind = 3 : i32, simulation.descriptor_id = 0 : i64},
        %bad: !simulation.ref<!simulation.logic<8>> {simulation.capture_kind = 3 : i32, simulation.descriptor_id = 1 : i64},
        %forced: !simulation.ref<!simulation.logic<8>> {simulation.capture_kind = 3 : i32, simulation.descriptor_id = 2 : i64})
        attributes {entry_kind = 1 : i32, code_unit_id = 1 : i64} {
      %unknown = simulation.logic.constant 0 : i8, -1 : i8 : !simulation.logic<8>
      simulation.ref.store %unknown to %bad : !simulation.logic<8>, !simulation.ref<!simulation.logic<8>>
      simulation.ref.copy %bad to %copied : !simulation.ref<!simulation.logic<8>>
      simulation.override %forced = %unknown assign false : !simulation.ref<!simulation.logic<8>>, !simulation.logic<8>
      %known = simulation.context.storage %ctx[3] : !simulation.ref<!simulation.logic<8>>
      %known_copy = simulation.context.storage %ctx[4] : !simulation.ref<!simulation.logic<8>>
      %known_force = simulation.context.storage %ctx[5] : !simulation.ref<!simulation.logic<8>>
      %one = simulation.logic.constant 1 : i8, 0 : i8 : !simulation.logic<8>
      simulation.ref.store %one to %known : !simulation.logic<8>, !simulation.ref<!simulation.logic<8>>
      simulation.ref.copy %known to %known_copy : !simulation.ref<!simulation.logic<8>>
      simulation.override %known_force = %one assign false : !simulation.ref<!simulation.logic<8>>, !simulation.logic<8>
      %net = simulation.context.net %ctx[0] : !simulation.net<!simulation.logic<8>>
      simulation.net.write %net = %unknown : !simulation.net<!simulation.logic<8>>, !simulation.logic<8>
      %highz = simulation.context.driver %ctx[1] : !simulation.driver<!simulation.logic<8>>
      %charge = simulation.context.driver %ctx[2] : !simulation.driver<!simulation.logic<8>>
      %zero = simulation.logic.constant 0 : i8, 0 : i8 : !simulation.logic<8>
      simulation.driver.drive %highz = %zero : !simulation.driver<!simulation.logic<8>>, !simulation.logic<8>
      simulation.driver.drive %charge = %one : !simulation.driver<!simulation.logic<8>>, !simulation.logic<8>
      simulation.return
    }
  }
}

// CHECK-LABEL: state-domain @audit_writers
// CHECK-NEXT: root storage 3: inductive-two-state
// CHECK-NEXT: root storage 4: inductive-two-state
// CHECK-NEXT: root storage 5: inductive-two-state
// CHECK-NEXT: func @update
