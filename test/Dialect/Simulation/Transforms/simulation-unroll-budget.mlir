// RUN: obelisk-opt %s --pass-pipeline='builtin.module(simulation.design(simulation.func(obelisk-sim-unroll-bounded-loops{maximum-loop-size=6},canonicalize)))' | FileCheck %s
// RUN: obelisk-opt %s --pass-pipeline='builtin.module(simulation.design(simulation.func(obelisk-sim-unroll-bounded-loops{maximum-loop-size=5},canonicalize)))' | FileCheck %s --check-prefix=SMALL
// RUN: obelisk-opt %s --pass-pipeline='builtin.module(simulation.design(simulation.func(obelisk-sim-unroll-bounded-loops{maximum-loop-size=18446744073709551615},canonicalize)))' | FileCheck %s

// After simplification each loop retains two stores (weighted cost six),
// reusing the constants already needed by the sibling loop. Charge this
// residual cost independently for each nest, rather than its raw expansion.
// A maximum uint64 option must not overflow the profitability check.
module {
  simulation.design @budget {
    simulation.scope.decl 0
    simulation.code_unit.decl 1 in 0 function hierarchy "siblings"
    // CHECK-LABEL: simulation.func @siblings(
    // CHECK-COUNT-4: simulation.ref.store
    // CHECK-NOT: cf.
    // CHECK: simulation.return
    // SMALL-LABEL: simulation.func @siblings(
    // SMALL: cf.cond_br
    // SMALL: simulation.ref.store
    // SMALL: cf.cond_br
    // SMALL: simulation.ref.store
    // SMALL: simulation.return
    simulation.func @siblings(%ctx: !simulation.context {simulation.capture_kind = 0 : i32}, %out: !simulation.ref<i32> {simulation.capture_kind = 1 : i32}) attributes {entry_kind = 8 : i32, code_unit_id = 1 : i64} {
      %zero = arith.constant 0 : i32
      %one = arith.constant 1 : i32
      %two = arith.constant 2 : i32
      cf.br ^first(%zero : i32)
    ^first(%i: i32):
      %test = arith.cmpi ult, %i, %two : i32
      cf.cond_br %test, ^first_body, ^second(%zero : i32)
    ^first_body:
      simulation.ref.store %i to %out : i32, !simulation.ref<i32>
      %next = arith.addi %i, %one : i32
      cf.br ^first(%next : i32)
    ^second(%j: i32):
      %more = arith.cmpi ult, %j, %two : i32
      cf.cond_br %more, ^second_body, ^done
    ^second_body:
      simulation.ref.store %j to %out : i32, !simulation.ref<i32>
      %nextj = arith.addi %j, %one : i32
      cf.br ^second(%nextj : i32)
    ^done:
      simulation.return
    }
  }
}
