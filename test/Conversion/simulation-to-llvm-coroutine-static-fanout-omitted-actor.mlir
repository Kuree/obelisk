// RUN: talos-opt %s \
// RUN:   --pass-pipeline='builtin.module(simulation.design(talos-sim-plan-static-superstep),convert-talos-sim-processes-to-llvm-coroutines)' \
// RUN:   | FileCheck %s

// The planner certifies the exact Clause 31 clock coordinator as the sole
// runtime-owned omission.
// simulation-to-llvm-coroutine-static-fanout-bridged.mlir covers an omitted
// actor the planner did not certify.
// CHECK: llvm.mlir.global internal constant @__talos_aot_static_fanout_v1

module attributes {
  llvm.data_layout = "e-m:e-p:64:64-i64:64-n8:16:32:64-S128",
  llvm.target_triple = "x86_64-unknown-linux-gnu",
  schedule.native_scheduler = 3 : i32
} {
  simulation.design @mixed attributes {
    compute_graph = #schedule.graph<
      version = 1, vpi = off, workers = 1,
      nodes = [
        #schedule.fragment<id = 0, function = @root, block = 0,
          region = active, action = terminate, tier = native, cost = 1,
          lane = 0, twoState = true, effects = []>,
        #schedule.fragment<id = 1, function = @coordinator, block = 0,
          region = observed, action = continue, tier = native, cost = 1,
          lane = 0, twoState = true, effects = []>,
        #schedule.fragment<id = 2, function = @coordinator, block = 1,
          region = observed, action = suspend_any, tier = native, cost = 1,
          lane = 0, twoState = true, effects = [
            #schedule.effect<effect = watch, resource = storage,
              target = descriptor, descriptor = 0, formal = 0, low = 0,
              width = 1, dynamic = false, deferred = false,
              trigger = posedge>]>,
        #schedule.fragment<id = 3, function = @ordinary, block = 1,
          region = observed, action = suspend_change, tier = native, cost = 1,
          lane = 0, twoState = true, effects = [
            #schedule.effect<effect = watch, resource = storage,
              target = descriptor, descriptor = 0, formal = 0, low = 0,
              width = 1, dynamic = false, deferred = false,
              trigger = change>]>,
        #schedule.fragment<id = 4, function = @clock, block = 0,
          region = active, action = continue, tier = native, cost = 1,
          lane = 0, twoState = true, effects = []>,
        #schedule.fragment<id = 5, function = @clock, block = 1,
          region = active, action = suspend_delay, tier = native, cost = 1,
          lane = 0, twoState = true, effects = []>,
        #schedule.fragment<id = 6, function = @clock, block = 2,
          region = active, action = continue, tier = native, cost = 1,
          lane = 0, twoState = true, effects = [
            #schedule.effect<effect = read, resource = storage,
              target = descriptor, descriptor = 0, formal = 0, low = 0,
              width = 1, dynamic = false, deferred = false, trigger = none>,
            #schedule.effect<effect = write, resource = storage,
              target = descriptor, descriptor = 0, formal = 0, low = 0,
              width = 1, dynamic = false, deferred = false, trigger = none>]>,
        #schedule.fragment<id = 7, function = @ordinary, block = 0,
          region = observed, action = continue, tier = native, cost = 1,
          lane = 0, twoState = true, effects = []>
      ],
      edges = [
        #schedule.edge<source = 0, target = 1, kind = spawn>,
        #schedule.edge<source = 0, target = 7, kind = spawn>,
        #schedule.edge<source = 0, target = 4, kind = spawn>,
        #schedule.edge<source = 1, target = 2, kind = process_order>,
        #schedule.edge<source = 2, target = 3, kind = process_order>,
        #schedule.edge<source = 3, target = 2, kind = process_order>,
        #schedule.edge<source = 4, target = 5, kind = process_order>,
        #schedule.edge<source = 5, target = 6, kind = resume>,
        #schedule.edge<source = 6, target = 5, kind = process_order>,
        #schedule.edge<source = 6, target = 2, kind = sensitivity,
          resource = <effect = watch, resource = storage,
            target = descriptor, descriptor = 0, formal = 0, low = 0,
            width = 1, dynamic = false, deferred = false,
            trigger = posedge>>,
        #schedule.edge<source = 7, target = 3, kind = process_order>
      ],
      regions = [
        #schedule.region<kind = active, groups = [
          #schedule.group<fragments = [0], schedule = acyclic,
            feedback = []>,
          #schedule.group<fragments = [4], schedule = acyclic,
            feedback = []>,
          #schedule.group<fragments = [5], schedule = acyclic,
            feedback = []>,
          #schedule.group<fragments = [6], schedule = acyclic,
            feedback = []>]>,
        #schedule.region<kind = nba, groups = []>,
        #schedule.region<kind = observed, groups = [
          #schedule.group<fragments = [1], schedule = acyclic,
            feedback = []>,
          #schedule.group<fragments = [7], schedule = acyclic,
            feedback = []>,
          #schedule.group<fragments = [2, 3], schedule = control_loop,
            feedback = []>]>,
        #schedule.region<kind = reactive, groups = []>,
        #schedule.region<kind = postponed, groups = []>]>
  } {
    simulation.scope.decl 0
    simulation.storage.decl 0 in 0 : !simulation.logic<1> design
    simulation.code_unit.decl 1 in 0 root_initializer hierarchy "root"
    simulation.code_unit.decl 2 in 0 always hierarchy "coordinator"
    simulation.code_unit.decl 3 in 0 initial hierarchy "ordinary"
    simulation.code_unit.decl 4 in 0 always hierarchy "clock"

    simulation.func @root(
        %ctx: !simulation.context {simulation.capture_kind = 0 : i32})
        attributes {entry_kind = 0 : i32, code_unit_id = 1 : i64} {
      %clock = simulation.context.storage %ctx[0] :
          !simulation.ref<!simulation.logic<1>>
      %coordinator = simulation.spawn @coordinator(%ctx, %clock) :
          !simulation.context, !simulation.ref<!simulation.logic<1>> ->
          !simulation.process
      %ordinary = simulation.spawn @ordinary(%ctx) :
          !simulation.context -> !simulation.process
      %periodic = simulation.spawn @clock(%ctx, %clock) :
          !simulation.context, !simulation.ref<!simulation.logic<1>> ->
          !simulation.process
      simulation.return
    }

    simulation.func private @coordinator(
        %ctx: !simulation.context {simulation.capture_kind = 0 : i32},
        %clock: !simulation.ref<!simulation.logic<1>>
            {simulation.capture_kind = 3 : i32,
             simulation.descriptor_id = 0 : i64})
        attributes {entry_kind = 3 : i32, code_unit_id = 2 : i64,
                    domain = 0 : i32, home_region = 8 : i32,
                    simulation.timing_check_coordinator} {
      cf.br ^wait
    ^wait:
      simulation.suspend.clock_set %clock conditions 0 edges [1]
          indices [-1] site 23 to ^wait :
          !simulation.ref<!simulation.logic<1>>
    }

    simulation.func @ordinary(
        %ctx: !simulation.context {simulation.capture_kind = 0 : i32})
        attributes {entry_kind = 1 : i32, code_unit_id = 3 : i64,
                    domain = 0 : i32, home_region = 8 : i32} {
      %clock = simulation.context.storage %ctx[0] :
          !simulation.ref<!simulation.logic<1>>
      cf.br ^wait
    ^wait:
      simulation.suspend.change %clock to ^wait
          {site = #schedule.continuation<id = 2>} :
          !simulation.ref<!simulation.logic<1>>
    }

    simulation.func @clock(
        %ctx: !simulation.context {simulation.capture_kind = 0 : i32},
        %clock: !simulation.ref<!simulation.logic<1>>
            {simulation.capture_kind = 3 : i32,
             simulation.descriptor_id = 0 : i64})
        attributes {entry_kind = 3 : i32, code_unit_id = 4 : i64,
                    domain = 0 : i32, home_region = 2 : i32} {
      cf.br ^wait
    ^wait:
      %delay = simulation.time.constant 1
      simulation.suspend.delay %delay to ^toggle
          {site = #schedule.continuation<id = 1>,
           timing = #schedule.timing_site<id = 0, kind = calendar>}
    ^toggle:
      %old = simulation.ref.load %clock :
          !simulation.ref<!simulation.logic<1>> -> !simulation.logic<1>
      %new = simulation.logic.unary bit_not %old :
          (!simulation.logic<1>) -> !simulation.logic<1>
      simulation.ref.store %new to %clock : !simulation.logic<1>,
          !simulation.ref<!simulation.logic<1>>
      cf.br ^wait
    }
  }
}
