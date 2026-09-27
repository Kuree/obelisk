// RUN: obelisk-opt %s \
// RUN:   --pass-pipeline='builtin.module(obelisk_sim.design(obelisk-sim-materialize-graph-regions,obelisk-sim-materialize-compute-fusion,obelisk-sim-specialize-static-state-nba,obelisk-sim-plan-static-superstep),convert-obelisk-sim-processes-to-llvm-coroutines)' \
// RUN:   | FileCheck %s

// A clockless event-driven design still uses the generated coordinator. The
// AOT-node loop owns calendar/control progress and invokes the coordinator
// whenever static fanout queues model ingress.
module attributes {
  llvm.data_layout = "e-m:e-p:64:64-i64:64-n8:16:32:64-S128",
  llvm.target_triple = "x86_64-unknown-linux-gnu",
  schedule.native_scheduler = 0 : i32
} {
  obelisk_sim.design @clockless_eval attributes {
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
          lane = 0, twoState = true, effects = []>],
      edges = [
        #schedule.edge<source = 0, target = 1, kind = process_order>,
        #schedule.edge<source = 1, target = 2, kind = resume>,
        #schedule.edge<source = 2, target = 1, kind = process_order>,
        #schedule.edge<source = 2, target = 1, kind = sensitivity,
          resource = <effect = watch, resource = storage,
            target = descriptor, descriptor = 0, formal = 0, low = 0,
            width = 1, dynamic = false, deferred = false, trigger = change>>,
        #schedule.edge<source = 3, target = 0, kind = spawn>],
      regions = [
        #schedule.region<kind = active, groups = [
          #schedule.group<fragments = [0], schedule = acyclic,
            feedback = []>,
          #schedule.group<fragments = [1], schedule = acyclic,
            feedback = []>,
          #schedule.group<fragments = [2], schedule = acyclic,
            feedback = []>,
          #schedule.group<fragments = [3], schedule = acyclic,
            feedback = []>]>,
        #schedule.region<kind = nba, groups = []>,
        #schedule.region<kind = observed, groups = []>,
        #schedule.region<kind = reactive, groups = []>,
        #schedule.region<kind = postponed, groups = []>]>
  } {
    obelisk_sim.scope.decl 0
    obelisk_sim.code_unit.decl 1 in 0 root_initializer hierarchy "test.root"
    obelisk_sim.code_unit.decl 2 in 0 always hierarchy "test.loop"
    obelisk_sim.storage.decl 0 in 0 : !obelisk_sim.logic<1> design

    obelisk_sim.func @root(
        %ctx: !obelisk_sim.context {obelisk_sim.capture_kind = 0 : i32})
        attributes {entry_kind = 0 : i32, code_unit_id = 1 : i64} {
      %state = obelisk_sim.context.storage %ctx[0] :
          !obelisk_sim.ref<!obelisk_sim.logic<1>>
      %loop = obelisk_sim.spawn @loop(%ctx, %state) :
          !obelisk_sim.context, !obelisk_sim.ref<!obelisk_sim.logic<1>>
          -> !obelisk_sim.process
      obelisk_sim.return
    }

    obelisk_sim.func @loop(
        %ctx: !obelisk_sim.context {obelisk_sim.capture_kind = 0 : i32},
        %state: !obelisk_sim.ref<!obelisk_sim.logic<1>>
            {obelisk_sim.capture_kind = 3 : i32,
             obelisk_sim.descriptor_id = 0 : i64})
        attributes {entry_kind = 3 : i32, code_unit_id = 2 : i64,
                    schedule.native.region_body} {
      cf.br ^wait
    ^wait:
      obelisk_sim.suspend.change %state to ^resume
          {site = #schedule.continuation<id = 1>} :
          !obelisk_sim.ref<!obelisk_sim.logic<1>>
    ^resume:
      %value = obelisk_sim.ref.load %state :
          !obelisk_sim.ref<!obelisk_sim.logic<1>> -> !obelisk_sim.logic<1>
      %next = obelisk_sim.logic.unary bit_not %value :
          (!obelisk_sim.logic<1>) -> !obelisk_sim.logic<1>
      obelisk_sim.ref.store %next to %state : !obelisk_sim.logic<1>,
          !obelisk_sim.ref<!obelisk_sim.logic<1>>
      cf.br ^wait
    }
  }
}

// CHECK-LABEL: llvm.func @__obelisk_aot_schedule_run_v1
// CHECK: llvm.call @obelisk_rt_v1_scheduler_run_aot_nodes
// CHECK-LABEL: llvm.func @__obelisk_eval_dispatch_v1
// CHECK: llvm.switch
