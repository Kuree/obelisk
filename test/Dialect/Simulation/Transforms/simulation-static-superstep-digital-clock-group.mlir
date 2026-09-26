// RUN: obelisk-opt %s \
// RUN:   --pass-pipeline='builtin.module(obelisk_sim.design(obelisk-sim-plan-static-superstep))' \
// RUN:   | FileCheck %s

// Exercise only the static-superstep planner.  A conventional digital clock
// group with one periodic clock and one clock-sensitive state update must stay
// wholly eligible for Tier 1 and for the eventual two-state handover.
module {
  obelisk_sim.design @digital_clock_group attributes {
    compute_graph = #obelisk_sim.graph<
      version = 1, vpi = off, workers = 1,
      nodes = [
        #obelisk_sim.fragment<id = 0, function = @root, block = 0,
          region = active, action = terminate, tier = native, cost = 1,
          lane = 0, twoState = true, effects = []>,
        #obelisk_sim.fragment<id = 1, function = @clock, block = 0,
          region = active, action = suspend_delay, tier = native, cost = 1,
          lane = 0, twoState = true, effects = []>,
        #obelisk_sim.fragment<id = 2, function = @clock, block = 1,
          region = active, action = continue, tier = native, cost = 2,
          lane = 0, twoState = true, effects = [
            #obelisk_sim.effect<effect = read, resource = storage,
              target = descriptor, descriptor = 0, formal = 0, low = 0,
              width = 1, dynamic = false, deferred = false, trigger = none>,
            #obelisk_sim.effect<effect = write, resource = storage,
              target = descriptor, descriptor = 0, formal = 0, low = 0,
              width = 1, dynamic = false, deferred = false,
              trigger = none>]>,
        #obelisk_sim.fragment<id = 3, function = @update, block = 0,
          region = active, action = suspend_change, tier = native, cost = 1,
          lane = 0, twoState = true, effects = [
            #obelisk_sim.effect<effect = watch, resource = storage,
              target = descriptor, descriptor = 0, formal = 0, low = 0,
              width = 1, dynamic = false, deferred = false,
              trigger = change>]>,
        #obelisk_sim.fragment<id = 4, function = @update, block = 1,
          region = active, action = continue, tier = native, cost = 4,
          lane = 0, twoState = true, effects = [
            #obelisk_sim.effect<effect = read, resource = storage,
              target = descriptor, descriptor = 1, formal = 0, low = 0,
              width = 32, dynamic = false, deferred = false,
              trigger = none>,
            #obelisk_sim.effect<effect = write, resource = storage,
              target = descriptor, descriptor = 1, formal = 0, low = 0,
              width = 32, dynamic = false, deferred = false,
              trigger = none>]>],
      edges = [
        #obelisk_sim.edge<source = 0, target = 1, kind = spawn>,
        #obelisk_sim.edge<source = 0, target = 3, kind = spawn>,
        #obelisk_sim.edge<source = 1, target = 2, kind = resume>,
        #obelisk_sim.edge<source = 2, target = 1, kind = process_order>,
        #obelisk_sim.edge<source = 2, target = 3, kind = sensitivity,
          resource = <effect = watch, resource = storage,
            target = descriptor, descriptor = 0, formal = 0, low = 0,
            width = 1, dynamic = false, deferred = false,
            trigger = change>>,
        #obelisk_sim.edge<source = 3, target = 4, kind = resume>,
        #obelisk_sim.edge<source = 4, target = 3, kind = process_order>],
      regions = [
        #obelisk_sim.region<kind = active, groups = [
          #obelisk_sim.group<fragments = [0], schedule = acyclic,
            feedback = []>,
          #obelisk_sim.group<fragments = [1], schedule = acyclic,
            feedback = []>,
          #obelisk_sim.group<fragments = [2], schedule = acyclic,
            feedback = []>,
          #obelisk_sim.group<fragments = [3], schedule = acyclic,
            feedback = []>,
          #obelisk_sim.group<fragments = [4], schedule = acyclic,
            feedback = []>]>,
        #obelisk_sim.region<kind = nba, groups = []>,
        #obelisk_sim.region<kind = observed, groups = []>,
        #obelisk_sim.region<kind = reactive, groups = []>,
        #obelisk_sim.region<kind = postponed, groups = []>]>
  } {
    obelisk_sim.scope.decl 0
    obelisk_sim.code_unit.decl 1 in 0 root_initializer hierarchy "root"
    obelisk_sim.code_unit.decl 2 in 0 always hierarchy "clock"
    obelisk_sim.code_unit.decl 3 in 0 always hierarchy "update"
    obelisk_sim.storage.decl 0 in 0 : !obelisk_sim.logic<1> design
    obelisk_sim.storage.decl 1 in 0 : !obelisk_sim.logic<32> design

    obelisk_sim.func @root(
        %ctx: !obelisk_sim.context {obelisk_sim.capture_kind = 0 : i32})
        attributes {entry_kind = 0 : i32, code_unit_id = 1 : i64} {
      %clock_ref = obelisk_sim.context.storage %ctx[0] :
          !obelisk_sim.ref<!obelisk_sim.logic<1>>
      %state = obelisk_sim.context.storage %ctx[1] :
          !obelisk_sim.ref<!obelisk_sim.logic<32>>
      %clock_process = obelisk_sim.spawn @clock(%ctx, %clock_ref) :
          !obelisk_sim.context, !obelisk_sim.ref<!obelisk_sim.logic<1>>
          -> !obelisk_sim.process
      %update_process = obelisk_sim.spawn @update(
          %ctx, %clock_ref, %state) :
          !obelisk_sim.context, !obelisk_sim.ref<!obelisk_sim.logic<1>>,
          !obelisk_sim.ref<!obelisk_sim.logic<32>> -> !obelisk_sim.process
      obelisk_sim.return
    }
    obelisk_sim.func @clock(
        %ctx: !obelisk_sim.context {obelisk_sim.capture_kind = 0 : i32},
        %clock_ref: !obelisk_sim.ref<!obelisk_sim.logic<1>>
            {obelisk_sim.capture_kind = 3 : i32,
             obelisk_sim.descriptor_id = 0 : i64})
        attributes {entry_kind = 3 : i32, code_unit_id = 2 : i64} {
      cf.br ^wait
    ^wait:
      %delay = obelisk_sim.time.constant 1
      obelisk_sim.suspend.delay %delay to ^toggle
          {site = #obelisk_sim.continuation<id = 1>,
           timing = #obelisk_sim.timing_site<id = 0, kind = calendar>}
    ^toggle:
      %old = obelisk_sim.ref.load %clock_ref :
          !obelisk_sim.ref<!obelisk_sim.logic<1>> -> !obelisk_sim.logic<1>
      %new = obelisk_sim.logic.unary bit_not %old :
          (!obelisk_sim.logic<1>) -> !obelisk_sim.logic<1>
      obelisk_sim.ref.store %new to %clock_ref : !obelisk_sim.logic<1>,
          !obelisk_sim.ref<!obelisk_sim.logic<1>>
      cf.br ^wait
    }
    obelisk_sim.func @update(
        %ctx: !obelisk_sim.context {obelisk_sim.capture_kind = 0 : i32},
        %clock_ref: !obelisk_sim.ref<!obelisk_sim.logic<1>>
            {obelisk_sim.capture_kind = 3 : i32,
             obelisk_sim.descriptor_id = 0 : i64},
        %state: !obelisk_sim.ref<!obelisk_sim.logic<32>>
            {obelisk_sim.capture_kind = 3 : i32,
             obelisk_sim.descriptor_id = 1 : i64})
        attributes {entry_kind = 3 : i32, code_unit_id = 3 : i64} {
      cf.br ^wait
    ^wait:
      obelisk_sim.suspend.change %clock_ref to ^resume
          {site = #obelisk_sim.continuation<id = 2>} :
          !obelisk_sim.ref<!obelisk_sim.logic<1>>
    ^resume:
      %old = obelisk_sim.ref.load %state :
          !obelisk_sim.ref<!obelisk_sim.logic<32>> -> !obelisk_sim.logic<32>
      %new = obelisk_sim.logic.unary bit_not %old :
          (!obelisk_sim.logic<32>) -> !obelisk_sim.logic<32>
      obelisk_sim.ref.store %new to %state : !obelisk_sim.logic<32>,
          !obelisk_sim.ref<!obelisk_sim.logic<32>>
      cf.br ^wait
    }
  }
}

// CHECK: obelisk_sim.design @digital_clock_group attributes {
// CHECK-SAME: obelisk_sim.static_superstep = #obelisk_sim.static_superstep<version = 1
// CHECK-SAME: actors = [@root, @clock, @update]
