// RUN: obelisk-opt %s '--pass-pipeline=builtin.module(simulation.design(simulation.func(schedule-specialize-reference-joins)))' -o %t
// RUN: FileCheck %s < %t
// RUN: obelisk-opt %t '--pass-pipeline=builtin.module(simulation.design(simulation.func(schedule-specialize-reference-joins)))' -o %t.again
// RUN: diff %t %t.again

// Two edges from the same predecessor remain distinct. Each selected block
// executes the original read/store order using that edge's incoming values.
module {
  simulation.design @joins {
    simulation.scope.decl 0
    simulation.code_unit.decl 1 in 0 function hierarchy "edges"
    // CHECK-LABEL: simulation.func @edges(
    // CHECK-SAME: %[[LEFT:arg1]]:
    // CHECK-SAME: %[[RIGHT:arg2]]:
    // CHECK: cf.cond_br
    // CHECK-DAG: %[[AV:.*]] = simulation.ref.load %[[LEFT]]
    // CHECK-DAG: simulation.ref.store %[[AV]] to %[[LEFT]] {test.keep}
    // CHECK-DAG: %[[BV:.*]] = simulation.ref.load %[[RIGHT]]
    // CHECK-DAG: simulation.ref.store %[[BV]] to %[[RIGHT]] {test.keep}
    // CHECK: simulation.return
    simulation.func @edges(%ctx: !simulation.context {simulation.capture_kind = 0 : i32}, %a: !simulation.ref<i8> {simulation.capture_kind = 2 : i32}, %b: !simulation.ref<i8> {simulation.capture_kind = 2 : i32}, %choose: i1 {simulation.capture_kind = 2 : i32}) attributes {entry_kind = 8 : i32, code_unit_id = 1 : i64} {
      cf.cond_br %choose, ^join(%a : !simulation.ref<i8>), ^join(%b : !simulation.ref<i8>)
    ^join(%selected: !simulation.ref<i8>):
      %value = simulation.ref.load %selected : !simulation.ref<i8> -> i8
      simulation.ref.store %value to %selected {test.keep} : i8, !simulation.ref<i8>
      cf.br ^done
    ^done:
      simulation.return
    }

    simulation.code_unit.decl 4 in 0 function hierarchy "conditional"
    // The selected load may itself control a branch. Specialization preserves
    // both outgoing edges and leaves the shared effectful continuation alone.
    // CHECK-LABEL: simulation.func @conditional(
    // CHECK-SAME: %[[A:arg1]]:
    // CHECK-SAME: %[[B:arg2]]:
    // CHECK: cf.cond_br
    // CHECK: %[[BV:.*]] = simulation.ref.load %[[B]]
    // CHECK-NEXT: cf.cond_br %[[BV]], ^[[YES:bb[0-9]+]], ^[[DONE:bb[0-9]+]]
    // CHECK: %[[AV:.*]] = simulation.ref.load %[[A]]
    // CHECK-NEXT: cf.cond_br %[[AV]], ^[[YES]], ^[[DONE]]
    // CHECK: ^[[YES]]:
    // CHECK-NEXT: simulation.ref.store
    // CHECK-NEXT: cf.br ^[[DONE]]
    // CHECK: ^[[DONE]]:
    // CHECK-NEXT: simulation.return
    simulation.func @conditional(%ctx: !simulation.context {simulation.capture_kind = 0 : i32}, %a: !simulation.ref<i1> {simulation.capture_kind = 2 : i32}, %b: !simulation.ref<i1> {simulation.capture_kind = 2 : i32}, %choose: i1 {simulation.capture_kind = 2 : i32}, %out: !simulation.ref<i1> {simulation.capture_kind = 2 : i32}) attributes {entry_kind = 8 : i32, code_unit_id = 4 : i64} {
      cf.cond_br %choose, ^probe(%a : !simulation.ref<i1>), ^probe(%b : !simulation.ref<i1>)
    ^probe(%selected: !simulation.ref<i1>):
      %value = simulation.ref.load %selected : !simulation.ref<i1> -> i1
      cf.cond_br %value, ^yes, ^done
    ^yes:
      simulation.ref.store %choose to %out : i1, !simulation.ref<i1>
      cf.br ^done
    ^done:
      simulation.return
    }

    simulation.code_unit.decl 2 in 0 function hierarchy "common"
    // A scalar phi alone does not justify duplication.
    // CHECK-LABEL: simulation.func @common(
    // CHECK: cf.cond_br %{{.*}}, ^[[JOIN:bb[0-9]+]](%{{.*}}, %{{.*}} : !simulation.ref<i8>, i8), ^[[JOIN]](
    // CHECK: ^[[JOIN]](%{{.*}}: !simulation.ref<i8>, %{{.*}}: i8):
    simulation.func @common(%ctx: !simulation.context {simulation.capture_kind = 0 : i32}, %a: !simulation.ref<i8> {simulation.capture_kind = 2 : i32}, %choose: i1 {simulation.capture_kind = 2 : i32}) attributes {entry_kind = 8 : i32, code_unit_id = 2 : i64} {
      %zero = arith.constant 0 : i8
      %one = arith.constant 1 : i8
      cf.cond_br %choose, ^join(%a, %zero : !simulation.ref<i8>, i8), ^join(%a, %one : !simulation.ref<i8>, i8)
    ^join(%selected: !simulation.ref<i8>, %value: i8):
      simulation.ref.store %value to %selected : i8, !simulation.ref<i8>
      cf.br ^done
    ^done:
      simulation.return
    }

    simulation.code_unit.decl 3 in 0 function hierarchy "escaping"
    // A loaded scalar used outside the selected block is forwarded through a
    // scalar phi. The load still executes exactly once on the selected edge.
    // CHECK-LABEL: simulation.func @escaping(
    // CHECK: cf.cond_br
    // CHECK: %[[V1:.*]] = simulation.ref.load %arg2
    // CHECK-NEXT: cf.br ^[[CONT:bb[0-9]+]](%[[V1]] : i8)
    // CHECK: %[[V2:.*]] = simulation.ref.load %arg1
    // CHECK-NEXT: cf.br ^[[CONT]](%[[V2]] : i8)
    // CHECK: ^[[CONT]](%[[MERGED:.*]]: i8):
    // CHECK-NEXT: cf.br
    // CHECK: simulation.ref.store %[[MERGED]] to %arg1
    simulation.func @escaping(%ctx: !simulation.context {simulation.capture_kind = 0 : i32}, %a: !simulation.ref<i8> {simulation.capture_kind = 2 : i32}, %b: !simulation.ref<i8> {simulation.capture_kind = 2 : i32}, %choose: i1 {simulation.capture_kind = 2 : i32}) attributes {entry_kind = 8 : i32, code_unit_id = 3 : i64} {
      cf.cond_br %choose, ^join(%a : !simulation.ref<i8>), ^join(%b : !simulation.ref<i8>)
    ^join(%selected: !simulation.ref<i8>):
      %value = simulation.ref.load %selected : !simulation.ref<i8> -> i8
      cf.br ^done
    ^done:
      simulation.ref.store %value to %a : i8, !simulation.ref<i8>
      simulation.return
    }

    simulation.code_unit.decl 5 in 0 function hierarchy "escaping_address"
    // Forwarding the escaping address would only recreate the reference phi.
    // CHECK-LABEL: simulation.func @escaping_address(
    // CHECK: cf.cond_br %{{.*}}, ^[[ADDR:bb[0-9]+]](%{{.*}} : !simulation.ref<i8>), ^[[ADDR]](
    // CHECK: ^[[ADDR]](%[[REF:.*]]: !simulation.ref<i8>):
    // CHECK-NEXT: cf.br
    // CHECK: simulation.ref.load %[[REF]]
    simulation.func @escaping_address(%ctx: !simulation.context {simulation.capture_kind = 0 : i32}, %a: !simulation.ref<i8> {simulation.capture_kind = 2 : i32}, %b: !simulation.ref<i8> {simulation.capture_kind = 2 : i32}, %choose: i1 {simulation.capture_kind = 2 : i32}) attributes {entry_kind = 8 : i32, code_unit_id = 5 : i64} {
      cf.cond_br %choose, ^join(%a : !simulation.ref<i8>), ^join(%b : !simulation.ref<i8>)
    ^join(%selected: !simulation.ref<i8>):
      cf.br ^done
    ^done:
      %value = simulation.ref.load %selected : !simulation.ref<i8> -> i8
      simulation.ref.store %value to %a : i8, !simulation.ref<i8>
      simulation.return
    }
  }
}
