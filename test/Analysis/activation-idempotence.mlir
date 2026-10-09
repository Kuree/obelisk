// RUN: obelisk-opt %s '--test-obelisk-activation-regions=idempotence=true' -o %t.analyzed 2>&1 | FileCheck %s
// RUN: obelisk-opt %s '--test-obelisk-activation-regions=idempotence=true reaching-values=true' -o /dev/null 2>&1 | FileCheck %s
// RUN: obelisk-opt %s '--test-obelisk-activation-regions=idempotence=true idempotence-maximum-operations=1' -o /dev/null 2>&1 | FileCheck %s --check-prefix=BUDGET
// RUN: obelisk-opt %s -o %t.original
// RUN: diff %t.original %t.analyzed
// A conditional hold may use incoming state only on paths that preserve that
// state. Updates, branch decisions, and every final output participate in the
// proof; exclusive ownership and observer safety are separate obligations.
// Exhausting the compile-time proof budget must reject, never assume success.
// BUDGET: regions @conditional_hold valid=1
// BUDGET-NEXT: idempotent=0
// BUDGET-NOT: idempotent=1
module {
  simulation.design @idempotence {
    simulation.scope.decl 0
    simulation.storage.decl 0 in 0 : i1 design
    simulation.storage.decl 1 in 0 : i8 design
    simulation.storage.decl 2 in 0 : i8 design
    simulation.storage.decl 3 in 0 : i8 design
    // CHECK-LABEL: regions @conditional_hold valid=1
    // CHECK-NEXT:   idempotent=1
    simulation.code_unit.decl 1 in 0 always_comb hierarchy "conditional_hold"
    simulation.func @conditional_hold(%ctx: !simulation.context {simulation.capture_kind = 0 : i32}) attributes {entry_kind = 4 : i32, code_unit_id = 1 : i64} {
      %enable = simulation.context.storage %ctx[0] : !simulation.ref<i1>
      %input = simulation.context.storage %ctx[1] : !simulation.ref<i8>
      %state = simulation.context.storage %ctx[2] : !simulation.ref<i8>
      %output = simulation.context.storage %ctx[3] : !simulation.ref<i8>
      %zero = arith.constant 0 : i8
      %one = arith.constant 1 : i8
      cf.br ^body
    ^body:
      %old = simulation.ref.load %state : !simulation.ref<i8> -> i8
      %flag = simulation.ref.load %enable : !simulation.ref<i1> -> i1
      cf.cond_br %flag, ^update, ^hold
    ^update:
      %incoming = simulation.ref.load %input : !simulation.ref<i8> -> i8
      cf.br ^join(%incoming : i8)
    ^hold:
      cf.br ^join(%old : i8)
    ^join(%value: i8):
      simulation.ref.store %value to %state : i8, !simulation.ref<i8>
      %derived = arith.xori %value, %one : i8
      simulation.ref.store %derived to %output : i8, !simulation.ref<i8>
      simulation.suspend.any %enable, %input edges [0, 0] to ^body : !simulation.ref<i1>, !simulation.ref<i8>
    }
    // CHECK-LABEL: regions @old_snapshot valid=1
    // CHECK-NEXT:   idempotent=0
    simulation.code_unit.decl 2 in 0 always_comb hierarchy "old_snapshot"
    simulation.func @old_snapshot(%ctx: !simulation.context {simulation.capture_kind = 0 : i32}) attributes {entry_kind = 4 : i32, code_unit_id = 2 : i64} {
      %enable = simulation.context.storage %ctx[0] : !simulation.ref<i1>
      %input = simulation.context.storage %ctx[1] : !simulation.ref<i8>
      %state = simulation.context.storage %ctx[2] : !simulation.ref<i8>
      %output = simulation.context.storage %ctx[3] : !simulation.ref<i8>
      %zero = arith.constant 0 : i8
      %one = arith.constant 1 : i8
      cf.br ^body
    ^body:
      %old = simulation.ref.load %state : !simulation.ref<i8> -> i8
      %flag = simulation.ref.load %enable : !simulation.ref<i1> -> i1
      cf.cond_br %flag, ^update, ^hold
    ^update:
      %incoming = simulation.ref.load %input : !simulation.ref<i8> -> i8
      cf.br ^join(%incoming : i8)
    ^hold:
      cf.br ^join(%old : i8)
    ^join(%value: i8):
      simulation.ref.store %value to %state : i8, !simulation.ref<i8>
      %derived = arith.xori %old, %one : i8
      simulation.ref.store %derived to %output : i8, !simulation.ref<i8>
      simulation.suspend.any %enable, %input edges [0, 0] to ^body : !simulation.ref<i1>, !simulation.ref<i8>
    }
    // CHECK-LABEL: regions @counter valid=1
    // CHECK-NEXT:   idempotent=0
    simulation.code_unit.decl 3 in 0 always_comb hierarchy "counter"
    simulation.func @counter(%ctx: !simulation.context {simulation.capture_kind = 0 : i32}) attributes {entry_kind = 4 : i32, code_unit_id = 3 : i64} {
      %enable = simulation.context.storage %ctx[0] : !simulation.ref<i1>
      %input = simulation.context.storage %ctx[1] : !simulation.ref<i8>
      %state = simulation.context.storage %ctx[2] : !simulation.ref<i8>
      %output = simulation.context.storage %ctx[3] : !simulation.ref<i8>
      %zero = arith.constant 0 : i8
      %one = arith.constant 1 : i8
      cf.br ^body
    ^body:
      %old = simulation.ref.load %state : !simulation.ref<i8> -> i8
      %next = arith.addi %old, %one : i8
      simulation.ref.store %next to %state : i8, !simulation.ref<i8>
      simulation.suspend.any %enable, %input edges [0, 0] to ^body : !simulation.ref<i1>, !simulation.ref<i8>
    }
    // CHECK-LABEL: regions @state_controls_update valid=1
    // CHECK-NEXT:   idempotent=0
    simulation.code_unit.decl 4 in 0 always_comb hierarchy "state_controls_update"
    simulation.func @state_controls_update(%ctx: !simulation.context {simulation.capture_kind = 0 : i32}) attributes {entry_kind = 4 : i32, code_unit_id = 4 : i64} {
      %enable = simulation.context.storage %ctx[0] : !simulation.ref<i1>
      %input = simulation.context.storage %ctx[1] : !simulation.ref<i8>
      %state = simulation.context.storage %ctx[2] : !simulation.ref<i8>
      %output = simulation.context.storage %ctx[3] : !simulation.ref<i8>
      %zero = arith.constant 0 : i8
      %one = arith.constant 1 : i8
      cf.br ^body
    ^body:
      %old = simulation.ref.load %state : !simulation.ref<i8> -> i8
      %flag = arith.cmpi eq, %old, %zero : i8
      cf.cond_br %flag, ^set, ^clear
    ^set:
      cf.br ^join(%one : i8)
    ^clear:
      cf.br ^join(%zero : i8)
    ^join(%value: i8):
      simulation.ref.store %value to %state : i8, !simulation.ref<i8>
      simulation.suspend.any %enable, %input edges [0, 0] to ^body : !simulation.ref<i1>, !simulation.ref<i8>
    }
    // CHECK-LABEL: regions @unchanged_state valid=1
    // CHECK-NEXT:   idempotent=1
    simulation.code_unit.decl 5 in 0 always_comb hierarchy "unchanged_state"
    simulation.func @unchanged_state(%ctx: !simulation.context {simulation.capture_kind = 0 : i32}) attributes {entry_kind = 4 : i32, code_unit_id = 5 : i64} {
      %enable = simulation.context.storage %ctx[0] : !simulation.ref<i1>
      %input = simulation.context.storage %ctx[1] : !simulation.ref<i8>
      %state = simulation.context.storage %ctx[2] : !simulation.ref<i8>
      %output = simulation.context.storage %ctx[3] : !simulation.ref<i8>
      %zero = arith.constant 0 : i8
      %one = arith.constant 1 : i8
      cf.br ^body
    ^body:
      %old = simulation.ref.load %state : !simulation.ref<i8> -> i8
      simulation.ref.store %old to %state : i8, !simulation.ref<i8>
      %derived = arith.xori %old, %one : i8
      simulation.ref.store %derived to %output : i8, !simulation.ref<i8>
      simulation.suspend.any %enable, %input edges [0, 0] to ^body : !simulation.ref<i1>, !simulation.ref<i8>
    }
    // CHECK-LABEL: regions @exchanged_state valid=1
    // CHECK-NEXT:   idempotent=0
    simulation.code_unit.decl 6 in 0 always_comb hierarchy "exchanged_state"
    simulation.func @exchanged_state(%ctx: !simulation.context {simulation.capture_kind = 0 : i32}) attributes {entry_kind = 4 : i32, code_unit_id = 6 : i64} {
      %enable = simulation.context.storage %ctx[0] : !simulation.ref<i1>
      %input = simulation.context.storage %ctx[1] : !simulation.ref<i8>
      %state = simulation.context.storage %ctx[2] : !simulation.ref<i8>
      %output = simulation.context.storage %ctx[3] : !simulation.ref<i8>
      %zero = arith.constant 0 : i8
      %one = arith.constant 1 : i8
      cf.br ^body
    ^body:
      %old = simulation.ref.load %state : !simulation.ref<i8> -> i8
      %other = simulation.ref.load %output : !simulation.ref<i8> -> i8
      simulation.ref.store %other to %state : i8, !simulation.ref<i8>
      simulation.ref.store %old to %output : i8, !simulation.ref<i8>
      simulation.suspend.any %enable, %input edges [0, 0] to ^body : !simulation.ref<i1>, !simulation.ref<i8>
    }
  }
}
