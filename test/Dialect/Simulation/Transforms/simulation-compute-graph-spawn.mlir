// RUN: obelisk-opt %s --pass-pipeline='builtin.module(simulation.design(obelisk-sim-build-compute-graph,obelisk-sim-verify-compute-graph))' | FileCheck %s

module {
  simulation.design @spawns {
    simulation.code_unit.decl 9000001 in 0 function hierarchy "test.spawns.forks.9000001"
    simulation.code_unit.decl 9000002 in 0 initial hierarchy "test.spawns.first.9000002"
    simulation.code_unit.decl 9000003 in 0 initial hierarchy "test.spawns.second.9000003"
    simulation.scope.decl 0

    // A spawn creates an actor; it is not a value fed back into the current
    // activation, so it must not make its own schedule group cyclic.
    // CHECK: kind = spawn
    // CHECK-NOT: schedule = convergence
    simulation.func @root(
        %ctx: !simulation.context {simulation.capture_kind = 0 : i32})
        attributes {entry_kind = 0 : i32} {
      %direct = simulation.spawn @first(%ctx) : !simulation.context -> !simulation.process
      // `fork ... join_none` inside a zero-time function is legal and still
      // starts a process, so the edge is derived through the call graph.
      simulation.call @forks(%ctx) : (!simulation.context) -> ()
      simulation.return
    }

    simulation.func @forks(
        %ctx: !simulation.context {simulation.capture_kind = 0 : i32})
        attributes {entry_kind = 8 : i32, code_unit_id = 9000001 : i64} {
      %indirect = simulation.spawn @second(%ctx) : !simulation.context -> !simulation.process
      simulation.return
    }

    // Two processes that spawn each other form a spawn cycle. That is a legal
    // testbench, not a zero-time convergence loop.
    simulation.func @first(
        %ctx: !simulation.context {simulation.capture_kind = 0 : i32})
        attributes {entry_kind = 1 : i32, code_unit_id = 9000002 : i64} {
      %p = simulation.spawn @second(%ctx) : !simulation.context -> !simulation.process
      simulation.return
    }

    simulation.func @second(
        %ctx: !simulation.context {simulation.capture_kind = 0 : i32})
        attributes {entry_kind = 1 : i32, code_unit_id = 9000003 : i64} {
      %p = simulation.spawn @first(%ctx) : !simulation.context -> !simulation.process
      simulation.return
    }
  }
}
