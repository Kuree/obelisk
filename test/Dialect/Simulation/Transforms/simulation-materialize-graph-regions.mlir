// RUN: obelisk-opt %s --pass-pipeline='builtin.module(obelisk_sim.design(obelisk-sim-materialize-graph-regions{max-acyclic-cost=16}))' | FileCheck %s
// RUN: obelisk-opt %s --pass-pipeline='builtin.module(obelisk_sim.design(obelisk-sim-materialize-graph-regions{max-acyclic-cost=10}))' | FileCheck %s --check-prefix=LIMIT

module {
  // Adjacent native acyclic groups form one coarse kernel. The bytecode group
  // remains a fine-grained handoff boundary, and the following native group is
  // a separate kernel. Static commit groups can coarsen as lowering-ready
  // work. Placement balances whole kernels across worker lanes.
  // CHECK: obelisk_sim.design @regions attributes {
  // CHECK-SAME: compute_graph = #schedule.graph<
  // CHECK-SAME: workers = 2
  // CHECK-SAME: #schedule.group<fragments = [0], schedule = acyclic
  // CHECK-SAME: #schedule.group<fragments = [1], schedule = acyclic
  // CHECK-SAME: #schedule.group<fragments = [2], schedule = acyclic
  // CHECK-SAME: #schedule.group<fragments = [3], schedule = acyclic
  // CHECK-SAME: #schedule.group<fragments = [4], schedule = control_loop
  // CHECK-SAME: #schedule.group<fragments = [5], schedule = convergence
  // CHECK-SAME: schedule.compute_kernels = [
  // CHECK-SAME: #schedule.kernel<id = 0, region = active, schedule = acyclic, lane = 0, cost = 12, loweringReady = true, fragments = [0, 1]>
  // CHECK-SAME: #schedule.kernel<id = 1, region = active, schedule = acyclic, lane = 0, cost = 20, loweringReady = false, fragments = [2]>
  // CHECK-SAME: #schedule.kernel<id = 2, region = active, schedule = acyclic, lane = 1, cost = 8, loweringReady = true, fragments = [3]>
  // CHECK-SAME: #schedule.kernel<id = 3, region = active, schedule = control_loop, lane = 0, cost = 7, loweringReady = false, fragments = [4]>
  // CHECK-SAME: #schedule.kernel<id = 4, region = active, schedule = convergence, lane = 1, cost = 6, loweringReady = true, fragments = [5]>
  // CHECK-SAME: #schedule.kernel<id = 5, region = nba, schedule = acyclic, lane = 0, cost = 4, loweringReady = true, fragments = [6, 7]>]
  // LIMIT: schedule.compute_kernels = [
  // LIMIT-SAME: #schedule.kernel<id = 0, region = active, schedule = acyclic, lane = 0, cost = 9, loweringReady = true, fragments = [0]>
  // LIMIT-SAME: #schedule.kernel<id = 1, region = active, schedule = acyclic, lane = 0, cost = 3, loweringReady = true, fragments = [1]>
  obelisk_sim.design @regions attributes {
    compute_graph = #schedule.graph<
      version = 1, vpi = full, workers = 2,
      nodes = [
        #schedule.fragment<id = 0, function = @a, block = 0,
          region = active, action = terminate, tier = native, cost = 9,
          lane = 0, twoState = true, effects = []>,
        #schedule.fragment<id = 1, function = @b, block = 0,
          region = active, action = terminate, tier = native, cost = 3,
          lane = 0, twoState = true, effects = []>,
        #schedule.fragment<id = 2, function = @dynamic, block = 0,
          region = active, action = terminate, tier = bytecode, cost = 20,
          lane = 0, twoState = false, effects = []>,
        #schedule.fragment<id = 3, function = @c, block = 0,
          region = active, action = terminate, tier = native, cost = 8,
          lane = 0, twoState = true, effects = []>,
        #schedule.fragment<id = 4, function = @loop, block = 0,
          region = active, action = terminate, tier = native, cost = 7,
          lane = 0, twoState = true, effects = []>,
        #schedule.fragment<id = 5, function = @settle, block = 0,
          region = active, action = terminate, tier = native, cost = 6,
          lane = 0, twoState = true, effects = []>,
        #schedule.nba_commit<id = 6, slots = [0, 1], accumulatorSites = [2],
          frontierSites = [],
          effect = <effect = write, resource = storage, target = descriptor,
                    descriptor = 0, formal = 0, low = 0, width = 8,
                    dynamic = false, deferred = false, trigger = none>>,
        #schedule.nba_commit<id = 7, slots = [], accumulatorSites = [],
          frontierSites = [3],
          effect = <effect = write, resource = storage, target = descriptor,
                    descriptor = 1, formal = 0, low = 0, width = 8,
                    dynamic = false, deferred = false, trigger = none>>],
      edges = [],
      regions = [
        #schedule.region<kind = active, groups = [
          #schedule.group<fragments = [0], schedule = acyclic, feedback = []>,
          #schedule.group<fragments = [1], schedule = acyclic, feedback = []>,
          #schedule.group<fragments = [2], schedule = acyclic, feedback = []>,
          #schedule.group<fragments = [3], schedule = acyclic, feedback = []>,
          #schedule.group<fragments = [4], schedule = control_loop, feedback = []>,
          #schedule.group<fragments = [5], schedule = convergence, feedback = []>]>,
        #schedule.region<kind = nba, groups = [
          #schedule.group<fragments = [6], schedule = acyclic, feedback = []>,
          #schedule.group<fragments = [7], schedule = acyclic, feedback = []>]>,
        #schedule.region<kind = observed, groups = []>,
        #schedule.region<kind = reactive, groups = []>,
        #schedule.region<kind = postponed, groups = []>]>
  } {
    obelisk_sim.scope.decl 0
  }
}
