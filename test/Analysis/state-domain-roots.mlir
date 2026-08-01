// RUN: obelisk-opt %s -o /dev/null --pass-pipeline='builtin.module(test-obelisk-sim-state-domain)' 2>&1 | FileCheck %s

module {
  obelisk_sim.design @inductive_roots {
    obelisk_sim.scope.decl 0
    obelisk_sim.code_unit.decl 1 in 0 initial hierarchy "inductive_roots.update"
    obelisk_sim.storage.decl 0 in 0 : !obelisk_sim.logic<8> design
    obelisk_sim.storage.decl 1 in 0 : !obelisk_sim.logic<8> design
    obelisk_sim.storage.decl 2 in 0 : !obelisk_sim.logic<8> design
    obelisk_sim.net.decl 0 in 0 : !obelisk_sim.logic<8> design
    obelisk_sim.net.decl 1 in 0 : !obelisk_sim.logic<8> design
    obelisk_sim.driver.decl 0 in 0 drives 0 : !obelisk_sim.logic<8> design
    obelisk_sim.driver.decl 1 in 0 drives 1 : !obelisk_sim.logic<8> design
    obelisk_sim.driver.decl 2 in 0 drives 1 : !obelisk_sim.logic<8> design

    obelisk_sim.func @update(
        %ctx: !obelisk_sim.context {obelisk_sim.capture_kind = 0 : i32},
        %self: !obelisk_sim.ref<!obelisk_sim.logic<8>> {obelisk_sim.capture_kind = 3 : i32, obelisk_sim.descriptor_id = 0 : i64},
        %bad: !obelisk_sim.ref<!obelisk_sim.logic<8>> {obelisk_sim.capture_kind = 3 : i32, obelisk_sim.descriptor_id = 1 : i64},
        %dependent: !obelisk_sim.ref<!obelisk_sim.logic<8>> {obelisk_sim.capture_kind = 3 : i32, obelisk_sim.descriptor_id = 2 : i64},
        %single: !obelisk_sim.driver<!obelisk_sim.logic<8>> {obelisk_sim.capture_kind = 5 : i32, obelisk_sim.descriptor_id = 0 : i64},
        %multiple: !obelisk_sim.driver<!obelisk_sim.logic<8>> {obelisk_sim.capture_kind = 5 : i32, obelisk_sim.descriptor_id = 1 : i64})
        attributes {entry_kind = 1 : i32, code_unit_id = 1 : i64} {
      %one = obelisk_sim.logic.constant 1 : i8, 0 : i8 : !obelisk_sim.logic<8>
      %unknown = obelisk_sim.logic.constant 0 : i8, -1 : i8 : !obelisk_sim.logic<8>
      %old = obelisk_sim.ref.load %self : !obelisk_sim.ref<!obelisk_sim.logic<8>> -> !obelisk_sim.logic<8>
      %next = obelisk_sim.logic.binary add %old, %one : !obelisk_sim.logic<8>
      obelisk_sim.ref.store %next to %self : !obelisk_sim.logic<8>, !obelisk_sim.ref<!obelisk_sim.logic<8>>
      obelisk_sim.ref.store %unknown to %bad : !obelisk_sim.logic<8>, !obelisk_sim.ref<!obelisk_sim.logic<8>>
      %bad_value = obelisk_sim.ref.load %bad : !obelisk_sim.ref<!obelisk_sim.logic<8>> -> !obelisk_sim.logic<8>
      obelisk_sim.ref.store %bad_value to %dependent : !obelisk_sim.logic<8>, !obelisk_sim.ref<!obelisk_sim.logic<8>>
      obelisk_sim.driver.drive %single = %one : !obelisk_sim.driver<!obelisk_sim.logic<8>>, !obelisk_sim.logic<8>
      obelisk_sim.driver.drive %multiple = %one : !obelisk_sim.driver<!obelisk_sim.logic<8>>, !obelisk_sim.logic<8>
      obelisk_sim.return
    }
  }
}

// CHECK-LABEL: state-domain @inductive_roots
// CHECK-NEXT: root storage 0: inductive-two-state
// CHECK-NEXT: root net 0: inductive-two-state
// CHECK-NOT: root storage 1
// CHECK-NOT: root storage 2
// CHECK-NOT: root net 1
// CHECK: func @update
