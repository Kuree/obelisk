// RUN: obelisk-opt %s -o /dev/null \
// RUN:   --pass-pipeline='builtin.module(test-obelisk-simulation-schedule-analysis)' \
// RUN:   2>&1 | FileCheck %s --check-prefix=FALLBACK
// RUN: obelisk-opt %s -o /dev/null \
// RUN:   --pass-pipeline='builtin.module(simulation.design(obelisk-sim-build-compute-graph,obelisk-sim-verify-compute-graph),test-obelisk-simulation-schedule-analysis)' \
// RUN:   2>&1 | FileCheck %s --check-prefix=GRAPH

// The fallback preserves semantic IR order. Printing is name-sorted so these
// checks also prove that ranks are facts, not an artifact of output order.
// FALLBACK: schedule @schedule
// FALLBACK-NEXT: func @a_initial entry=1
// FALLBACK-NEXT: bb0 rank=1
// FALLBACK-NEXT: func @root entry=0
// FALLBACK-NEXT: bb0 rank=0
// FALLBACK-NEXT: func @z_always entry=2
// FALLBACK-NEXT: bb0 rank=2

// The compute graph overrides both backend fallbacks with scheduler order.
// GRAPH: schedule @schedule
// GRAPH-NEXT: func @a_initial entry=2
// GRAPH-NEXT: bb0 rank=2
// GRAPH-NEXT: func @root entry=0
// GRAPH-NEXT: bb0 rank=0
// GRAPH-NEXT: func @z_always entry=1
// GRAPH-NEXT: bb0 rank=1

module {
  simulation.design @schedule {
    simulation.code_unit.decl 1 in 0 root_initializer hierarchy "root"
    simulation.code_unit.decl 2 in 0 initial hierarchy "a_initial"
    simulation.code_unit.decl 3 in 0 always hierarchy "z_always"
    simulation.scope.decl 0

    simulation.func @root(
        %ctx: !simulation.context
            {simulation.capture_kind = 0 : i32})
        attributes {entry_kind = 0 : i32, code_unit_id = 1 : i64} {
      %always = simulation.spawn @z_always(%ctx) :
          !simulation.context -> !simulation.process
      %initial = simulation.spawn @a_initial(%ctx) :
          !simulation.context -> !simulation.process
      simulation.return
    }

    simulation.func @a_initial(
        %ctx: !simulation.context
            {simulation.capture_kind = 0 : i32})
        attributes {entry_kind = 1 : i32, code_unit_id = 2 : i64} {
      simulation.return
    }

    simulation.func @z_always(
        %ctx: !simulation.context
            {simulation.capture_kind = 0 : i32})
        attributes {entry_kind = 3 : i32, code_unit_id = 3 : i64} {
      simulation.return
    }
  }
}
