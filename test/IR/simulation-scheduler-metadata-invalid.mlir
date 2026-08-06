// RUN: obelisk-opt --split-input-file --verify-diagnostics %s

obelisk_sim.design @wrong_ingress attributes {
  compute_graph = #obelisk_sim.graph<
    version = 1, vpi = off, workers = 1,
    nodes = [#obelisk_sim.fragment<id = 0, function = @watch, block = 0,
      region = active, action = suspend_edge, tier = native, cost = 1,
      lane = 0, twoState = false, effects = [
        #obelisk_sim.effect<effect = watch, resource = storage,
          target = descriptor, descriptor = 7, formal = 0, low = 0,
          width = 1, dynamic = false, deferred = false,
          trigger = posedge>]>],
    edges = [], regions = [
      #obelisk_sim.region<kind = active, groups = [
        #obelisk_sim.group<fragments = [0], schedule = acyclic,
          feedback = []>]>,
      #obelisk_sim.region<kind = nba, groups = []>,
      #obelisk_sim.region<kind = observed, groups = []>,
      #obelisk_sim.region<kind = reactive, groups = []>,
      #obelisk_sim.region<kind = postponed, groups = []>]>,
  // expected-error @+1 {{scheduler ingress is invalid or duplicated}}
  obelisk_sim.three_tier_schedule = #obelisk_sim.three_tier_schedule<
    version = 1, sourceGraph = #obelisk_sim.graph<
      version = 1, vpi = off, workers = 1,
      nodes = [#obelisk_sim.fragment<id = 0, function = @watch, block = 0,
        region = active, action = suspend_edge, tier = native, cost = 1,
        lane = 0, twoState = false, effects = [
          #obelisk_sim.effect<effect = watch, resource = storage,
            target = descriptor, descriptor = 7, formal = 0, low = 0,
            width = 1, dynamic = false, deferred = false,
            trigger = posedge>]>],
      edges = [], regions = [
        #obelisk_sim.region<kind = active, groups = [
          #obelisk_sim.group<fragments = [0], schedule = acyclic,
            feedback = []>]>,
        #obelisk_sim.region<kind = nba, groups = []>,
        #obelisk_sim.region<kind = observed, groups = []>,
        #obelisk_sim.region<kind = reactive, groups = []>,
        #obelisk_sim.region<kind = postponed, groups = []>]>,
    ownerCount = 2, triggers = [
      #obelisk_sim.trigger_group<id = 0, key = <resource = storage,
        descriptor = 7, low = 0, width = 1, edge = posedge>>,
      #obelisk_sim.trigger_group<id = 1, key = <resource = storage,
        descriptor = 7, low = 0, width = 1, edge = negedge>>],
    kernels = [#obelisk_sim.scheduled_kernel<id = 0, owner = 0,
      readyBit = 0, tier = tier1, region = active, schedule = acyclic,
      shared = false, loweringReady = true, twoStateEligible = false,
      promotionRoots = [], fragments = [0]>], roots = [], ingress = [
        #obelisk_sim.scheduler_ingress<trigger = 1, owner = 0, readyBit = 0,
          fragment = 0>]>
} {
  obelisk_sim.scope.decl 0
}

// -----

module attributes {
  // expected-error @+1 {{effect packed range overflows}}
  test.effect = #obelisk_sim.effect<effect = read, resource = storage,
    target = descriptor, descriptor = 0, formal = 0,
    low = 18446744073709551615, width = 2, dynamic = false,
    deferred = false, trigger = none>
}

// -----

obelisk_sim.design @wrong_root attributes {
  compute_graph = #obelisk_sim.graph<
    version = 1, vpi = off, workers = 1,
    nodes = [#obelisk_sim.fragment<id = 0, function = @writer, block = 0,
      region = active, action = terminate, tier = native, cost = 1,
      lane = 0, twoState = false, effects = [
        #obelisk_sim.effect<effect = write, resource = storage,
          target = descriptor, descriptor = 9, formal = 0, low = 0,
          width = 8, dynamic = false, deferred = false, trigger = none>]>],
    edges = [], regions = [
      #obelisk_sim.region<kind = active, groups = [
        #obelisk_sim.group<fragments = [0], schedule = acyclic,
          feedback = []>]>,
      #obelisk_sim.region<kind = nba, groups = []>,
      #obelisk_sim.region<kind = observed, groups = []>,
      #obelisk_sim.region<kind = reactive, groups = []>,
      #obelisk_sim.region<kind = postponed, groups = []>]>,
  // expected-error @+1 {{scheduled-root owner or tier disagrees with writers}}
  obelisk_sim.three_tier_schedule = #obelisk_sim.three_tier_schedule<
    version = 1, sourceGraph = #obelisk_sim.graph<
      version = 1, vpi = off, workers = 1,
      nodes = [#obelisk_sim.fragment<id = 0, function = @writer, block = 0,
        region = active, action = terminate, tier = native, cost = 1,
        lane = 0, twoState = false, effects = [
          #obelisk_sim.effect<effect = write, resource = storage,
            target = descriptor, descriptor = 9, formal = 0, low = 0,
            width = 8, dynamic = false, deferred = false, trigger = none>]>],
      edges = [], regions = [
        #obelisk_sim.region<kind = active, groups = [
          #obelisk_sim.group<fragments = [0], schedule = acyclic,
            feedback = []>]>,
        #obelisk_sim.region<kind = nba, groups = []>,
        #obelisk_sim.region<kind = observed, groups = []>,
        #obelisk_sim.region<kind = reactive, groups = []>,
        #obelisk_sim.region<kind = postponed, groups = []>]>,
    ownerCount = 1, triggers = [], kernels = [
      #obelisk_sim.scheduled_kernel<id = 0, owner = 0, readyBit = 0,
        tier = tier1, region = active, schedule = acyclic, shared = true,
        loweringReady = true, twoStateEligible = false,
        promotionRoots = [], fragments = [0]>], roots = [
      #obelisk_sim.scheduled_root<resource = storage, descriptor = 9,
        low = 0, width = 8, owner = 0, tier = tier3>], ingress = []>
} {
  obelisk_sim.scope.decl 0
}

