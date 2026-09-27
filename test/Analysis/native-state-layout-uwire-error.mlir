// RUN: not obelisk-opt %s --test-obelisk-native-state-layout-analysis 2>&1 | FileCheck %s

module {
  simulation.design @design attributes {compute_graph = #schedule.graph<version = 1, vpi = off, workers = 1, nodes = [#schedule.fragment<id = 0, function = @__obelisk_root, block = 0, region = active, action = terminate, tier = native, cost = 5, lane = 0, twoState = true, effects = []>, #schedule.fragment<id = 1, function = @unit_0, block = 0, region = active, action = terminate, tier = native, cost = 4, lane = 0, twoState = true, effects = [#schedule.effect<effect = drive, resource = net, target = descriptor, descriptor = 0, formal = 0, low = 0, width = 1, dynamic = false, deferred = false, trigger = none>]>, #schedule.fragment<id = 2, function = @unit_1, block = 0, region = active, action = terminate, tier = native, cost = 4, lane = 0, twoState = true, effects = [#schedule.effect<effect = drive, resource = net, target = descriptor, descriptor = 0, formal = 0, low = 0, width = 1, dynamic = false, deferred = false, trigger = none>]>], edges = [#schedule.edge<source = 0, target = 1, kind = spawn>, #schedule.edge<source = 0, target = 2, kind = spawn>, #schedule.edge<source = 1, target = 2, kind = conflict, resource = <effect = drive, resource = net, target = descriptor, descriptor = 0, formal = 0, low = 0, width = 1, dynamic = false, deferred = false, trigger = none>>], regions = [#schedule.region<kind = active, groups = [#schedule.group<fragments = [1], schedule = acyclic, feedback = []>, #schedule.group<fragments = [2], schedule = acyclic, feedback = []>, #schedule.group<fragments = [0], schedule = acyclic, feedback = []>]>, #schedule.region<kind = nba, groups = []>, #schedule.region<kind = observed, groups = []>, #schedule.region<kind = reactive, groups = []>, #schedule.region<kind = postponed, groups = []>]>, time_precision_fs = 1000000 : i64} {
    simulation.scope.decl 0 hierarchy "\\$root " debug "$root" {dpi_precision_femtoseconds = 1000000 : i64, dpi_unit_femtoseconds = 1000000 : i64}
    simulation.scope.decl 1 parent 0 hierarchy "unsupported_overlapping_uwire" debug "unsupported_overlapping_uwire" {dpi_precision_femtoseconds = 1000000 : i64, dpi_unit_femtoseconds = 1000000 : i64}
    simulation.net.decl 0 in 1 : !simulation.logic<1> design hierarchy "unsupported_overlapping_uwire.value" debug "value" {observability = 0 : i32}
    simulation.net.decl 1 in 1 : !simulation.logic<1> design hierarchy "unsupported_overlapping_uwire.alias" debug "alias" {observability = 0 : i32, resolution_kind = 2 : i32}
    simulation.net.connect.decl 0 in 1 0[0] to 1[0] width 1 reversed = false rhs_dominates = true
    simulation.driver.decl 0 in 1 drives 0 : !simulation.logic<1> design hierarchy "unsupported_overlapping_uwire.value" debug "continuous" {driven_low = 0 : i64, driven_width = 1 : i64}
    simulation.driver.decl 1 in 1 drives 0 : !simulation.logic<1> design hierarchy "unsupported_overlapping_uwire.value" debug "continuous" {driven_low = 0 : i64, driven_width = 1 : i64}
    simulation.code_unit.decl 832639515527371617 in 0 root_initializer hierarchy "__obelisk_root" debug "root initializer"
    simulation.code_unit.decl 1984617976032943628 in 1 continuous hierarchy "unsupported_overlapping_uwire.$code_unit_6" debug ""
    simulation.code_unit.decl 1813757016372479160 in 1 continuous hierarchy "unsupported_overlapping_uwire.$code_unit_11" debug ""
    simulation.func @__obelisk_root(%arg0: !simulation.context {simulation.capture_kind = 0 : i32}) attributes {code_unit_id = 832639515527371617 : i64, domain = 0 : i32, effect_summary = [], entry_kind = 0 : i32, fragment_abi = #schedule.fragment_abi<version = 1, fragments = [0]>, home_region = 2 : i32} {
      %0 = simulation.context.net %arg0[0] : !simulation.net<!simulation.logic<1>>
      %1 = simulation.context.driver %arg0[0] : !simulation.driver<!simulation.logic<1>>
      %2 = simulation.spawn @unit_0(%arg0, %0, %1) : !simulation.context, !simulation.net<!simulation.logic<1>>, !simulation.driver<!simulation.logic<1>> -> !simulation.process
      %3 = simulation.context.driver %arg0[1] : !simulation.driver<!simulation.logic<1>>
      %4 = simulation.spawn @unit_1(%arg0, %0, %3) : !simulation.context, !simulation.net<!simulation.logic<1>>, !simulation.driver<!simulation.logic<1>> -> !simulation.process
      simulation.return
    }
    simulation.func private @unit_0(%arg0: !simulation.context {simulation.capture_kind = 0 : i32}, %arg1: !simulation.net<!simulation.logic<1>> {simulation.capture_kind = 4 : i32, simulation.descriptor_id = 0 : i64}, %arg2: !simulation.driver<!simulation.logic<1>> {simulation.capture_kind = 5 : i32, simulation.descriptor_id = 0 : i64}) attributes {code_unit_id = 1984617976032943628 : i64, domain = 0 : i32, effect_summary = [#schedule.effect<effect = drive, resource = net, target = descriptor, descriptor = 0, formal = 0, low = 0, width = 1, dynamic = false, deferred = false, trigger = none>], entry_kind = 7 : i32, fragment_abi = #schedule.fragment_abi<version = 1, fragments = [1]>, home_region = 2 : i32, simulation.hierarchical_name = "unsupported_overlapping_uwire"} {
      %0 = simulation.logic.constant false, false : !simulation.logic<1>
      simulation.driver.drive %arg2 = %0 : !simulation.driver<!simulation.logic<1>>, !simulation.logic<1>
      simulation.return
    }
    simulation.func private @unit_1(%arg0: !simulation.context {simulation.capture_kind = 0 : i32}, %arg1: !simulation.net<!simulation.logic<1>> {simulation.capture_kind = 4 : i32, simulation.descriptor_id = 0 : i64}, %arg2: !simulation.driver<!simulation.logic<1>> {simulation.capture_kind = 5 : i32, simulation.descriptor_id = 1 : i64}) attributes {code_unit_id = 1813757016372479160 : i64, domain = 0 : i32, effect_summary = [#schedule.effect<effect = drive, resource = net, target = descriptor, descriptor = 0, formal = 0, low = 0, width = 1, dynamic = false, deferred = false, trigger = none>], entry_kind = 7 : i32, fragment_abi = #schedule.fragment_abi<version = 1, fragments = [2]>, home_region = 2 : i32, simulation.hierarchical_name = "unsupported_overlapping_uwire"} {
      %0 = simulation.logic.constant true, false : !simulation.logic<1>
      simulation.driver.drive %arg2 = %0 : !simulation.driver<!simulation.logic<1>>, !simulation.logic<1>
      simulation.return
    }
  }
}

// CHECK: uwire connectivity component 0[0] has more than one driver
