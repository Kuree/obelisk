// RUN: obelisk-opt %s --pass-pipeline='builtin.module(simulation.design(obelisk-sim-simplify-bodies{vpi=off}))' | FileCheck %s
// RUN: obelisk-opt %s --pass-pipeline='builtin.module(simulation.design(obelisk-sim-simplify-bodies{vpi=full}))' | FileCheck %s --check-prefix=VPI
// Snapshot forwarding uses a dense must-memory lattice. Keep publications,
// intersect at CFG joins, and kill facts at aliases and opaque boundaries.
module {
  simulation.design @memory {
    simulation.scope.decl 0
    simulation.storage.decl 0 in 0 : i32 design
    simulation.code_unit.decl 1 in 0 function hierarchy "diamond"
    simulation.code_unit.decl 2 in 0 function hierarchy "clobber"
    simulation.code_unit.decl 3 in 0 function hierarchy "pulse"
    simulation.code_unit.decl 4 in 0 function hierarchy "reads"
    simulation.code_unit.decl 5 in 0 function hierarchy "barrier"
    // CHECK-LABEL: simulation.func @diamond
    // CHECK: simulation.ref.store
    // CHECK-NOT: simulation.ref.load
    // CHECK: simulation.return
    simulation.func @diamond(%ctx: !simulation.context {simulation.capture_kind = 0 : i32}, %c: i1 {simulation.capture_kind = 2 : i32}) -> i32 attributes {entry_kind = 8 : i32, code_unit_id = 1 : i64} {
      %root = simulation.context.storage %ctx[0] : !simulation.ref<i32>
      %one = arith.constant 1 : i32
      simulation.ref.store %one to %root : i32, !simulation.ref<i32>
      cf.cond_br %c, ^left, ^right
    ^left:
      cf.br ^join
    ^right:
      cf.br ^join
    ^join:
      %v = simulation.ref.load %root : !simulation.ref<i32> -> i32
      simulation.return %v : i32
    }
    // CHECK-LABEL: simulation.func @clobber
    // CHECK: simulation.ref.load
    // CHECK: simulation.return
    simulation.func @clobber(%ctx: !simulation.context {simulation.capture_kind = 0 : i32}, %c: i1 {simulation.capture_kind = 2 : i32}) -> i32 attributes {entry_kind = 8 : i32, code_unit_id = 2 : i64} {
      %root = simulation.context.storage %ctx[0] : !simulation.ref<i32>
      %one = arith.constant 1 : i32
      %two = arith.constant 2 : i32
      simulation.ref.store %one to %root : i32, !simulation.ref<i32>
      cf.cond_br %c, ^left, ^right
    ^left:
      simulation.ref.store %two to %root : i32, !simulation.ref<i32>
      cf.br ^join
    ^right:
      cf.br ^join
    ^join:
      %v = simulation.ref.load %root : !simulation.ref<i32> -> i32
      simulation.return %v : i32
    }
    // CHECK-LABEL: simulation.func @pulse
    // CHECK-COUNT-3: simulation.ref.store
    // CHECK-NOT: simulation.ref.load
    // CHECK: simulation.return
    // VPI-LABEL: simulation.func @pulse
    // VPI-COUNT-3: simulation.ref.store
    // VPI: simulation.ref.load
    simulation.func @pulse(%ctx: !simulation.context {simulation.capture_kind = 0 : i32}) -> i32 attributes {entry_kind = 8 : i32, code_unit_id = 3 : i64} {
      %root = simulation.context.storage %ctx[0] : !simulation.ref<i32>
      %zero = arith.constant 0 : i32
      %one = arith.constant 1 : i32
      simulation.ref.store %zero to %root : i32, !simulation.ref<i32>
      simulation.ref.store %one to %root : i32, !simulation.ref<i32>
      simulation.ref.store %zero to %root : i32, !simulation.ref<i32>
      %v = simulation.ref.load %root : !simulation.ref<i32> -> i32
      simulation.return %v : i32
    }
    // CHECK-LABEL: simulation.func @reads
    // CHECK: %[[READ:.*]] = simulation.ref.load
    // CHECK-NOT: simulation.ref.load
    // CHECK: arith.addi %[[READ]], %[[READ]]
    simulation.func @reads(%ctx: !simulation.context {simulation.capture_kind = 0 : i32}) -> i32 attributes {entry_kind = 8 : i32, code_unit_id = 4 : i64} {
      %root = simulation.context.storage %ctx[0] : !simulation.ref<i32>
      %a = simulation.ref.load %root : !simulation.ref<i32> -> i32
      %b = simulation.ref.load %root : !simulation.ref<i32> -> i32
      %v = arith.addi %a, %b : i32
      simulation.return %v : i32
    }
    // CHECK-LABEL: simulation.func @barrier
    // CHECK: simulation.call @reads
    // CHECK: simulation.ref.load
    simulation.func @barrier(%ctx: !simulation.context {simulation.capture_kind = 0 : i32}) -> i32 attributes {entry_kind = 8 : i32, code_unit_id = 5 : i64} {
      %root = simulation.context.storage %ctx[0] : !simulation.ref<i32>
      %a = simulation.ref.load %root : !simulation.ref<i32> -> i32
      %b = simulation.call @reads(%ctx) : (!simulation.context) -> i32
      %c = simulation.ref.load %root : !simulation.ref<i32> -> i32
      simulation.return %c : i32
    }
  }
}
