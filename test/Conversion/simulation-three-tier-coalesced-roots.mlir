// RUN: obelisk-opt %s --obelisk-sim-materialize-graph-regions -o %t
// RUN: FileCheck %s < %t
// RUN: obelisk-opt %t -o %t.roundtrip
// RUN: diff %t %t.roundtrip
// RUN: sed 's/width = 16, owner = 0/width = 17, owner = 0/' %t > %t.extra
// RUN: not obelisk-opt %t.extra -o /dev/null 2>&1 | FileCheck %s --check-prefix=EXTRA
// RUN: sed 's/low = 4, width = 8, dynamic/low = 4, width = 4, dynamic/g; s/low = 8, width = 8, dynamic/low = 9, width = 7, dynamic/g' %t > %t.gap
// RUN: not obelisk-opt %t.gap -o /dev/null 2>&1 | FileCheck %s --check-prefix=EXTRA
// RUN: sed 's/width = 16, owner = 0/width = 15, owner = 0/' %t > %t.missing
// RUN: not obelisk-opt %t.missing -o /dev/null 2>&1 | FileCheck %s --check-prefix=MISSING
// RUN: sed 's/width = 16, owner = 0, tier = tier1/width = 16, owner = 0, tier = tier3/' %t > %t.tier
// RUN: not obelisk-opt %t.tier -o /dev/null 2>&1 | FileCheck %s --check-prefix=TIER
// RUN: sed 's/width = 16, owner = 0, tier = tier1>/width = 16, owner = 0, tier = tier1>, #obelisk_sim.scheduled_root<resource = storage, descriptor = 9, low = 8, width = 8, owner = 0, tier = tier1>/' %t > %t.overlap
// RUN: not obelisk-opt %t.overlap -o /dev/null 2>&1 | FileCheck %s --check-prefix=OVERLAP

// Adjacent partial writes have one owner after kernel formation. The compact
// root covers both writes, although neither write covers the whole root.
// Verify the serialized plan as well as the unchecked attribute construction.
// CHECK: roots = [#obelisk_sim.scheduled_root<resource = storage, descriptor = 9, low = 0, width = 16, owner = 0, tier = tier1>]
// EXTRA: error: scheduled root crosses or lacks an exact writer partition
// MISSING: error: scheduled roots do not cover every writer range
// TIER: error: scheduled-root owner or tier disagrees with writers
// OVERLAP: error: scheduled-root ranges overlap
module {
  obelisk_sim.design @adjacent attributes {
    compute_graph = #obelisk_sim.graph<version = 1, vpi = off, workers = 1,
      nodes = [
        #obelisk_sim.fragment<id = 0, function = @low, block = 0,
          region = active, action = terminate, tier = native, cost = 1,
          lane = 0, twoState = true, effects = [
            #obelisk_sim.effect<effect = write, resource = storage,
              target = descriptor, descriptor = 9, formal = 0, low = 0,
              width = 8, dynamic = false, deferred = false, trigger = none>,
            // Repeated and overlapping effects from one kernel need counts:
            // ending the first write must not remove its remaining writers.
            #obelisk_sim.effect<effect = write, resource = storage,
              target = descriptor, descriptor = 9, formal = 0, low = 4,
              width = 8, dynamic = false, deferred = false, trigger = none>]>,
        #obelisk_sim.fragment<id = 1, function = @high, block = 0,
          region = active, action = terminate, tier = native, cost = 1,
          lane = 0, twoState = true, effects = [
            #obelisk_sim.effect<effect = write, resource = storage,
              target = descriptor, descriptor = 9, formal = 0, low = 8,
              width = 8, dynamic = false, deferred = false, trigger = none>]>],
      edges = [], regions = [
        #obelisk_sim.region<kind = active, groups = [
          #obelisk_sim.group<fragments = [0], schedule = acyclic, feedback = []>,
          #obelisk_sim.group<fragments = [1], schedule = acyclic, feedback = []>]>,
        #obelisk_sim.region<kind = nba, groups = []>,
        #obelisk_sim.region<kind = observed, groups = []>,
        #obelisk_sim.region<kind = reactive, groups = []>,
        #obelisk_sim.region<kind = postponed, groups = []>]>
  } {
    obelisk_sim.scope.decl 0
  }
}
