// RUN: not obelisk-opt %s --test-obelisk-native-state-layout-analysis 2>&1 | FileCheck %s

module {
  obelisk_sim.design @design attributes {compute_graph = #obelisk_sim.graph<version = 1, vpi = off, workers = 1, nodes = [#obelisk_sim.fragment<id = 0, function = @__obelisk_root, block = 0, region = active, action = terminate, tier = native, cost = 5, lane = 0, twoState = true, effects = []>, #obelisk_sim.fragment<id = 1, function = @unit_0, block = 0, region = active, action = terminate, tier = native, cost = 4, lane = 0, twoState = true, effects = [#obelisk_sim.effect<effect = drive, resource = net, target = descriptor, descriptor = 0, formal = 0, low = 0, width = 1, dynamic = false, deferred = false, trigger = none>]>, #obelisk_sim.fragment<id = 2, function = @unit_1, block = 0, region = active, action = terminate, tier = native, cost = 4, lane = 0, twoState = true, effects = [#obelisk_sim.effect<effect = drive, resource = net, target = descriptor, descriptor = 0, formal = 0, low = 0, width = 1, dynamic = false, deferred = false, trigger = none>]>], edges = [#obelisk_sim.edge<source = 0, target = 1, kind = spawn>, #obelisk_sim.edge<source = 0, target = 2, kind = spawn>, #obelisk_sim.edge<source = 1, target = 2, kind = conflict, resource = <effect = drive, resource = net, target = descriptor, descriptor = 0, formal = 0, low = 0, width = 1, dynamic = false, deferred = false, trigger = none>>], regions = [#obelisk_sim.region<kind = active, groups = [#obelisk_sim.group<fragments = [1], schedule = acyclic, feedback = []>, #obelisk_sim.group<fragments = [2], schedule = acyclic, feedback = []>, #obelisk_sim.group<fragments = [0], schedule = acyclic, feedback = []>]>, #obelisk_sim.region<kind = nba, groups = []>, #obelisk_sim.region<kind = observed, groups = []>, #obelisk_sim.region<kind = reactive, groups = []>, #obelisk_sim.region<kind = postponed, groups = []>]>, time_precision_fs = 1000000 : i64} {
    obelisk_sim.scope.decl 0 hierarchy "\\$root " debug "$root" {dpi_precision_femtoseconds = 1000000 : i64, dpi_unit_femtoseconds = 1000000 : i64}
    obelisk_sim.scope.decl 1 parent 0 hierarchy "unsupported_overlapping_uwire" debug "unsupported_overlapping_uwire" {dpi_precision_femtoseconds = 1000000 : i64, dpi_unit_femtoseconds = 1000000 : i64}
    obelisk_sim.net.decl 0 in 1 : !obelisk_sim.logic<1> design hierarchy "unsupported_overlapping_uwire.value" debug "value" {observability = 0 : i32, resolution_kind = 2 : i32}
    obelisk_sim.driver.decl 0 in 1 drives 0 : !obelisk_sim.logic<1> design hierarchy "unsupported_overlapping_uwire.value" debug "continuous" {driven_low = 0 : i64, driven_width = 1 : i64}
    obelisk_sim.driver.decl 1 in 1 drives 0 : !obelisk_sim.logic<1> design hierarchy "unsupported_overlapping_uwire.value" debug "continuous" {driven_low = 0 : i64, driven_width = 1 : i64}
    obelisk_sim.code_unit.decl 832639515527371617 in 0 root_initializer hierarchy "__obelisk_root" debug "root initializer"
    obelisk_sim.code_unit.decl 1984617976032943628 in 1 continuous hierarchy "unsupported_overlapping_uwire.$code_unit_6" debug ""
    obelisk_sim.code_unit.decl 1813757016372479160 in 1 continuous hierarchy "unsupported_overlapping_uwire.$code_unit_11" debug ""
    obelisk_sim.func @__obelisk_root(%arg0: !obelisk_sim.context {obelisk_sim.capture_kind = 0 : i32}) attributes {code_unit_id = 832639515527371617 : i64, domain = 0 : i32, effect_summary = [], entry_kind = 0 : i32, fragment_abi = #obelisk_sim.fragment_abi<version = 1, fragments = [0]>, home_region = 2 : i32} {
      %0 = obelisk_sim.context.net %arg0[0] : !obelisk_sim.net<!obelisk_sim.logic<1>>
      %1 = obelisk_sim.context.driver %arg0[0] : !obelisk_sim.driver<!obelisk_sim.logic<1>>
      %2 = obelisk_sim.spawn @unit_0(%arg0, %0, %1) : !obelisk_sim.context, !obelisk_sim.net<!obelisk_sim.logic<1>>, !obelisk_sim.driver<!obelisk_sim.logic<1>> -> !obelisk_sim.process
      %3 = obelisk_sim.context.driver %arg0[1] : !obelisk_sim.driver<!obelisk_sim.logic<1>>
      %4 = obelisk_sim.spawn @unit_1(%arg0, %0, %3) : !obelisk_sim.context, !obelisk_sim.net<!obelisk_sim.logic<1>>, !obelisk_sim.driver<!obelisk_sim.logic<1>> -> !obelisk_sim.process
      obelisk_sim.return
    }
    obelisk_sim.func private @unit_0(%arg0: !obelisk_sim.context {obelisk_sim.capture_kind = 0 : i32}, %arg1: !obelisk_sim.net<!obelisk_sim.logic<1>> {obelisk_sim.capture_kind = 4 : i32, obelisk_sim.descriptor_id = 0 : i64}, %arg2: !obelisk_sim.driver<!obelisk_sim.logic<1>> {obelisk_sim.capture_kind = 5 : i32, obelisk_sim.descriptor_id = 0 : i64}) attributes {code_unit_id = 1984617976032943628 : i64, domain = 0 : i32, effect_summary = [#obelisk_sim.effect<effect = drive, resource = net, target = descriptor, descriptor = 0, formal = 0, low = 0, width = 1, dynamic = false, deferred = false, trigger = none>], entry_kind = 7 : i32, fragment_abi = #obelisk_sim.fragment_abi<version = 1, fragments = [1]>, home_region = 2 : i32, obelisk_sim.hierarchical_name = "unsupported_overlapping_uwire"} {
      %0 = obelisk_sim.logic.constant false, false : !obelisk_sim.logic<1>
      obelisk_sim.driver.drive %arg2 = %0 : !obelisk_sim.driver<!obelisk_sim.logic<1>>, !obelisk_sim.logic<1>
      obelisk_sim.return
    }
    obelisk_sim.func private @unit_1(%arg0: !obelisk_sim.context {obelisk_sim.capture_kind = 0 : i32}, %arg1: !obelisk_sim.net<!obelisk_sim.logic<1>> {obelisk_sim.capture_kind = 4 : i32, obelisk_sim.descriptor_id = 0 : i64}, %arg2: !obelisk_sim.driver<!obelisk_sim.logic<1>> {obelisk_sim.capture_kind = 5 : i32, obelisk_sim.descriptor_id = 1 : i64}) attributes {code_unit_id = 1813757016372479160 : i64, domain = 0 : i32, effect_summary = [#obelisk_sim.effect<effect = drive, resource = net, target = descriptor, descriptor = 0, formal = 0, low = 0, width = 1, dynamic = false, deferred = false, trigger = none>], entry_kind = 7 : i32, fragment_abi = #obelisk_sim.fragment_abi<version = 1, fragments = [2]>, home_region = 2 : i32, obelisk_sim.hierarchical_name = "unsupported_overlapping_uwire"} {
      %0 = obelisk_sim.logic.constant true, false : !obelisk_sim.logic<1>
      obelisk_sim.driver.drive %arg2 = %0 : !obelisk_sim.driver<!obelisk_sim.logic<1>>, !obelisk_sim.logic<1>
      obelisk_sim.return
    }
  }
}

// CHECK: uwire connectivity component 0[0] has more than one driver