// -----

obelisk_sim.design @arbitrary_mixed_writer_owner attributes {
  compute_graph = #obelisk_sim.graph<
    version = 1, vpi = off, workers = 1,
    nodes = [
      #obelisk_sim.fragment<id = 0, function = @writer0, block = 0,
        region = active, action = terminate, tier = native, cost = 1,
        lane = 0, twoState = false, effects = [
          #obelisk_sim.effect<effect = write, resource = storage,
            target = descriptor, descriptor = 10, formal = 0, low = 0,
            width = 8, dynamic = false, deferred = false, trigger = none>]>,
      #obelisk_sim.fragment<id = 1, function = @writer1, block = 0,
        region = active, action = terminate, tier = native, cost = 1,
        lane = 0, twoState = false, effects = [
          #obelisk_sim.effect<effect = write, resource = storage,
            target = descriptor, descriptor = 10, formal = 0, low = 0,
            width = 8, dynamic = false, deferred = false, trigger = none>]>],
    edges = [], regions = [
      #obelisk_sim.region<kind = active, groups = [
        #obelisk_sim.group<fragments = [0], schedule = acyclic,
          feedback = []>,
        #obelisk_sim.group<fragments = [1], schedule = acyclic,
          feedback = []>]>,
      #obelisk_sim.region<kind = nba, groups = []>,
      #obelisk_sim.region<kind = observed, groups = []>,
      #obelisk_sim.region<kind = reactive, groups = []>,
      #obelisk_sim.region<kind = postponed, groups = []>]>,
  // Owner 2 is the canonical lowest-common barrier.  Owner 3 is unused and
  // must not be accepted merely because it is not a kernel owner.
  // expected-error @+1 {{scheduled-root owner or tier disagrees with writers}}
  obelisk_sim.three_tier_schedule = #obelisk_sim.three_tier_schedule<
    version = 1, sourceGraph = #obelisk_sim.graph<
      version = 1, vpi = off, workers = 1,
      nodes = [
        #obelisk_sim.fragment<id = 0, function = @writer0, block = 0,
          region = active, action = terminate, tier = native, cost = 1,
          lane = 0, twoState = false, effects = [
            #obelisk_sim.effect<effect = write, resource = storage,
              target = descriptor, descriptor = 10, formal = 0, low = 0,
              width = 8, dynamic = false, deferred = false, trigger = none>]>,
        #obelisk_sim.fragment<id = 1, function = @writer1, block = 0,
          region = active, action = terminate, tier = native, cost = 1,
          lane = 0, twoState = false, effects = [
            #obelisk_sim.effect<effect = write, resource = storage,
              target = descriptor, descriptor = 10, formal = 0, low = 0,
              width = 8, dynamic = false, deferred = false,
              trigger = none>]>],
      edges = [], regions = [
        #obelisk_sim.region<kind = active, groups = [
          #obelisk_sim.group<fragments = [0], schedule = acyclic,
            feedback = []>,
          #obelisk_sim.group<fragments = [1], schedule = acyclic,
            feedback = []>]>,
        #obelisk_sim.region<kind = nba, groups = []>,
        #obelisk_sim.region<kind = observed, groups = []>,
        #obelisk_sim.region<kind = reactive, groups = []>,
        #obelisk_sim.region<kind = postponed, groups = []>]>,
    ownerCount = 4, triggers = [], kernels = [
      #obelisk_sim.scheduled_kernel<id = 0, owner = 0, readyBit = 0,
        tier = tier1, region = active, schedule = acyclic, shared = true,
        loweringReady = true, twoStateEligible = false,
        promotionRoots = [], fragments = [0]>,
      #obelisk_sim.scheduled_kernel<id = 1, owner = 1, readyBit = 0,
        tier = tier2, region = active, schedule = acyclic, shared = true,
        loweringReady = true, twoStateEligible = false,
        promotionRoots = [], fragments = [1]>], roots = [
      #obelisk_sim.scheduled_root<resource = storage, descriptor = 10,
        low = 0, width = 8, owner = 3, tier = tier2>], ingress = []>
} {
  obelisk_sim.scope.decl 0
}
