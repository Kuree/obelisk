// RUN: obelisk-opt %s \
// RUN:   --pass-pipeline='builtin.module(simulation.design(obelisk-sim-plan-static-superstep))' \
// RUN:   | FileCheck %s

// Exercise only the static-superstep planner.  A conventional digital clock
// group with one periodic clock and one clock-sensitive state update must stay
// wholly eligible for Tier 1 and for the eventual two-state handover.
module {
  simulation.design @digital_clock_group attributes {
    compute_graph = #schedule.graph<
      version = 1, vpi = off, workers = 1,
      nodes = [
        #schedule.fragment<id = 0, function = @root, block = 0,
          region = active, action = terminate, tier = native, cost = 1,
          lane = 0, twoState = true, effects = []>,
        #schedule.fragment<id = 1, function = @clock, block = 0,
          region = active, action = suspend_delay, tier = native, cost = 1,
          lane = 0, twoState = true, effects = []>,
        #schedule.fragment<id = 2, function = @clock, block = 1,
          region = active, action = continue, tier = native, cost = 2,
          lane = 0, twoState = true, effects = [
            #schedule.effect<effect = read, resource = storage,
              target = descriptor, descriptor = 0, formal = 0, low = 0,
              width = 1, dynamic = false, deferred = false, trigger = none>,
            #schedule.effect<effect = write, resource = storage,
              target = descriptor, descriptor = 0, formal = 0, low = 0,
              width = 1, dynamic = false, deferred = false,
              trigger = none>]>,
        #schedule.fragment<id = 3, function = @update, block = 0,
          region = active, action = suspend_change, tier = native, cost = 1,
          lane = 0, twoState = true, effects = [
            #schedule.effect<effect = watch, resource = storage,
              target = descriptor, descriptor = 0, formal = 0, low = 0,
              width = 1, dynamic = false, deferred = false,
              trigger = change>]>,
        #schedule.fragment<id = 4, function = @update, block = 1,
          region = active, action = continue, tier = native, cost = 4,
          lane = 0, twoState = true, effects = [
            #schedule.effect<effect = read, resource = storage,
              target = descriptor, descriptor = 1, formal = 0, low = 0,
              width = 32, dynamic = false, deferred = false,
              trigger = none>,
            #schedule.effect<effect = write, resource = storage,
              target = descriptor, descriptor = 1, formal = 0, low = 0,
              width = 32, dynamic = false, deferred = false,
              trigger = none>]>],
      edges = [
        #schedule.edge<source = 0, target = 1, kind = spawn>,
        #schedule.edge<source = 0, target = 3, kind = spawn>,
        #schedule.edge<source = 1, target = 2, kind = resume>,
        #schedule.edge<source = 2, target = 1, kind = process_order>,
        #schedule.edge<source = 2, target = 3, kind = sensitivity,
          resource = <effect = watch, resource = storage,
            target = descriptor, descriptor = 0, formal = 0, low = 0,
            width = 1, dynamic = false, deferred = false,
            trigger = change>>,
        #schedule.edge<source = 3, target = 4, kind = resume>,
        #schedule.edge<source = 4, target = 3, kind = process_order>],
      regions = [
        #schedule.region<kind = active, groups = [
          #schedule.group<fragments = [0], schedule = acyclic,
            feedback = []>,
          #schedule.group<fragments = [1], schedule = acyclic,
            feedback = []>,
          #schedule.group<fragments = [2], schedule = acyclic,
            feedback = []>,
          #schedule.group<fragments = [3], schedule = acyclic,
            feedback = []>,
          #schedule.group<fragments = [4], schedule = acyclic,
            feedback = []>]>,
        #schedule.region<kind = nba, groups = []>,
        #schedule.region<kind = observed, groups = []>,
        #schedule.region<kind = reactive, groups = []>,
        #schedule.region<kind = postponed, groups = []>]>
  } {
    simulation.scope.decl 0
    simulation.code_unit.decl 1 in 0 root_initializer hierarchy "root"
    simulation.code_unit.decl 2 in 0 always hierarchy "clock"
    simulation.code_unit.decl 3 in 0 always hierarchy "update"
    simulation.storage.decl 0 in 0 : !simulation.logic<1> design
    simulation.storage.decl 1 in 0 : !simulation.logic<32> design

    simulation.func @root(
        %ctx: !simulation.context {simulation.capture_kind = 0 : i32})
        attributes {entry_kind = 0 : i32, code_unit_id = 1 : i64} {
      %clock_ref = simulation.context.storage %ctx[0] :
          !simulation.ref<!simulation.logic<1>>
      %state = simulation.context.storage %ctx[1] :
          !simulation.ref<!simulation.logic<32>>
      %clock_process = simulation.spawn @clock(%ctx, %clock_ref) :
          !simulation.context, !simulation.ref<!simulation.logic<1>>
          -> !simulation.process
      %update_process = simulation.spawn @update(
          %ctx, %clock_ref, %state) :
          !simulation.context, !simulation.ref<!simulation.logic<1>>,
          !simulation.ref<!simulation.logic<32>> -> !simulation.process
      simulation.return
    }
    simulation.func @clock(
        %ctx: !simulation.context {simulation.capture_kind = 0 : i32},
        %clock_ref: !simulation.ref<!simulation.logic<1>>
            {simulation.capture_kind = 3 : i32,
             simulation.descriptor_id = 0 : i64})
        attributes {entry_kind = 3 : i32, code_unit_id = 2 : i64} {
      cf.br ^wait
    ^wait:
      %delay = simulation.time.constant 1
      simulation.suspend.delay %delay to ^toggle
          {site = #schedule.continuation<id = 1>,
           timing = #schedule.timing_site<id = 0, kind = calendar>}
    ^toggle:
      %old = simulation.ref.load %clock_ref :
          !simulation.ref<!simulation.logic<1>> -> !simulation.logic<1>
      %new = simulation.logic.unary bit_not %old :
          (!simulation.logic<1>) -> !simulation.logic<1>
      simulation.ref.store %new to %clock_ref : !simulation.logic<1>,
          !simulation.ref<!simulation.logic<1>>
      cf.br ^wait
    }
    simulation.func @update(
        %ctx: !simulation.context {simulation.capture_kind = 0 : i32},
        %clock_ref: !simulation.ref<!simulation.logic<1>>
            {simulation.capture_kind = 3 : i32,
             simulation.descriptor_id = 0 : i64},
        %state: !simulation.ref<!simulation.logic<32>>
            {simulation.capture_kind = 3 : i32,
             simulation.descriptor_id = 1 : i64})
        attributes {entry_kind = 3 : i32, code_unit_id = 3 : i64} {
      cf.br ^wait
    ^wait:
      simulation.suspend.change %clock_ref to ^resume
          {site = #schedule.continuation<id = 2>} :
          !simulation.ref<!simulation.logic<1>>
    ^resume:
      %old = simulation.ref.load %state :
          !simulation.ref<!simulation.logic<32>> -> !simulation.logic<32>
      %new = simulation.logic.unary bit_not %old :
          (!simulation.logic<32>) -> !simulation.logic<32>
      simulation.ref.store %new to %state : !simulation.logic<32>,
          !simulation.ref<!simulation.logic<32>>
      cf.br ^wait
    }
  }
}

// CHECK: simulation.design @digital_clock_group attributes {
// CHECK-SAME: schedule.static_superstep = #schedule.static_superstep<version = 1
// CHECK-SAME: actors = [@root, @clock, @update]
