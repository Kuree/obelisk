// RUN: obelisk-opt %s --pass-pipeline='builtin.module(simulation.design(obelisk-sim-plan-static-superstep))' | FileCheck %s

// A statically indexed native convergence group is a clean-superstep
// candidate even when it contains multiple fragments. Net watches are exact
// descriptor sensitivities too; the later native fanout analysis remains the
// final proof that the complete schedule is eligible.

module {
  // CHECK: simulation.design @convergence attributes {
  // CHECK-SAME: schedule.static_superstep = #schedule.static_superstep<version = 1
  // CHECK-SAME: actors = [@root, @settle]
  simulation.design @convergence attributes {
    compute_graph = #schedule.graph<
      version = 1, vpi = off, workers = 1,
      nodes = [
        #schedule.fragment<id = 0, function = @root, block = 0,
          region = active, action = terminate, tier = native, cost = 1,
          lane = 0, twoState = true, effects = []>,
        #schedule.fragment<id = 1, function = @settle, block = 0,
          region = active, action = suspend_change, tier = native, cost = 1,
          lane = 0, twoState = false,
          effects = [#schedule.effect<effect = watch, resource = net,
            target = descriptor, descriptor = 0, formal = 0, low = 0,
            width = 1, dynamic = false, deferred = false,
            trigger = change>]>],
      edges = [],
      regions = [
        #schedule.region<kind = active, groups = [
          #schedule.group<fragments = [0, 1], schedule = convergence,
            feedback = []>]>,
        #schedule.region<kind = nba, groups = []>,
        #schedule.region<kind = observed, groups = []>,
        #schedule.region<kind = reactive, groups = []>,
        #schedule.region<kind = postponed, groups = []>]>
  } {
    simulation.code_unit.decl 1 in 0 root_initializer hierarchy "root"
    simulation.code_unit.decl 2 in 0 always hierarchy "settle"
    simulation.scope.decl 0

    simulation.func @root(
        %ctx: !simulation.context {simulation.capture_kind = 0 : i32})
        attributes {entry_kind = 0 : i32, code_unit_id = 1 : i64} {
      %process = simulation.spawn @settle(%ctx) :
          !simulation.context -> !simulation.process
      simulation.return
    }

    simulation.func @settle(
        %ctx: !simulation.context {simulation.capture_kind = 0 : i32})
        attributes {entry_kind = 3 : i32, code_unit_id = 2 : i64} {
      simulation.return
    }
  }
}
