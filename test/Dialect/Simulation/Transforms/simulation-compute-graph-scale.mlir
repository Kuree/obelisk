// RUN: talos-opt %s --split-input-file \
// RUN:   --pass-pipeline='builtin.module(test-talos-insert-block-chain,simulation.design(talos-sim-build-compute-graph,talos-sim-verify-compute-graph))' \
// RUN:   | FileCheck %s

// Generated process CFGs reach tens of thousands of blocks. Every graph
// traversal must therefore be iterative: a per-block recursion exhausts the
// stack long before it exhausts memory. Each marked branch expands into a
// chain of 20001 blocks in all, deep enough that a recursive traversal is not
// viable with conventional thread stack sizes.

// The chain is one long acyclic run, and the loop back to the top of the body
// is the only cycle in it.
// CHECK-LABEL: simulation.design @deep_loop
// CHECK: #schedule.region<kind = active
// CHECK-SAME: schedule = control_loop
// CHECK: #schedule.region<kind = postponed

module {
  simulation.design @deep_loop {
    simulation.code_unit.decl 1 in 0 initial hierarchy "test.deep.chain"
    simulation.scope.decl 0
    simulation.func @chain(
        %ctx: !simulation.context {simulation.capture_kind = 0 : i32})
        attributes {code_unit_id = 1 : i64, entry_kind = 1 : i32} {
      cf.br ^top
    ^top:
      cf.br ^bottom {test.chain_length = 19998 : i64}
    ^bottom:
      cf.br ^top
    }
  }
}

// -----

// An acyclic chain has many singleton SCCs. Classifying each singleton by
// rescanning every edge is quadratic even though SCC traversal is iterative.
// Exercise the stable internal-edge index and verify full node coverage: every
// block from the entry (0) to the return (20000) is its own acyclic group.
// CHECK-LABEL: simulation.design @deep_return
// CHECK-NOT: schedule = convergence
// CHECK-NOT: schedule = control_loop
// CHECK: #schedule.fragment<id = 20000
// CHECK-SAME: #schedule.region<kind = active
// CHECK-SAME: #schedule.group<fragments = [0], schedule = acyclic
// CHECK-SAME: #schedule.group<fragments = [20000], schedule = acyclic
// CHECK-NOT: schedule = convergence
// CHECK-NOT: schedule = control_loop
// CHECK-SAME: #schedule.region<kind = nba

module {
  simulation.design @deep_return {
    simulation.code_unit.decl 1 in 0 initial hierarchy "test.deep.chain"
    simulation.scope.decl 0
    simulation.func @chain(
        %ctx: !simulation.context {simulation.capture_kind = 0 : i32})
        attributes {code_unit_id = 1 : i64, entry_kind = 1 : i32} {
      cf.br ^last {test.chain_length = 19999 : i64}
    ^last:
      simulation.return
    }
  }
}
