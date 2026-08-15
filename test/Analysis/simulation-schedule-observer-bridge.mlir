// RUN: obelisk-opt %s -o /dev/null \
// RUN:   --pass-pipeline='builtin.module(test-obelisk-simulation-schedule-analysis)' \
// RUN:   2>&1 | FileCheck %s

// Observer capture bridges are physical CFG blocks but not compute-graph
// fragment ordinals. Graph block 1 therefore resolves to physical bb2.
// CHECK: schedule @observer_bridge
// CHECK-NEXT: func @observer entry=0
// CHECK-NEXT: bb0 rank=0
// CHECK-NEXT: bb2 rank=1
// CHECK-NOT: bb1

module {
  obelisk_sim.design @observer_bridge attributes {
    compute_graph = #obelisk_sim.graph<
      version = 1, vpi = off, workers = 1,
      nodes = [
        #obelisk_sim.fragment<id = 0, function = @observer, block = 0,
          region = observed, action = continue, tier = native, cost = 0,
          lane = 0, twoState = true, effects = []>,
        #obelisk_sim.fragment<id = 1, function = @observer, block = 1,
          region = observed, action = terminate, tier = native, cost = 0,
          lane = 0, twoState = true, effects = []>
      ],
      edges = [],
      regions = [
        #obelisk_sim.region<kind = active, groups = []>,
        #obelisk_sim.region<kind = nba, groups = []>,
        #obelisk_sim.region<kind = observed, groups = [
          #obelisk_sim.group<fragments = [0], schedule = acyclic, feedback = []>,
          #obelisk_sim.group<fragments = [1], schedule = acyclic, feedback = []>
        ]>,
        #obelisk_sim.region<kind = reactive, groups = []>,
        #obelisk_sim.region<kind = postponed, groups = []>
      ]>
  } {
    obelisk_sim.code_unit.decl 1 in 0 observer hierarchy "observer"
    obelisk_sim.scope.decl 0
    obelisk_sim.func @observer(
        %ctx: !obelisk_sim.context {obelisk_sim.capture_kind = 0 : i32})
        -> i1
        attributes {entry_kind = 14 : i32, code_unit_id = 1 : i64} {
      cf.br ^bridge
    ^bridge:
      cf.br ^body {obelisk_sim.observer_capture_bridge}
    ^body:
      %true = arith.constant true
      obelisk_sim.return %true : i1
    }
  }
}
