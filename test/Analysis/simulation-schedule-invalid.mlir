// RUN: not obelisk-opt %s -o /dev/null \
// RUN:   --pass-pipeline='builtin.module(test-obelisk-simulation-schedule-analysis)' \
// RUN:   2>&1 | FileCheck %s

// CHECK: error: 'simulation.func' op compute-graph fragment block is out of range

module {
  simulation.design @schedule_invalid attributes {
    compute_graph = #schedule.graph<
      version = 1, vpi = off, workers = 1,
      nodes = [
        #schedule.fragment<id = 0, function = @root, block = 1,
          region = active, action = terminate, tier = native, cost = 0,
          lane = 0, twoState = true, effects = []>
      ],
      edges = [],
      regions = [
        #schedule.region<kind = active, groups = [
          #schedule.group<fragments = [0], schedule = acyclic, feedback = []>
        ]>,
        #schedule.region<kind = nba, groups = []>,
        #schedule.region<kind = observed, groups = []>,
        #schedule.region<kind = reactive, groups = []>,
        #schedule.region<kind = postponed, groups = []>
      ]>
  } {
    simulation.code_unit.decl 1 in 0 root_initializer hierarchy "root"
    simulation.scope.decl 0
    simulation.func @root(
        %ctx: !simulation.context {simulation.capture_kind = 0 : i32})
        attributes {entry_kind = 0 : i32, code_unit_id = 1 : i64} {
      simulation.return
    }
  }
}
