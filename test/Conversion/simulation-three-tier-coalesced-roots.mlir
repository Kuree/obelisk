// RUN: obelisk-opt %s --obelisk-sim-materialize-graph-regions -o %t
// RUN: FileCheck %s < %t
// RUN: obelisk-opt %t | diff %t -
// RUN: obelisk-opt %t \
// RUN:   '--test-obelisk-edit-attributes=replace-from="#schedule.scheduled_root<resource = storage, descriptor = 9, low = 0, width = 16, owner = 0, tier = tier1>" replace-to="#schedule.scheduled_root<resource = storage, descriptor = 9, low = 0, width = 17, owner = 0, tier = tier1>"' \
// RUN:   | not obelisk-opt -o /dev/null 2>&1 | FileCheck %s --check-prefix=EXTRA
// RUN: obelisk-opt %t \
// RUN:   '--test-obelisk-edit-attributes=replace-from="#schedule.effect<effect = write, resource = storage, target = descriptor, descriptor = 9, formal = 0, low = 4, width = 8, dynamic = false, deferred = false, trigger = none>" replace-to="#schedule.effect<effect = write, resource = storage, target = descriptor, descriptor = 9, formal = 0, low = 4, width = 4, dynamic = false, deferred = false, trigger = none>"' \
// RUN:   '--test-obelisk-edit-attributes=replace-from="#schedule.effect<effect = write, resource = storage, target = descriptor, descriptor = 9, formal = 0, low = 8, width = 8, dynamic = false, deferred = false, trigger = none>" replace-to="#schedule.effect<effect = write, resource = storage, target = descriptor, descriptor = 9, formal = 0, low = 9, width = 7, dynamic = false, deferred = false, trigger = none>"' \
// RUN:   | not obelisk-opt -o /dev/null 2>&1 | FileCheck %s --check-prefix=EXTRA
// RUN: obelisk-opt %t \
// RUN:   '--test-obelisk-edit-attributes=replace-from="#schedule.scheduled_root<resource = storage, descriptor = 9, low = 0, width = 16, owner = 0, tier = tier1>" replace-to="#schedule.scheduled_root<resource = storage, descriptor = 9, low = 0, width = 15, owner = 0, tier = tier1>"' \
// RUN:   | not obelisk-opt -o /dev/null 2>&1 | FileCheck %s --check-prefix=MISSING
// RUN: obelisk-opt %t \
// RUN:   '--test-obelisk-edit-attributes=replace-from="#schedule.scheduled_root<resource = storage, descriptor = 9, low = 0, width = 16, owner = 0, tier = tier1>" replace-to="#schedule.scheduled_root<resource = storage, descriptor = 9, low = 0, width = 16, owner = 0, tier = tier3>"' \
// RUN:   | not obelisk-opt -o /dev/null 2>&1 | FileCheck %s --check-prefix=TIER
// RUN: obelisk-opt %t \
// RUN:   '--test-obelisk-edit-attributes=replace-from="[#schedule.scheduled_root<resource = storage, descriptor = 9, low = 0, width = 16, owner = 0, tier = tier1>]" replace-to="[#schedule.scheduled_root<resource = storage, descriptor = 9, low = 0, width = 16, owner = 0, tier = tier1>, #schedule.scheduled_root<resource = storage, descriptor = 9, low = 8, width = 8, owner = 0, tier = tier1>]"' \
// RUN:   | not obelisk-opt -o /dev/null 2>&1 | FileCheck %s --check-prefix=OVERLAP

// Adjacent partial writes have one owner after kernel formation. The compact
// root covers both writes, although neither write covers the whole root.
// Verify the serialized plan as well as the unchecked attribute construction.
// CHECK: roots = [#schedule.scheduled_root<resource = storage, descriptor = 9, low = 0, width = 16, owner = 0, tier = tier1>]
// EXTRA: error: scheduled root crosses or lacks an exact writer partition
// MISSING: error: scheduled roots do not cover every writer range
// TIER: error: scheduled-root owner or tier disagrees with writers
// OVERLAP: error: scheduled-root ranges overlap
module {
  simulation.design @adjacent attributes {
    compute_graph = #schedule.graph<version = 1, vpi = off, workers = 1,
      nodes = [
        #schedule.fragment<id = 0, function = @low, block = 0,
          region = active, action = terminate, tier = native, cost = 1,
          lane = 0, twoState = true, effects = [
            #schedule.effect<effect = write, resource = storage,
              target = descriptor, descriptor = 9, formal = 0, low = 0,
              width = 8, dynamic = false, deferred = false, trigger = none>,
            // Repeated and overlapping effects from one kernel need counts:
            // ending the first write must not remove its remaining writers.
            #schedule.effect<effect = write, resource = storage,
              target = descriptor, descriptor = 9, formal = 0, low = 4,
              width = 8, dynamic = false, deferred = false, trigger = none>]>,
        #schedule.fragment<id = 1, function = @high, block = 0,
          region = active, action = terminate, tier = native, cost = 1,
          lane = 0, twoState = true, effects = [
            #schedule.effect<effect = write, resource = storage,
              target = descriptor, descriptor = 9, formal = 0, low = 8,
              width = 8, dynamic = false, deferred = false, trigger = none>]>],
      edges = [], regions = [
        #schedule.region<kind = active, groups = [
          #schedule.group<fragments = [0], schedule = acyclic, feedback = []>,
          #schedule.group<fragments = [1], schedule = acyclic, feedback = []>]>,
        #schedule.region<kind = nba, groups = []>,
        #schedule.region<kind = observed, groups = []>,
        #schedule.region<kind = reactive, groups = []>,
        #schedule.region<kind = postponed, groups = []>]>
  } {
    simulation.scope.decl 0
  }
}
