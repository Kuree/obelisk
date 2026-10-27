// RUN: talos-opt %s --pass-pipeline='builtin.module(simulation.design(simulation.func(talos-sim-unroll-bounded-loops,canonicalize,cse)))' | FileCheck %s

// Candidate cleanup must use the same CFG normalization as the production
// canonicalizer. Merging these side-effecting blocks through a reference phi
// hides the concrete NBA roots from scheduling. Every enqueue retains both
// its selected root and its iteration's value (IEEE 1800-2023 4.6(b)).
module {
  simulation.design @roots {
    simulation.scope.decl 0
    simulation.storage.decl 0 in 0 : i32 design
    simulation.storage.decl 1 in 0 : i32 design
    simulation.code_unit.decl 1 in 0 function hierarchy "conditional_roots"
    // CHECK-LABEL: simulation.func @conditional_roots(
    // CHECK-DAG: %[[ZERO:.*]] = arith.constant 0 : i32
    // CHECK-DAG: %[[ONE:.*]] = arith.constant 1 : i32
    // CHECK-DAG: %[[A:.*]] = simulation.context.storage %{{.*}}[0]
    // CHECK-DAG: %[[B:.*]] = simulation.context.storage %{{.*}}[1]
    // Check the edges, not block layout as an execution order: both first
    // enqueues must reach the second choice before any second enqueue runs.
    // CHECK: cf.cond_br %arg1, ^[[FIRST_A:bb[0-9]+]], ^[[FIRST_B:bb[0-9]+]]
    // CHECK: ^[[FIRST_B]]:
    // CHECK-NEXT: simulation.nba.enqueue %[[ZERO]] to %[[B]]
    // CHECK-NEXT: cf.br ^[[SECOND:bb[0-9]+]]
    // CHECK: ^[[SECOND]]:
    // CHECK-NEXT: cf.cond_br %arg1, ^[[SECOND_A:bb[0-9]+]], ^[[SECOND_B:bb[0-9]+]]
    // CHECK: ^[[FIRST_A]]:
    // CHECK-NEXT: simulation.nba.enqueue %[[ZERO]] to %[[A]]
    // CHECK-NEXT: cf.br ^[[SECOND]]
    // CHECK: ^[[SECOND_B]]:
    // CHECK-NEXT: simulation.nba.enqueue %[[ONE]] to %[[B]]
    // CHECK-NEXT: cf.br ^[[DONE:bb[0-9]+]]
    // CHECK: ^[[DONE]]:
    // CHECK-NEXT: simulation.return
    // CHECK: ^[[SECOND_A]]:
    // CHECK-NEXT: simulation.nba.enqueue %[[ONE]] to %[[A]]
    // CHECK-NEXT: cf.br ^[[DONE]]
    // CHECK: }
    simulation.func @conditional_roots(%ctx: !simulation.context {simulation.capture_kind = 0 : i32}, %choose: i1 {simulation.capture_kind = 1 : i32}) attributes {entry_kind = 8 : i32, code_unit_id = 1 : i64} {
      %a = simulation.context.storage %ctx[0] : !simulation.ref<i32>
      %b = simulation.context.storage %ctx[1] : !simulation.ref<i32>
      %zero = arith.constant 0 : i32
      %one = arith.constant 1 : i32
      %two = arith.constant 2 : i32
      cf.br ^loop(%zero : i32)
    ^loop(%i: i32):
      %more = arith.cmpi ult, %i, %two : i32
      cf.cond_br %more, ^body, ^done
    ^body:
      cf.cond_br %choose, ^left, ^right
    ^left:
      simulation.nba.enqueue %i to %a : (i32, !simulation.ref<i32>) -> ()
      cf.br ^latch
    ^right:
      simulation.nba.enqueue %i to %b : (i32, !simulation.ref<i32>) -> ()
      cf.br ^latch
    ^latch:
      %next = arith.addi %i, %one : i32
      cf.br ^loop(%next : i32)
    ^done:
      simulation.return
    }
  }
}
