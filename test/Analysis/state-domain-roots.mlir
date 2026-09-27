// RUN: obelisk-opt %s -o /dev/null --pass-pipeline='builtin.module(test-obelisk-sim-state-domain)' 2>&1 | FileCheck %s
// RUN: obelisk-opt %s -o /dev/null --mlir-disable-threading --pass-pipeline='builtin.module(test-obelisk-sim-state-domain)' 2>&1 | FileCheck %s

// Rejection must propagate across successive root-proof waves and through a
// private call boundary. Shared structural summaries cannot retain the earlier
// assumption that a rejected source root is known.

module {
  simulation.design @inductive_roots {
    simulation.scope.decl 0
    simulation.code_unit.decl 1 in 0 initial hierarchy "inductive_roots.update"
    simulation.code_unit.decl 2 in 0 function hierarchy "inductive_roots.forward"
    simulation.storage.decl 0 in 0 : !simulation.logic<8> design
    simulation.storage.decl 1 in 0 : !simulation.logic<8> design
    simulation.storage.decl 2 in 0 : !simulation.logic<8> design
    simulation.storage.decl 3 in 0 : !simulation.logic<8> design
    simulation.net.decl 0 in 0 : !simulation.logic<8> design
    simulation.net.decl 1 in 0 : !simulation.logic<8> design
    simulation.driver.decl 0 in 0 drives 0 : !simulation.logic<8> design
    simulation.driver.decl 1 in 0 drives 1 : !simulation.logic<8> design
    simulation.driver.decl 2 in 0 drives 1 : !simulation.logic<8> design

    simulation.func @update(
        %ctx: !simulation.context {simulation.capture_kind = 0 : i32},
        %self: !simulation.ref<!simulation.logic<8>> {simulation.capture_kind = 3 : i32, simulation.descriptor_id = 0 : i64},
        %bad: !simulation.ref<!simulation.logic<8>> {simulation.capture_kind = 3 : i32, simulation.descriptor_id = 1 : i64},
        %dependent: !simulation.ref<!simulation.logic<8>> {simulation.capture_kind = 3 : i32, simulation.descriptor_id = 2 : i64},
        %transitive: !simulation.ref<!simulation.logic<8>> {simulation.capture_kind = 3 : i32, simulation.descriptor_id = 3 : i64},
        %single: !simulation.driver<!simulation.logic<8>> {simulation.capture_kind = 5 : i32, simulation.descriptor_id = 0 : i64},
        %multiple: !simulation.driver<!simulation.logic<8>> {simulation.capture_kind = 5 : i32, simulation.descriptor_id = 1 : i64})
        attributes {entry_kind = 1 : i32, code_unit_id = 1 : i64} {
      %one = simulation.logic.constant 1 : i8, 0 : i8 : !simulation.logic<8>
      %unknown = simulation.logic.constant 0 : i8, -1 : i8 : !simulation.logic<8>
      %old = simulation.ref.load %self : !simulation.ref<!simulation.logic<8>> -> !simulation.logic<8>
      %next = simulation.logic.binary add %old, %one : !simulation.logic<8>
      simulation.ref.store %next to %self : !simulation.logic<8>, !simulation.ref<!simulation.logic<8>>
      simulation.ref.store %unknown to %bad : !simulation.logic<8>, !simulation.ref<!simulation.logic<8>>
      %bad_value = simulation.ref.load %bad : !simulation.ref<!simulation.logic<8>> -> !simulation.logic<8>
      simulation.ref.store %bad_value to %dependent : !simulation.logic<8>, !simulation.ref<!simulation.logic<8>>
      %dependent_value = simulation.ref.load %dependent : !simulation.ref<!simulation.logic<8>> -> !simulation.logic<8>
      %forwarded = simulation.call @forward(%ctx, %dependent_value) : (!simulation.context, !simulation.logic<8>) -> !simulation.logic<8>
      simulation.ref.store %forwarded to %transitive : !simulation.logic<8>, !simulation.ref<!simulation.logic<8>>
      simulation.driver.drive %single = %one : !simulation.driver<!simulation.logic<8>>, !simulation.logic<8>
      simulation.driver.drive %multiple = %one : !simulation.driver<!simulation.logic<8>>, !simulation.logic<8>
      simulation.return
    }

    simulation.func private @forward(
        %ctx: !simulation.context {simulation.capture_kind = 0 : i32},
        %value: !simulation.logic<8> {simulation.capture_kind = 1 : i32})
        -> !simulation.logic<8> attributes {entry_kind = 8 : i32, code_unit_id = 2 : i64} {
      simulation.return %value : !simulation.logic<8>
    }
  }
}

// CHECK-LABEL: state-domain @inductive_roots
// CHECK-NEXT: root storage 0: inductive-two-state
// CHECK-NEXT: root net 0: inductive-two-state
// CHECK-NOT: root storage 1
// CHECK-NOT: root storage 2
// CHECK-NOT: root storage 3
// CHECK-NOT: root net 1
// CHECK: func @update
