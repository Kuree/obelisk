// RUN: obelisk-opt %s --schedule-promote-eval-storage | FileCheck %s --implicit-check-not='simulation.ref.alloc' --implicit-check-not='^bb{{[0-9]+}}{{.*}}!simulation.ref<'

// SROA exposes 19 extracts from a joined aggregate load. Canonicalization can
// turn those extracts into 38 reference operations, exceeding the bounded
// native join specializer's per-block limit. Expose the concrete roots before
// expanding the aggregate. Each selected read still occurs after the branch
// and observes the same reaching memory version (LRM 4.6/10.4.1).
!array = !simulation.packed_array<18 : 0 x !simulation.logic<1>>
module {
  simulation.design @joined {
    simulation.scope.decl 0
    simulation.storage.decl 0 in 0 : !array design
    simulation.storage.decl 1 in 0 : !array design
    simulation.storage.decl 2 in 0 : i1 design
    simulation.code_unit.decl 1 in 0 function hierarchy "eval"
    // CHECK-LABEL: simulation.func @eval(
    // CHECK-DAG: %[[FALSE:.*]] = arith.constant false
    // CHECK-DAG: %[[TRUE:.*]] = arith.constant true
    // CHECK: %[[PUBLISHED:.*]] = simulation.context.storage %arg0[2]
    // CHECK: cf.cond_br %arg1, ^[[LEFT:bb[0-9]+]], ^[[RIGHT:bb[0-9]+]]
    // CHECK: ^[[LEFT]]:
    // CHECK-NEXT: simulation.ref.store %[[TRUE]] to %[[PUBLISHED]]
    // CHECK-NEXT: %{{.*}} = simulation.ref.load %arg2
    // CHECK: cf.br ^[[JOIN:bb[0-9]+]]
    // CHECK: ^[[RIGHT]]:
    // CHECK-NEXT: simulation.ref.store %[[FALSE]] to %[[PUBLISHED]]
    // CHECK-NEXT: %{{.*}} = simulation.ref.load %arg3
    // CHECK: cf.br ^[[JOIN]]
    // CHECK: ^[[JOIN]](
    // CHECK: simulation.return
    simulation.func @eval(%ctx: !simulation.context {simulation.capture_kind = 0 : i32}, %choose: i1 {simulation.capture_kind = 1 : i32}, %a: !simulation.ref<!array> {simulation.capture_kind = 3 : i32, simulation.descriptor_id = 0 : i64}, %b: !simulation.ref<!array> {simulation.capture_kind = 3 : i32, simulation.descriptor_id = 1 : i64}) -> (!simulation.logic<1>, !simulation.logic<1>, !simulation.logic<1>, !simulation.logic<1>, !simulation.logic<1>, !simulation.logic<1>, !simulation.logic<1>, !simulation.logic<1>, !simulation.logic<1>, !simulation.logic<1>, !simulation.logic<1>, !simulation.logic<1>, !simulation.logic<1>, !simulation.logic<1>, !simulation.logic<1>, !simulation.logic<1>, !simulation.logic<1>, !simulation.logic<1>, !simulation.logic<1>) attributes {entry_kind = 8 : i32, code_unit_id = 1 : i64, schedule.eval.borrowed_captures} {
      %initial = simulation.aggregate.default : !array
      %local = simulation.ref.alloc %initial : !array -> !simulation.ref<!array>
      %published = simulation.context.storage %ctx[2] : !simulation.ref<i1>
      cf.cond_br %choose, ^left, ^right
    ^left:
      %one = arith.constant 1 : i1
      simulation.ref.store %one to %published : i1, !simulation.ref<i1>
      cf.br ^read(%a : !simulation.ref<!array>)
    ^right:
      %zero = arith.constant 0 : i1
      simulation.ref.store %zero to %published : i1, !simulation.ref<i1>
      cf.br ^read(%b : !simulation.ref<!array>)
    ^read(%reference: !simulation.ref<!array>):
      %value = simulation.ref.load %reference : !simulation.ref<!array> -> !array
      simulation.ref.store %value to %local : !array, !simulation.ref<!array>
      %copy = simulation.ref.load %local : !simulation.ref<!array> -> !array
      %e0 = simulation.aggregate.extract %copy[0] : (!array) -> !simulation.logic<1>
      %e1 = simulation.aggregate.extract %copy[1] : (!array) -> !simulation.logic<1>
      %e2 = simulation.aggregate.extract %copy[2] : (!array) -> !simulation.logic<1>
      %e3 = simulation.aggregate.extract %copy[3] : (!array) -> !simulation.logic<1>
      %e4 = simulation.aggregate.extract %copy[4] : (!array) -> !simulation.logic<1>
      %e5 = simulation.aggregate.extract %copy[5] : (!array) -> !simulation.logic<1>
      %e6 = simulation.aggregate.extract %copy[6] : (!array) -> !simulation.logic<1>
      %e7 = simulation.aggregate.extract %copy[7] : (!array) -> !simulation.logic<1>
      %e8 = simulation.aggregate.extract %copy[8] : (!array) -> !simulation.logic<1>
      %e9 = simulation.aggregate.extract %copy[9] : (!array) -> !simulation.logic<1>
      %e10 = simulation.aggregate.extract %copy[10] : (!array) -> !simulation.logic<1>
      %e11 = simulation.aggregate.extract %copy[11] : (!array) -> !simulation.logic<1>
      %e12 = simulation.aggregate.extract %copy[12] : (!array) -> !simulation.logic<1>
      %e13 = simulation.aggregate.extract %copy[13] : (!array) -> !simulation.logic<1>
      %e14 = simulation.aggregate.extract %copy[14] : (!array) -> !simulation.logic<1>
      %e15 = simulation.aggregate.extract %copy[15] : (!array) -> !simulation.logic<1>
      %e16 = simulation.aggregate.extract %copy[16] : (!array) -> !simulation.logic<1>
      %e17 = simulation.aggregate.extract %copy[17] : (!array) -> !simulation.logic<1>
      %e18 = simulation.aggregate.extract %copy[18] : (!array) -> !simulation.logic<1>
      cf.br ^finish(%e0, %e1, %e2, %e3, %e4, %e5, %e6, %e7, %e8, %e9, %e10, %e11, %e12, %e13, %e14, %e15, %e16, %e17, %e18 : !simulation.logic<1>, !simulation.logic<1>, !simulation.logic<1>, !simulation.logic<1>, !simulation.logic<1>, !simulation.logic<1>, !simulation.logic<1>, !simulation.logic<1>, !simulation.logic<1>, !simulation.logic<1>, !simulation.logic<1>, !simulation.logic<1>, !simulation.logic<1>, !simulation.logic<1>, !simulation.logic<1>, !simulation.logic<1>, !simulation.logic<1>, !simulation.logic<1>, !simulation.logic<1>)
    ^finish(%r0: !simulation.logic<1>, %r1: !simulation.logic<1>, %r2: !simulation.logic<1>, %r3: !simulation.logic<1>, %r4: !simulation.logic<1>, %r5: !simulation.logic<1>, %r6: !simulation.logic<1>, %r7: !simulation.logic<1>, %r8: !simulation.logic<1>, %r9: !simulation.logic<1>, %r10: !simulation.logic<1>, %r11: !simulation.logic<1>, %r12: !simulation.logic<1>, %r13: !simulation.logic<1>, %r14: !simulation.logic<1>, %r15: !simulation.logic<1>, %r16: !simulation.logic<1>, %r17: !simulation.logic<1>, %r18: !simulation.logic<1>):
      simulation.return %r0, %r1, %r2, %r3, %r4, %r5, %r6, %r7, %r8, %r9, %r10, %r11, %r12, %r13, %r14, %r15, %r16, %r17, %r18 : !simulation.logic<1>, !simulation.logic<1>, !simulation.logic<1>, !simulation.logic<1>, !simulation.logic<1>, !simulation.logic<1>, !simulation.logic<1>, !simulation.logic<1>, !simulation.logic<1>, !simulation.logic<1>, !simulation.logic<1>, !simulation.logic<1>, !simulation.logic<1>, !simulation.logic<1>, !simulation.logic<1>, !simulation.logic<1>, !simulation.logic<1>, !simulation.logic<1>, !simulation.logic<1>
    }
  }
}
