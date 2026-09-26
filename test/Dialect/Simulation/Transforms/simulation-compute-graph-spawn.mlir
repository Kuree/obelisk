// RUN: obelisk-opt %s --pass-pipeline='builtin.module(obelisk_sim.design(obelisk-sim-build-compute-graph,obelisk-sim-verify-compute-graph))' | FileCheck %s

module {
  obelisk_sim.design @spawns {
    obelisk_sim.code_unit.decl 9000001 in 0 function hierarchy "test.spawns.forks.9000001"
    obelisk_sim.code_unit.decl 9000002 in 0 initial hierarchy "test.spawns.first.9000002"
    obelisk_sim.code_unit.decl 9000003 in 0 initial hierarchy "test.spawns.second.9000003"
    obelisk_sim.scope.decl 0

    // A spawn creates an actor; it is not a value fed back into the current
    // activation, so it must not make its own schedule group cyclic.
    // CHECK: kind = spawn
    // CHECK-NOT: schedule = convergence
    obelisk_sim.func @root(
        %ctx: !obelisk_sim.context {obelisk_sim.capture_kind = 0 : i32})
        attributes {entry_kind = 0 : i32} {
      %direct = obelisk_sim.spawn @first(%ctx) : !obelisk_sim.context -> !obelisk_sim.process
      // `fork ... join_none` inside a zero-time function is legal and still
      // starts a process, so the edge is derived through the call graph.
      obelisk_sim.call @forks(%ctx) : (!obelisk_sim.context) -> ()
      obelisk_sim.return
    }

    obelisk_sim.func @forks(
        %ctx: !obelisk_sim.context {obelisk_sim.capture_kind = 0 : i32})
        attributes {entry_kind = 8 : i32, code_unit_id = 9000001 : i64} {
      %indirect = obelisk_sim.spawn @second(%ctx) : !obelisk_sim.context -> !obelisk_sim.process
      obelisk_sim.return
    }

    // Two processes that spawn each other form a spawn cycle. That is a legal
    // testbench, not a zero-time convergence loop.
    obelisk_sim.func @first(
        %ctx: !obelisk_sim.context {obelisk_sim.capture_kind = 0 : i32})
        attributes {entry_kind = 1 : i32, code_unit_id = 9000002 : i64} {
      %p = obelisk_sim.spawn @second(%ctx) : !obelisk_sim.context -> !obelisk_sim.process
      obelisk_sim.return
    }

    obelisk_sim.func @second(
        %ctx: !obelisk_sim.context {obelisk_sim.capture_kind = 0 : i32})
        attributes {entry_kind = 1 : i32, code_unit_id = 9000003 : i64} {
      %p = obelisk_sim.spawn @first(%ctx) : !obelisk_sim.context -> !obelisk_sim.process
      obelisk_sim.return
    }
  }
}
