// RUN: %python %S/Inputs/expand-zero-trip-body.py %s > %t.mlir
// RUN: obelisk-opt %t.mlir --pass-pipeline='builtin.module(simulation.design(simulation.func(obelisk-sim-unroll-bounded-loops{maximum-loop-size=1000})))' | FileCheck %s

// The final expansion would contain no stores, but outer-first replication
// would temporarily copy 300 unreachable stores 64 times. Bound intermediate
// construction before making the trial copy, not just the final expanded nest.
// Refusing the trial keeps the original CFG and source-order effects intact
// (IEEE 1800-2023 4.6 and 12.7.1).
module {
  simulation.design @construction {
    simulation.scope.decl 0
    simulation.code_unit.decl 1 in 0 function hierarchy "zero_trip_child"
    // CHECK-LABEL: simulation.func @zero_trip_child(
    // CHECK: cf.cond_br
    // CHECK: cf.cond_br
    // CHECK-COUNT-300: simulation.ref.store
    // CHECK-NOT: simulation.ref.store
    // CHECK: simulation.return
    simulation.func @zero_trip_child(%ctx: !simulation.context {simulation.capture_kind = 0 : i32}, %out: !simulation.ref<i32> {simulation.capture_kind = 1 : i32}) attributes {entry_kind = 8 : i32, code_unit_id = 1 : i64} {
      %zero = arith.constant 0 : i32
      %one = arith.constant 1 : i32
      %limit = arith.constant 64 : i32
      cf.br ^outer(%zero : i32)
    ^outer(%i: i32):
      %more = arith.cmpi ult, %i, %limit : i32
      cf.cond_br %more, ^inner(%zero : i32), ^done
    ^inner(%j: i32):
      %inside = arith.cmpi ult, %j, %zero : i32
      cf.cond_br %inside, ^body, ^latch
    ^body:
      // EXPAND-ZERO-TRIP-BODY
      %nextj = arith.addi %j, %one : i32
      cf.br ^inner(%nextj : i32)
    ^latch:
      %nexti = arith.addi %i, %one : i32
      cf.br ^outer(%nexti : i32)
    ^done:
      simulation.return
    }
  }
}
