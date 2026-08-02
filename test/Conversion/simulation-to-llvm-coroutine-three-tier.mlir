// RUN: obelisk-opt %s \
// RUN:   --pass-pipeline='builtin.module(obelisk_sim.design(obelisk-sim-materialize-graph-regions,obelisk-sim-specialize-static-state-nba,obelisk-sim-plan-static-superstep),convert-obelisk-sim-processes-to-llvm-coroutines)' \
// RUN:   | FileCheck %s

module attributes {
  llvm.data_layout = "e-m:e-p:64:64-i64:64-n8:16:32:64-S128",
  llvm.target_triple = "x86_64-unknown-linux-gnu",
  obelisk.native_scheduler = 2 : i32
} {
  obelisk_sim.design @direct_scc attributes {
    compute_graph = #obelisk_sim.graph<
      version = 1, vpi = off, workers = 1,
      nodes = [
        #obelisk_sim.fragment<id = 0, function = @loop, block = 0,
          region = active, action = continue, tier = native, cost = 1,
          lane = 0, twoState = true, effects = []>,
        #obelisk_sim.fragment<id = 1, function = @loop, block = 1,
          region = active, action = suspend_change, tier = native, cost = 1,
          lane = 0, twoState = true, effects = [
            #obelisk_sim.effect<effect = watch, resource = storage,
              target = descriptor, descriptor = 0, formal = 0, low = 0,
              width = 8, dynamic = false, deferred = false,
              trigger = change>]>,
        #obelisk_sim.fragment<id = 2, function = @loop, block = 2,
          region = active, action = continue, tier = native, cost = 1,
          lane = 0, twoState = true, effects = [
            #obelisk_sim.effect<effect = read, resource = storage,
              target = descriptor, descriptor = 0, formal = 0, low = 0,
              width = 8, dynamic = false, deferred = false, trigger = none>,
            #obelisk_sim.effect<effect = write, resource = storage,
              target = descriptor, descriptor = 0, formal = 0, low = 0,
              width = 8, dynamic = false, deferred = false,
              trigger = none>]>,
        #obelisk_sim.fragment<id = 3, function = @root, block = 0,
          region = active, action = terminate, tier = native, cost = 1,
          lane = 0, twoState = true, effects = []>],
      edges = [],
      regions = [
        #obelisk_sim.region<kind = active, groups = [
          #obelisk_sim.group<fragments = [0, 1, 2], schedule = convergence,
            feedback = [#obelisk_sim.effect<effect = write,
              resource = storage, target = descriptor, descriptor = 0,
              formal = 0, low = 0, width = 8, dynamic = false,
              deferred = false, trigger = none>]>,
          #obelisk_sim.group<fragments = [3], schedule = acyclic,
            feedback = []>]>,
        #obelisk_sim.region<kind = nba, groups = []>,
        #obelisk_sim.region<kind = observed, groups = []>,
        #obelisk_sim.region<kind = reactive, groups = []>,
        #obelisk_sim.region<kind = postponed, groups = []>]>
  } {
    obelisk_sim.scope.decl 0
    obelisk_sim.code_unit.decl 1 in 0 root_initializer hierarchy "scc.root"
    obelisk_sim.code_unit.decl 2 in 0 always_comb hierarchy "scc.loop"
    obelisk_sim.storage.decl 0 in 0 : !obelisk_sim.logic<8> design

    obelisk_sim.func @root(
        %ctx: !obelisk_sim.context {obelisk_sim.capture_kind = 0 : i32})
        attributes {entry_kind = 0 : i32, code_unit_id = 1 : i64} {
      %storage = obelisk_sim.context.storage %ctx[0] :
          !obelisk_sim.ref<!obelisk_sim.logic<8>>
      %loop = obelisk_sim.spawn @loop(%ctx, %storage) :
          !obelisk_sim.context, !obelisk_sim.ref<!obelisk_sim.logic<8>>
          -> !obelisk_sim.process
      obelisk_sim.return
    }

    obelisk_sim.func @loop(
        %ctx: !obelisk_sim.context {obelisk_sim.capture_kind = 0 : i32},
        %state: !obelisk_sim.ref<!obelisk_sim.logic<8>>
            {obelisk_sim.capture_kind = 3 : i32,
             obelisk_sim.descriptor_id = 0 : i64})
        attributes {entry_kind = 4 : i32, code_unit_id = 2 : i64} {
      cf.br ^wait
    ^wait:
      obelisk_sim.suspend.change %state to ^resume
          {site = #obelisk_sim.continuation<id = 1>} :
          !obelisk_sim.ref<!obelisk_sim.logic<8>>
    ^resume:
      %value = obelisk_sim.ref.load %state :
          !obelisk_sim.ref<!obelisk_sim.logic<8>> -> !obelisk_sim.logic<8>
      obelisk_sim.ref.store %value to %state : !obelisk_sim.logic<8>,
          !obelisk_sim.ref<!obelisk_sim.logic<8>>
      cf.br ^wait
    }
  }
}

// An inactive direct SCC is one ingress comparison and branch. An active SCC
// evaluates only selected members, carries publications across sweeps, and has
// no iteration counter or runtime call. Tier 1 calls the subkernel directly.
// CHECK-LABEL: llvm.func @__obelisk_tier2_converge_v1_
// CHECK-NOT: llvm.call @obelisk_rt_
// CHECK: llvm.cond_br
// CHECK: llvm.mlir.addressof @__obelisk_tier2_members_v1_
// CHECK: llvm.load
// CHECK: llvm.call %{{.*}}(%{{.*}}, %{{.*}}) : !llvm.ptr, (!llvm.ptr, i32) -> !llvm.struct<(i64, i64)>
// CHECK: llvm.or
// CHECK: llvm.cond_br
// CHECK: llvm.return
// CHECK-LABEL: llvm.func @__obelisk_tier1_invoke_tier2_v1_
// CHECK-NOT: llvm.call @obelisk_rt_
// CHECK: llvm.call @__obelisk_tier2_converge_v1_
// CHECK: llvm.return
// Four-state and two-state bodies are separately selectable and contain no
// promotion guard. The slot-local eval step selects once before calling the
// body and invokes Tier 2 directly, without a named runtime call.
// CHECK-LABEL: llvm.func @__obelisk_tier1_eval_four_state_v1_
// CHECK-NOT: __obelisk_tier1_selected_variant
// CHECK: llvm.call %{{.*}}
// CHECK: llvm.return
// CHECK-LABEL: llvm.func @__obelisk_tier1_eval_two_state_v1_
// CHECK-NOT: __obelisk_tier1_selected_variant
// CHECK: llvm.call %{{.*}}
// CHECK: llvm.return
// CHECK-LABEL: llvm.func @__obelisk_tier1_eval_step_v1
// CHECK-NOT: llvm.call @obelisk_rt_
// CHECK: llvm.call @__obelisk_tier2_converge_v1_
// CHECK: llvm.mlir.addressof @__obelisk_tier1_selected_variant_v1
// CHECK: llvm.select
// CHECK: llvm.call %{{.*}}
// CHECK: llvm.return
