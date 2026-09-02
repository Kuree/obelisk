// RUN: obelisk-opt %s \
// RUN:   --pass-pipeline='builtin.module(obelisk_sim.design(obelisk-sim-materialize-graph-regions,obelisk-sim-materialize-compute-fusion,obelisk-sim-specialize-static-state-nba,obelisk-sim-plan-static-superstep),convert-obelisk-sim-processes-to-llvm-coroutines)' \
// RUN:   | FileCheck %s

// A generated fusion executor may retain stable source-owner metadata for a
// physical continuation whose exact body was preserved outside the outlined
// fusion. Group membership alone does not prove that the executor runs that
// continuation. Exercise the production ownership planner and coroutine
// materializer, and require both physical direct bodies to survive.
module attributes {
  llvm.data_layout = "e-m:e-p:64:64-i64:64-n8:16:32:64-S128",
  llvm.target_triple = "x86_64-unknown-linux-gnu",
  obelisk.native_scheduler = 3 : i32
} {
  obelisk_sim.design @fusion_ownership attributes {
    compute_graph = #obelisk_sim.graph<
      version = 1, vpi = off, workers = 1,
      nodes = [
        #obelisk_sim.fragment<id = 0, function = @fused, block = 0,
          region = active, action = continue, tier = native, cost = 1,
          lane = 0, twoState = true, effects = []>,
        #obelisk_sim.fragment<id = 1, function = @fused, block = 1,
          region = active, action = suspend_change, tier = native, cost = 1,
          lane = 0, twoState = true, effects = [
            #obelisk_sim.effect<effect = watch, resource = storage,
              target = descriptor, descriptor = 0, formal = 0, low = 0,
              width = 1, dynamic = false, deferred = false,
              trigger = change>]>,
        #obelisk_sim.fragment<id = 2, function = @fused, block = 2,
          region = active, action = continue, tier = native, cost = 1,
          lane = 0, twoState = true, effects = []>,
        #obelisk_sim.fragment<id = 3, function = @boundary, block = 0,
          region = active, action = continue, tier = native, cost = 1,
          lane = 0, twoState = true, effects = []>,
        #obelisk_sim.fragment<id = 4, function = @boundary, block = 1,
          region = active, action = suspend_change, tier = native, cost = 1,
          lane = 0, twoState = true, effects = [
            #obelisk_sim.effect<effect = watch, resource = storage,
              target = descriptor, descriptor = 1, formal = 0, low = 0,
              width = 1, dynamic = false, deferred = false,
              trigger = change>]>,
        #obelisk_sim.fragment<id = 5, function = @boundary, block = 2,
          region = active, action = continue, tier = native, cost = 1,
          lane = 0, twoState = true, effects = []>,
        #obelisk_sim.fragment<id = 6, function = @root, block = 0,
          region = active, action = terminate, tier = native, cost = 1,
          lane = 0, twoState = true, effects = []>,
        #obelisk_sim.fragment<id = 7, function = @clock, block = 0,
          region = active, action = continue, tier = native, cost = 1,
          lane = 0, twoState = true, effects = []>,
        #obelisk_sim.fragment<id = 8, function = @clock, block = 1,
          region = active, action = suspend_delay, tier = native, cost = 1,
          lane = 0, twoState = true, effects = []>,
        #obelisk_sim.fragment<id = 9, function = @clock, block = 2,
          region = active, action = continue, tier = native, cost = 2,
          lane = 0, twoState = true, effects = [
            #obelisk_sim.effect<effect = read, resource = storage,
              target = descriptor, descriptor = 0, formal = 0, low = 0,
              width = 1, dynamic = false, deferred = false, trigger = none>,
            #obelisk_sim.effect<effect = write, resource = storage,
              target = descriptor, descriptor = 0, formal = 0, low = 0,
              width = 1, dynamic = false, deferred = false, trigger = none>]>,
        #obelisk_sim.fragment<id = 10, function = @clock_outside, block = 0,
          region = active, action = continue, tier = native, cost = 1,
          lane = 0, twoState = true, effects = []>,
        #obelisk_sim.fragment<id = 11, function = @clock_outside, block = 1,
          region = active, action = suspend_delay, tier = native, cost = 1,
          lane = 0, twoState = true, effects = []>,
        #obelisk_sim.fragment<id = 12, function = @clock_outside, block = 2,
          region = active, action = continue, tier = native, cost = 2,
          lane = 0, twoState = true, effects = [
            #obelisk_sim.effect<effect = read, resource = storage,
              target = descriptor, descriptor = 1, formal = 0, low = 0,
              width = 1, dynamic = false, deferred = false, trigger = none>,
            #obelisk_sim.effect<effect = write, resource = storage,
              target = descriptor, descriptor = 1, formal = 0, low = 0,
              width = 1, dynamic = false, deferred = false, trigger = none>]>],
      edges = [
        #obelisk_sim.edge<source = 0, target = 1, kind = process_order>,
        #obelisk_sim.edge<source = 1, target = 2, kind = resume>,
        #obelisk_sim.edge<source = 2, target = 1, kind = process_order>,
        #obelisk_sim.edge<source = 3, target = 4, kind = process_order>,
        #obelisk_sim.edge<source = 4, target = 5, kind = resume>,
        #obelisk_sim.edge<source = 5, target = 4, kind = process_order>,
        #obelisk_sim.edge<source = 6, target = 0, kind = spawn>,
        #obelisk_sim.edge<source = 6, target = 3, kind = spawn>,
        #obelisk_sim.edge<source = 6, target = 7, kind = spawn>,
        #obelisk_sim.edge<source = 6, target = 10, kind = spawn>,
        #obelisk_sim.edge<source = 7, target = 8, kind = process_order>,
        #obelisk_sim.edge<source = 8, target = 9, kind = resume>,
        #obelisk_sim.edge<source = 9, target = 1, kind = sensitivity,
          resource = <effect = watch, resource = storage,
            target = descriptor, descriptor = 0, formal = 0, low = 0,
            width = 1, dynamic = false, deferred = false, trigger = change>>,
        #obelisk_sim.edge<source = 9, target = 8, kind = process_order>,
        #obelisk_sim.edge<source = 10, target = 11, kind = process_order>,
        #obelisk_sim.edge<source = 11, target = 12, kind = resume>,
        #obelisk_sim.edge<source = 12, target = 4, kind = sensitivity,
          resource = <effect = watch, resource = storage,
            target = descriptor, descriptor = 1, formal = 0, low = 0,
            width = 1, dynamic = false, deferred = false, trigger = change>>,
        #obelisk_sim.edge<source = 12, target = 11, kind = process_order>],
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
            feedback = []>,
          #obelisk_sim.group<fragments = [5], schedule = acyclic,
            feedback = []>,
          #obelisk_sim.group<fragments = [6], schedule = acyclic,
            feedback = []>,
          #obelisk_sim.group<fragments = [7], schedule = acyclic,
            feedback = []>,
          #obelisk_sim.group<fragments = [8], schedule = acyclic,
            feedback = []>,
          #obelisk_sim.group<fragments = [9], schedule = acyclic,
            feedback = []>,
          #obelisk_sim.group<fragments = [10], schedule = acyclic,
            feedback = []>,
          #obelisk_sim.group<fragments = [11], schedule = acyclic,
            feedback = []>,
          #obelisk_sim.group<fragments = [12], schedule = acyclic,
            feedback = []>]>,
        #obelisk_sim.region<kind = nba, groups = []>,
        #obelisk_sim.region<kind = observed, groups = []>,
        #obelisk_sim.region<kind = reactive, groups = []>,
        #obelisk_sim.region<kind = postponed, groups = []>]>
  } {
    obelisk_sim.scope.decl 0
    obelisk_sim.code_unit.decl 1 in 0 root_initializer hierarchy "owners.root"
    obelisk_sim.code_unit.decl 2 in 0 always hierarchy "owners.fused"
    obelisk_sim.code_unit.decl 3 in 0 always hierarchy "owners.boundary"
    obelisk_sim.code_unit.decl 4 in 0 always hierarchy "owners.clock"
    obelisk_sim.code_unit.decl 5 in 0 always hierarchy "owners.clock_outside"
    obelisk_sim.storage.decl 0 in 0 : !obelisk_sim.logic<1> design
    obelisk_sim.storage.decl 1 in 0 : !obelisk_sim.logic<1> design

    obelisk_sim.func @root(
        %ctx: !obelisk_sim.context {obelisk_sim.capture_kind = 0 : i32})
        attributes {entry_kind = 0 : i32, code_unit_id = 1 : i64} {
      %inside = obelisk_sim.context.storage %ctx[0] :
          !obelisk_sim.ref<!obelisk_sim.logic<1>>
      %outside = obelisk_sim.context.storage %ctx[1] :
          !obelisk_sim.ref<!obelisk_sim.logic<1>>
      %fused = obelisk_sim.spawn @fused(%ctx, %inside) :
          !obelisk_sim.context, !obelisk_sim.ref<!obelisk_sim.logic<1>>
          -> !obelisk_sim.process
      %boundary = obelisk_sim.spawn @boundary(%ctx, %outside) :
          !obelisk_sim.context, !obelisk_sim.ref<!obelisk_sim.logic<1>>
          -> !obelisk_sim.process
      %clock = obelisk_sim.spawn @clock(%ctx, %inside) :
          !obelisk_sim.context, !obelisk_sim.ref<!obelisk_sim.logic<1>>
          -> !obelisk_sim.process
      %clock_outside = obelisk_sim.spawn @clock_outside(%ctx, %outside) :
          !obelisk_sim.context, !obelisk_sim.ref<!obelisk_sim.logic<1>>
          -> !obelisk_sim.process
      obelisk_sim.return
    }

    obelisk_sim.func @fused(
        %ctx: !obelisk_sim.context {obelisk_sim.capture_kind = 0 : i32},
        %state: !obelisk_sim.ref<!obelisk_sim.logic<1>>
            {obelisk_sim.capture_kind = 3 : i32,
             obelisk_sim.descriptor_id = 0 : i64})
        attributes {entry_kind = 3 : i32, code_unit_id = 2 : i64,
                    obelisk.native.region_body,
                    obelisk.eval.fusion_group = 0 : i32,
                    obelisk.eval.source_owners = [
                      {code_unit = 2 : i64, continuation = 1 : i32},
                      {code_unit = 3 : i64, continuation = 2 : i32}]} {
      cf.br ^wait
    ^wait:
      obelisk_sim.suspend.change %state to ^resume
          {site = #obelisk_sim.continuation<id = 1>} :
          !obelisk_sim.ref<!obelisk_sim.logic<1>>
    ^resume:
      cf.br ^wait
    }

    obelisk_sim.func @boundary(
        %ctx: !obelisk_sim.context {obelisk_sim.capture_kind = 0 : i32},
        %state: !obelisk_sim.ref<!obelisk_sim.logic<1>>
            {obelisk_sim.capture_kind = 3 : i32,
             obelisk_sim.descriptor_id = 1 : i64})
        attributes {entry_kind = 3 : i32, code_unit_id = 3 : i64,
                    obelisk.native.region_body} {
      cf.br ^wait
    ^wait:
      obelisk_sim.suspend.change %state to ^resume
          {site = #obelisk_sim.continuation<id = 2>} :
          !obelisk_sim.ref<!obelisk_sim.logic<1>>
    ^resume:
      cf.br ^wait
    }

    obelisk_sim.func @clock(
        %ctx: !obelisk_sim.context {obelisk_sim.capture_kind = 0 : i32},
        %inside: !obelisk_sim.ref<!obelisk_sim.logic<1>>
            {obelisk_sim.capture_kind = 3 : i32,
             obelisk_sim.descriptor_id = 0 : i64})
        attributes {entry_kind = 3 : i32, code_unit_id = 4 : i64} {
      cf.br ^wait
    ^wait:
      %delay = obelisk_sim.time.constant 1
      obelisk_sim.suspend.delay %delay to ^toggle
          {site = #obelisk_sim.continuation<id = 3>,
           timing = #obelisk_sim.timing_site<id = 0, kind = calendar>}
    ^toggle:
      %inside_old = obelisk_sim.ref.load %inside :
          !obelisk_sim.ref<!obelisk_sim.logic<1>> -> !obelisk_sim.logic<1>
      %inside_new = obelisk_sim.logic.unary bit_not %inside_old :
          (!obelisk_sim.logic<1>) -> !obelisk_sim.logic<1>
      obelisk_sim.ref.store %inside_new to %inside :
          !obelisk_sim.logic<1>, !obelisk_sim.ref<!obelisk_sim.logic<1>>
      cf.br ^wait
    }

    obelisk_sim.func @clock_outside(
        %ctx: !obelisk_sim.context {obelisk_sim.capture_kind = 0 : i32},
        %outside: !obelisk_sim.ref<!obelisk_sim.logic<1>>
            {obelisk_sim.capture_kind = 3 : i32,
             obelisk_sim.descriptor_id = 1 : i64})
        attributes {entry_kind = 3 : i32, code_unit_id = 5 : i64} {
      cf.br ^wait
    ^wait:
      %delay = obelisk_sim.time.constant 2
      obelisk_sim.suspend.delay %delay to ^toggle
          {site = #obelisk_sim.continuation<id = 4>,
           timing = #obelisk_sim.timing_site<id = 1, kind = calendar>}
    ^toggle:
      %outside_old = obelisk_sim.ref.load %outside :
          !obelisk_sim.ref<!obelisk_sim.logic<1>> -> !obelisk_sim.logic<1>
      %outside_new = obelisk_sim.logic.unary bit_not %outside_old :
          (!obelisk_sim.logic<1>) -> !obelisk_sim.logic<1>
      obelisk_sim.ref.store %outside_new to %outside :
          !obelisk_sim.logic<1>, !obelisk_sim.ref<!obelisk_sim.logic<1>>
      cf.br ^wait
    }
  }
}

// CHECK-LABEL: llvm.func @__obelisk_direct_fragment_1_1.__obelisk_execute(
// CHECK-LABEL: llvm.func @__obelisk_direct_fragment_2_2.__obelisk_execute(
// CHECK-LABEL: llvm.func @__obelisk_eval_fast_coordinator_v1
// CHECK: llvm.call @__obelisk_direct_fragment_1_1.__obelisk_execute
// CHECK: llvm.call @__obelisk_direct_fragment_2_2.__obelisk_execute
