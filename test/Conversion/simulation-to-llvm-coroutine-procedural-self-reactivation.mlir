// RUN: obelisk-opt %s \
// RUN:   --pass-pipeline='builtin.module(simulation.design(obelisk-sim-materialize-graph-regions,obelisk-sim-materialize-compute-fusion,obelisk-sim-specialize-static-state-nba,obelisk-sim-plan-static-superstep),convert-obelisk-sim-processes-to-llvm-coroutines)' \
// RUN:   | FileCheck %s

// A procedural event loop is intentionally not a graph-level settling SCC.
// Its direct executor must still preserve a transition that reactivates its
// own earlier wait: clear the consumed ingress bit before calling the body.
module attributes {
  llvm.data_layout = "e-m:e-p:64:64-i64:64-n8:16:32:64-S128",
  llvm.target_triple = "x86_64-unknown-linux-gnu",
  schedule.native_scheduler = 3 : i32
} {
  simulation.design @procedural_self_reactivation attributes {
    compute_graph = #schedule.graph<
      version = 1, vpi = off, workers = 1,
      nodes = [
        #schedule.fragment<id = 0, function = @loop, block = 0,
          region = active, action = continue, tier = native, cost = 1,
          lane = 0, twoState = true, effects = []>,
        #schedule.fragment<id = 1, function = @loop, block = 1,
          region = active, action = suspend_change, tier = native, cost = 1,
          lane = 0, twoState = true, effects = [
            #schedule.effect<effect = watch, resource = storage,
              target = descriptor, descriptor = 0, formal = 0, low = 0,
              width = 1, dynamic = false, deferred = false,
              trigger = change>]>,
        #schedule.fragment<id = 2, function = @loop, block = 2,
          region = active, action = continue, tier = native, cost = 2,
          lane = 0, twoState = true, effects = [
            #schedule.effect<effect = read, resource = storage,
              target = descriptor, descriptor = 0, formal = 0, low = 0,
              width = 1, dynamic = false, deferred = false, trigger = none>,
            #schedule.effect<effect = write, resource = storage,
              target = descriptor, descriptor = 0, formal = 0, low = 0,
              width = 1, dynamic = false, deferred = false, trigger = none>]>,
        #schedule.fragment<id = 3, function = @root, block = 0,
          region = active, action = terminate, tier = native, cost = 1,
          lane = 0, twoState = true, effects = []>,
        #schedule.fragment<id = 4, function = @clock, block = 0,
          region = active, action = continue, tier = native, cost = 1,
          lane = 0, twoState = true, effects = []>,
        #schedule.fragment<id = 5, function = @clock, block = 1,
          region = active, action = suspend_delay, tier = native, cost = 1,
          lane = 0, twoState = true, effects = []>,
        #schedule.fragment<id = 6, function = @clock, block = 2,
          region = active, action = continue, tier = native, cost = 2,
          lane = 0, twoState = true, effects = [
            #schedule.effect<effect = read, resource = storage,
              target = descriptor, descriptor = 0, formal = 0, low = 0,
              width = 1, dynamic = false, deferred = false, trigger = none>,
            #schedule.effect<effect = write, resource = storage,
              target = descriptor, descriptor = 0, formal = 0, low = 0,
              width = 1, dynamic = false, deferred = false, trigger = none>]>],
      edges = [
        #schedule.edge<source = 0, target = 1, kind = process_order>,
        #schedule.edge<source = 1, target = 2, kind = resume>,
        #schedule.edge<source = 2, target = 1, kind = process_order>,
        #schedule.edge<source = 2, target = 1, kind = sensitivity,
          resource = <effect = watch, resource = storage,
            target = descriptor, descriptor = 0, formal = 0, low = 0,
            width = 1, dynamic = false, deferred = false, trigger = change>>,
        #schedule.edge<source = 3, target = 0, kind = spawn>,
        #schedule.edge<source = 3, target = 4, kind = spawn>,
        #schedule.edge<source = 4, target = 5, kind = process_order>,
        #schedule.edge<source = 5, target = 6, kind = resume>,
        #schedule.edge<source = 6, target = 1, kind = sensitivity,
          resource = <effect = watch, resource = storage,
            target = descriptor, descriptor = 0, formal = 0, low = 0,
            width = 1, dynamic = false, deferred = false, trigger = change>>,
        #schedule.edge<source = 6, target = 5, kind = process_order>],
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
            feedback = []>,
          #schedule.group<fragments = [5], schedule = acyclic,
            feedback = []>,
          #schedule.group<fragments = [6], schedule = acyclic,
            feedback = []>]>,
        #schedule.region<kind = nba, groups = []>,
        #schedule.region<kind = observed, groups = []>,
        #schedule.region<kind = reactive, groups = []>,
        #schedule.region<kind = postponed, groups = []>]>
  } {
    simulation.scope.decl 0
    simulation.code_unit.decl 1 in 0 root_initializer hierarchy "self.root"
    simulation.code_unit.decl 2 in 0 always hierarchy "self.loop"
    simulation.code_unit.decl 3 in 0 always hierarchy "self.clock"
    simulation.storage.decl 0 in 0 : !simulation.logic<1> design

    simulation.func @root(
        %ctx: !simulation.context {simulation.capture_kind = 0 : i32})
        attributes {entry_kind = 0 : i32, code_unit_id = 1 : i64} {
      %state = simulation.context.storage %ctx[0] :
          !simulation.ref<!simulation.logic<1>>
      %loop = simulation.spawn @loop(%ctx, %state) :
          !simulation.context, !simulation.ref<!simulation.logic<1>>
          -> !simulation.process
      %clock = simulation.spawn @clock(%ctx, %state) :
          !simulation.context, !simulation.ref<!simulation.logic<1>>
          -> !simulation.process
      simulation.return
    }

    simulation.func @loop(
        %ctx: !simulation.context {simulation.capture_kind = 0 : i32},
        %state: !simulation.ref<!simulation.logic<1>>
            {simulation.capture_kind = 3 : i32,
             simulation.descriptor_id = 0 : i64})
        attributes {entry_kind = 3 : i32, code_unit_id = 2 : i64,
                    schedule.native.region_body} {
      cf.br ^wait
    ^wait:
      simulation.suspend.change %state to ^resume
          {site = #schedule.continuation<id = 1>} :
          !simulation.ref<!simulation.logic<1>>
    ^resume:
      %value = simulation.ref.load %state :
          !simulation.ref<!simulation.logic<1>> -> !simulation.logic<1>
      %next = simulation.logic.unary bit_not %value :
          (!simulation.logic<1>) -> !simulation.logic<1>
      simulation.ref.store %next to %state : !simulation.logic<1>,
          !simulation.ref<!simulation.logic<1>>
      cf.br ^wait
    }

    simulation.func @clock(
        %ctx: !simulation.context {simulation.capture_kind = 0 : i32},
        %state: !simulation.ref<!simulation.logic<1>>
            {simulation.capture_kind = 3 : i32,
             simulation.descriptor_id = 0 : i64})
        attributes {entry_kind = 3 : i32, code_unit_id = 3 : i64} {
      cf.br ^wait
    ^wait:
      %delay = simulation.time.constant 1
      simulation.suspend.delay %delay to ^toggle
          {site = #schedule.continuation<id = 2>,
           timing = #schedule.timing_site<id = 0, kind = calendar>}
    ^toggle:
      %old = simulation.ref.load %state :
          !simulation.ref<!simulation.logic<1>> -> !simulation.logic<1>
      %new = simulation.logic.unary bit_not %old :
          (!simulation.logic<1>) -> !simulation.logic<1>
      simulation.ref.store %new to %state : !simulation.logic<1>,
          !simulation.ref<!simulation.logic<1>>
      cf.br ^wait
    }
  }
}

// CHECK-LABEL: llvm.func @__obelisk_direct_fragment_{{[0-9]+}}_1.__obelisk_execute(
// CHECK-SAME: schedule.eval.tier2_convergence
// CHECK-LABEL: llvm.func @__obelisk_eval_dispatch_v1
// CHECK: llvm.switch
// CHECK: %[[INGRESS:.*]] = llvm.mlir.addressof @__obelisk_aot_model_ingress_v1
// CHECK: %[[QUEUED:.*]] = llvm.load %[[INGRESS]]
// CHECK: %[[CLEARED:.*]] = llvm.and %[[QUEUED]],
// CHECK: llvm.store %[[CLEARED]], %[[INGRESS]]
// CHECK: llvm.call @__obelisk_direct_fragment_{{[0-9]+}}_1.__obelisk_execute
