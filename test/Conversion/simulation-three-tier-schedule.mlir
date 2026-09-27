// RUN: obelisk-opt %s --pass-pipeline='builtin.module(simulation.design(obelisk-sim-materialize-graph-regions{max-acyclic-cost=1}))' | FileCheck %s
// RUN: obelisk-opt %s --pass-pipeline='builtin.module(simulation.design(obelisk-sim-materialize-graph-regions{max-acyclic-cost=1}))' -o %t
// RUN: obelisk-opt %t -o %t.roundtrip
// RUN: diff %t %t.roundtrip

module {
  // Aliased process formals watching the same descriptor/range/edge collapse
  // into one physical trigger and retain distinct ready bits.  Opposite edges
  // and an asynchronous reset remain distinct.  Multi-clock cones, native
  // convergence SCCs, and bytecode islands each have one exclusive owner.
  // Partially overlapping Tier-1/Tier-3 writes are partitioned: only the
  // overlap receives a Tier-3 barrier owner.
  // CHECK: schedule.three_tier_schedule = #schedule.three_tier_schedule<
  // CHECK-SAME: ownerCount = 7
  // CHECK-SAME: triggers = [#schedule.trigger_group<id = 0, key = <resource = storage, descriptor = 7, low = 0, width = 1, edge = posedge>>
  // CHECK-SAME: #schedule.trigger_group<id = 1, key = <resource = storage, descriptor = 7, low = 0, width = 1, edge = negedge>>
  // CHECK-SAME: #schedule.trigger_group<id = 2, key = <resource = storage, descriptor = 8, low = 0, width = 1, edge = negedge>>]
  // CHECK-SAME: kernels = [#schedule.scheduled_kernel<id = 0, owner = 0, readyBit = 0, tier = tier1, region = active, schedule = acyclic, shared = false, loweringReady = true, twoStateEligible = true, promotionRoots = [#schedule.inductive_root<resource = storage, descriptor = 7>], fragments = [0]>
  // CHECK-SAME: #schedule.scheduled_kernel<id = 1, owner = 0, readyBit = 1, tier = tier1, region = active, schedule = acyclic, shared = false, loweringReady = true, twoStateEligible = true, promotionRoots = [#schedule.inductive_root<resource = storage, descriptor = 7>], fragments = [1]>
  // CHECK-SAME: #schedule.scheduled_kernel<id = 2, owner = 1, readyBit = 0, tier = tier1, region = active, schedule = acyclic, shared = false, loweringReady = true, twoStateEligible = false, promotionRoots = [], fragments = [2]>
  // CHECK-SAME: #schedule.scheduled_kernel<id = 3, owner = 3, readyBit = 0, tier = tier1, region = active, schedule = acyclic, shared = true, loweringReady = true, twoStateEligible = true, promotionRoots = [#schedule.inductive_root<resource = storage, descriptor = 7>, #schedule.inductive_root<resource = storage, descriptor = 8>], fragments = [3]>
  // CHECK-SAME: #schedule.scheduled_kernel<id = 4, owner = 4, readyBit = 0, tier = tier2, region = active, schedule = convergence, shared = true, loweringReady = true, twoStateEligible = false, promotionRoots = [], fragments = [4]>
  // CHECK-SAME: #schedule.scheduled_kernel<id = 5, owner = 5, readyBit = 0, tier = tier3, region = active, schedule = acyclic, shared = true, loweringReady = false, twoStateEligible = false, promotionRoots = [], fragments = [5]>
  // CHECK-SAME: #schedule.scheduled_kernel<id = 6, owner = 0, readyBit = 2, tier = tier1, region = active, schedule = acyclic, shared = false, loweringReady = true, twoStateEligible = true, promotionRoots = [#schedule.inductive_root<resource = storage, descriptor = 7>, #schedule.inductive_root<resource = storage, descriptor = 99>], fragments = [6]>]
  // CHECK-SAME: roots = [#schedule.scheduled_root<resource = storage, descriptor = 99, low = 0, width = 4, owner = 0, tier = tier1>
  // CHECK-SAME: #schedule.scheduled_root<resource = storage, descriptor = 99, low = 4, width = 4, owner = 6, tier = tier3>
  // CHECK-SAME: #schedule.scheduled_root<resource = storage, descriptor = 99, low = 8, width = 4, owner = 5, tier = tier3>]
  // CHECK-SAME: ingress = [#schedule.scheduler_ingress<trigger = 0, owner = 0, readyBit = 0, fragment = 0>
  // CHECK-SAME: #schedule.scheduler_ingress<trigger = 0, owner = 0, readyBit = 1, fragment = 1>
  // CHECK-SAME: #schedule.scheduler_ingress<trigger = 1, owner = 1, readyBit = 0, fragment = 2>
  // CHECK-SAME: #schedule.scheduler_ingress<trigger = 0, owner = 3, readyBit = 0, fragment = 3>
  // CHECK-SAME: #schedule.scheduler_ingress<trigger = 2, owner = 3, readyBit = 0, fragment = 3>
  // CHECK-SAME: #schedule.scheduler_ingress<trigger = 0, owner = 4, readyBit = 0, fragment = 4>
  // CHECK-SAME: #schedule.scheduler_ingress<trigger = 0, owner = 5, readyBit = 0, fragment = 5>
  // CHECK-SAME: #schedule.scheduler_ingress<trigger = 0, owner = 0, readyBit = 2, fragment = 6>]
  simulation.design @schedule attributes {
    compute_graph = #schedule.graph<
      version = 1, vpi = full, workers = 1,
      nodes = [
        #schedule.fragment<id = 0, function = @alias_a, block = 0,
          region = active, action = suspend_edge, tier = native, cost = 1,
          lane = 0, twoState = true, effects = [
            #schedule.effect<effect = watch, resource = storage,
              target = descriptor, descriptor = 7, formal = 0, low = 0,
              width = 1, dynamic = false, deferred = false,
              trigger = posedge>]>,
        #schedule.fragment<id = 1, function = @alias_b, block = 0,
          region = active, action = suspend_edge, tier = native, cost = 1,
          lane = 0, twoState = true, effects = [
            #schedule.effect<effect = watch, resource = storage,
              target = descriptor, descriptor = 7, formal = 0, low = 0,
              width = 1, dynamic = false, deferred = false,
              trigger = posedge>]>,
        #schedule.fragment<id = 2, function = @fall, block = 0,
          region = active, action = suspend_edge, tier = native, cost = 1,
          lane = 0, twoState = false, effects = [
            #schedule.effect<effect = watch, resource = storage,
              target = descriptor, descriptor = 7, formal = 0, low = 0,
              width = 1, dynamic = false, deferred = false,
              trigger = negedge>]>,
        #schedule.fragment<id = 3, function = @clock_reset, block = 0,
          region = active, action = suspend_any, tier = native, cost = 1,
          lane = 0, twoState = true, effects = [
            #schedule.effect<effect = watch, resource = storage,
              target = descriptor, descriptor = 7, formal = 0, low = 0,
              width = 1, dynamic = false, deferred = false,
              trigger = posedge>,
            #schedule.effect<effect = watch, resource = storage,
              target = descriptor, descriptor = 8, formal = 0, low = 0,
              width = 1, dynamic = false, deferred = false,
              trigger = negedge>]>,
        #schedule.fragment<id = 4, function = @settle, block = 0,
          region = active, action = suspend_change, tier = native, cost = 1,
          lane = 0, twoState = true, effects = [
            #schedule.effect<effect = watch, resource = storage,
              target = descriptor, descriptor = 7, formal = 0, low = 0,
              width = 1, dynamic = false, deferred = false,
              trigger = posedge>]>,
        #schedule.fragment<id = 5, function = @dynamic, block = 0,
          region = active, action = suspend_edge, tier = bytecode, cost = 1,
          lane = 0, twoState = false, effects = [
            #schedule.effect<effect = watch, resource = storage,
              target = descriptor, descriptor = 7, formal = 0, low = 0,
              width = 1, dynamic = false, deferred = false,
              trigger = posedge>,
            #schedule.effect<effect = write, resource = storage,
              target = descriptor, descriptor = 99, formal = 0, low = 4,
              width = 8, dynamic = false, deferred = false,
              trigger = none>]>,
        #schedule.fragment<id = 6, function = @mixed_writer, block = 0,
          region = active, action = suspend_edge, tier = native, cost = 1,
          lane = 0, twoState = true, effects = [
            #schedule.effect<effect = watch, resource = storage,
              target = descriptor, descriptor = 7, formal = 0, low = 0,
              width = 1, dynamic = false, deferred = false,
              trigger = posedge>,
            #schedule.effect<effect = write, resource = storage,
              target = descriptor, descriptor = 99, formal = 0, low = 0,
              width = 8, dynamic = false, deferred = false,
              trigger = none>]>],
      edges = [],
      regions = [
        #schedule.region<kind = active, groups = [
          #schedule.group<fragments = [0], schedule = acyclic, feedback = []>,
          #schedule.group<fragments = [1], schedule = acyclic, feedback = []>,
          #schedule.group<fragments = [2], schedule = acyclic, feedback = []>,
          #schedule.group<fragments = [3], schedule = acyclic, feedback = []>,
          #schedule.group<fragments = [4], schedule = convergence, feedback = []>,
          #schedule.group<fragments = [5], schedule = acyclic, feedback = []>,
          #schedule.group<fragments = [6], schedule = acyclic, feedback = []>]>,
        #schedule.region<kind = nba, groups = []>,
        #schedule.region<kind = observed, groups = []>,
        #schedule.region<kind = reactive, groups = []>,
        #schedule.region<kind = postponed, groups = []>]>
  } {
    simulation.scope.decl 0
  }
}
