// RUN: obelisk-opt %s --pass-pipeline='builtin.module(simulation.design(obelisk-sim-build-compute-graph,obelisk-sim-verify-compute-graph),test-obelisk-simulation-schedule-analysis)' > %t.graph 2> %t.ranks
// RUN: FileCheck %s --check-prefix=GRAPH < %t.graph
// RUN: FileCheck %s --check-prefix=RANK < %t.ranks

// Initial writes activate the startup waits. Together with the implicit
// startup phase this is a cycle: dropping the phase would split the SCC.
// Both groups and ranks match the original explicit Cartesian-product graph.
// GRAPH: regions = [#schedule.region<kind = active, groups = [#schedule.group<fragments = [0, 2, 4, 5], schedule = convergence
// GRAPH-SAME: #schedule.group<fragments = [1], schedule = acyclic
// GRAPH-SAME: #schedule.group<fragments = [3], schedule = acyclic
// GRAPH-SAME: #schedule.group<fragments = [6], schedule = acyclic

// RANK: schedule @startup_phases
// RANK-NEXT: func @always0 entry=0
// RANK-NEXT: bb0 rank=0
// RANK-NEXT: bb1 rank=4
// RANK-NEXT: func @always1 entry=2
// RANK-NEXT: bb0 rank=2
// RANK-NEXT: bb1 rank=5
// RANK-NEXT: func @initial0 entry=1
// RANK-NEXT: bb0 rank=1
// RANK-NEXT: func @initial1 entry=3
// RANK-NEXT: bb0 rank=3
// RANK-NEXT: func @root entry=6
// RANK-NEXT: bb0 rank=6

module {
  simulation.design @startup_phases {
    simulation.scope.decl 0
    simulation.code_unit.decl 1 in 0 root_initializer hierarchy "root"
    simulation.storage.decl 0 in 0 : i1 design
    simulation.code_unit.decl 2 in 0 initial hierarchy "initial0"
    simulation.code_unit.decl 3 in 0 initial hierarchy "initial1"
    simulation.code_unit.decl 4 in 0 always hierarchy "always0"
    simulation.code_unit.decl 5 in 0 always hierarchy "always1"
    simulation.func @root(%ctx: !simulation.context {simulation.capture_kind = 0 : i32})
        attributes {entry_kind = 0 : i32, code_unit_id = 1 : i64} {
      %p_initial0 = simulation.spawn @initial0(%ctx) :
          !simulation.context -> !simulation.process
      %p_always0 = simulation.spawn @always0(%ctx) :
          !simulation.context -> !simulation.process
      %p_initial1 = simulation.spawn @initial1(%ctx) :
          !simulation.context -> !simulation.process
      %p_always1 = simulation.spawn @always1(%ctx) :
          !simulation.context -> !simulation.process
      simulation.return
    }
    simulation.func @initial0(%ctx: !simulation.context {simulation.capture_kind = 0 : i32})
        attributes {entry_kind = 1 : i32, code_unit_id = 2 : i64} {
      %ref = simulation.context.storage %ctx[0] : !simulation.ref<i1>
      %one = arith.constant true
      simulation.ref.store %one to %ref : i1, !simulation.ref<i1>
      simulation.return
    }
    simulation.func @initial1(%ctx: !simulation.context {simulation.capture_kind = 0 : i32})
        attributes {entry_kind = 1 : i32, code_unit_id = 3 : i64} {
      %ref = simulation.context.storage %ctx[0] : !simulation.ref<i1>
      %one = arith.constant true
      simulation.ref.store %one to %ref : i1, !simulation.ref<i1>
      simulation.return
    }
    simulation.func @always0(%ctx: !simulation.context {simulation.capture_kind = 0 : i32})
        attributes {entry_kind = 3 : i32, code_unit_id = 4 : i64} {
      %ref = simulation.context.storage %ctx[0] : !simulation.ref<i1>
      simulation.suspend.change %ref to ^done : !simulation.ref<i1>
    ^done:
      simulation.return
    }
    simulation.func @always1(%ctx: !simulation.context {simulation.capture_kind = 0 : i32})
        attributes {entry_kind = 3 : i32, code_unit_id = 5 : i64} {
      %ref = simulation.context.storage %ctx[0] : !simulation.ref<i1>
      simulation.suspend.change %ref to ^done : !simulation.ref<i1>
    ^done:
      simulation.return
    }
  }
}
