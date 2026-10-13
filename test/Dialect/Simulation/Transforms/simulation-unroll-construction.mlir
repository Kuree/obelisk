// RUN: obelisk-opt %s --split-input-file --pass-pipeline='builtin.module(simulation.design(simulation.func(obelisk-sim-unroll-bounded-loops{maximum-loop-size=1000})))' | FileCheck %s

// The final expansion of this nest contains no stores because the inner loop
// never runs, but outer-first replication would temporarily copy the 32
// unreachable stores 64 * 16 times, past the 30000-operation construction
// ceiling. Bound intermediate construction before making the trial copy, not
// just the final expanded nest. Refusing the trial keeps the original CFG and
// source-order effects intact (IEEE 1800-2023 4.6 and 12.7.1).
module {
  simulation.design @construction {
    simulation.scope.decl 0
    simulation.code_unit.decl 1 in 0 function hierarchy "zero_trip_child"
    // CHECK-LABEL: simulation.func @zero_trip_child(
    // CHECK: cf.cond_br
    // CHECK: cf.cond_br
    // CHECK: cf.cond_br
    // CHECK-COUNT-32: simulation.ref.store
    // CHECK-NOT: simulation.ref.store
    // CHECK: simulation.return
    simulation.func @zero_trip_child(%ctx: !simulation.context {simulation.capture_kind = 0 : i32}, %out: !simulation.ref<i32> {simulation.capture_kind = 1 : i32}) attributes {entry_kind = 8 : i32, code_unit_id = 1 : i64} {
      %zero = arith.constant 0 : i32
      %one = arith.constant 1 : i32
      %outer_limit = arith.constant 64 : i32
      %middle_limit = arith.constant 16 : i32
      cf.br ^outer(%zero : i32)
    ^outer(%i: i32):
      %more = arith.cmpi ult, %i, %outer_limit : i32
      cf.cond_br %more, ^middle(%zero : i32), ^done
    ^middle(%k: i32):
      %again = arith.cmpi ult, %k, %middle_limit : i32
      cf.cond_br %again, ^inner(%zero : i32), ^latch
    ^inner(%j: i32):
      %inside = arith.cmpi ult, %j, %zero : i32
      cf.cond_br %inside, ^body, ^next
    ^body:
      simulation.ref.store %j to %out : i32, !simulation.ref<i32>
      simulation.ref.store %j to %out : i32, !simulation.ref<i32>
      simulation.ref.store %j to %out : i32, !simulation.ref<i32>
      simulation.ref.store %j to %out : i32, !simulation.ref<i32>
      simulation.ref.store %j to %out : i32, !simulation.ref<i32>
      simulation.ref.store %j to %out : i32, !simulation.ref<i32>
      simulation.ref.store %j to %out : i32, !simulation.ref<i32>
      simulation.ref.store %j to %out : i32, !simulation.ref<i32>
      simulation.ref.store %j to %out : i32, !simulation.ref<i32>
      simulation.ref.store %j to %out : i32, !simulation.ref<i32>
      simulation.ref.store %j to %out : i32, !simulation.ref<i32>
      simulation.ref.store %j to %out : i32, !simulation.ref<i32>
      simulation.ref.store %j to %out : i32, !simulation.ref<i32>
      simulation.ref.store %j to %out : i32, !simulation.ref<i32>
      simulation.ref.store %j to %out : i32, !simulation.ref<i32>
      simulation.ref.store %j to %out : i32, !simulation.ref<i32>
      simulation.ref.store %j to %out : i32, !simulation.ref<i32>
      simulation.ref.store %j to %out : i32, !simulation.ref<i32>
      simulation.ref.store %j to %out : i32, !simulation.ref<i32>
      simulation.ref.store %j to %out : i32, !simulation.ref<i32>
      simulation.ref.store %j to %out : i32, !simulation.ref<i32>
      simulation.ref.store %j to %out : i32, !simulation.ref<i32>
      simulation.ref.store %j to %out : i32, !simulation.ref<i32>
      simulation.ref.store %j to %out : i32, !simulation.ref<i32>
      simulation.ref.store %j to %out : i32, !simulation.ref<i32>
      simulation.ref.store %j to %out : i32, !simulation.ref<i32>
      simulation.ref.store %j to %out : i32, !simulation.ref<i32>
      simulation.ref.store %j to %out : i32, !simulation.ref<i32>
      simulation.ref.store %j to %out : i32, !simulation.ref<i32>
      simulation.ref.store %j to %out : i32, !simulation.ref<i32>
      simulation.ref.store %j to %out : i32, !simulation.ref<i32>
      simulation.ref.store %j to %out : i32, !simulation.ref<i32>
      %nextj = arith.addi %j, %one : i32
      cf.br ^inner(%nextj : i32)
    ^next:
      %nextk = arith.addi %k, %one : i32
      cf.br ^middle(%nextk : i32)
    ^latch:
      %nexti = arith.addi %i, %one : i32
      cf.br ^outer(%nexti : i32)
    ^done:
      simulation.return
    }
  }
}

// -----

// The same nest without the unreachable stores stays under the ceiling and
// unrolls completely, so the refusal above is the construction bound rather
// than the shape or trip counts of the nest.
module {
  simulation.design @construction {
    simulation.scope.decl 0
    simulation.code_unit.decl 1 in 0 function hierarchy "empty_child"
    // CHECK-LABEL: simulation.func @empty_child(
    // CHECK-NOT: cf.cond_br
    // CHECK: simulation.return
    simulation.func @empty_child(%ctx: !simulation.context {simulation.capture_kind = 0 : i32}, %out: !simulation.ref<i32> {simulation.capture_kind = 1 : i32}) attributes {entry_kind = 8 : i32, code_unit_id = 1 : i64} {
      %zero = arith.constant 0 : i32
      %one = arith.constant 1 : i32
      %outer_limit = arith.constant 64 : i32
      %middle_limit = arith.constant 16 : i32
      cf.br ^outer(%zero : i32)
    ^outer(%i: i32):
      %more = arith.cmpi ult, %i, %outer_limit : i32
      cf.cond_br %more, ^middle(%zero : i32), ^done
    ^middle(%k: i32):
      %again = arith.cmpi ult, %k, %middle_limit : i32
      cf.cond_br %again, ^inner(%zero : i32), ^latch
    ^inner(%j: i32):
      %inside = arith.cmpi ult, %j, %zero : i32
      cf.cond_br %inside, ^body, ^next
    ^body:
      %nextj = arith.addi %j, %one : i32
      cf.br ^inner(%nextj : i32)
    ^next:
      %nextk = arith.addi %k, %one : i32
      cf.br ^middle(%nextk : i32)
    ^latch:
      %nexti = arith.addi %i, %one : i32
      cf.br ^outer(%nexti : i32)
    ^done:
      simulation.return
    }
  }
}
