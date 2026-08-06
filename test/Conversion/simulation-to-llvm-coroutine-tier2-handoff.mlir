// RUN: obelisk-opt %s \
// RUN:   --pass-pipeline='builtin.module(obelisk_sim.design(obelisk-sim-materialize-graph-regions,obelisk-sim-materialize-compute-fusion,obelisk-sim-specialize-static-state-nba,obelisk-sim-plan-static-superstep),convert-obelisk-sim-processes-to-llvm-coroutines)' \
// RUN:   | FileCheck %s
// RUN: sed -e 's/code_unit.decl 4 in 0 initial/code_unit.decl 4 in 0 always/' \
// RUN:   -e 's/attributes {entry_kind = 1 : i32, code_unit_id = 4/attributes {entry_kind = 3 : i32, code_unit_id = 4/' %s \
// RUN:   | not obelisk-opt - \
// RUN:   --pass-pipeline='builtin.module(obelisk_sim.design(obelisk-sim-materialize-graph-regions,obelisk-sim-materialize-compute-fusion,obelisk-sim-specialize-static-state-nba,obelisk-sim-plan-static-superstep),convert-obelisk-sim-processes-to-llvm-coroutines)' \
// RUN:   2>&1 | FileCheck %s --check-prefix=LIVE-ARG
// RUN: sed -e 's/obelisk.native.region_body,/obelisk.native.region_body/' \
// RUN:   -e '/obelisk.eval.reconstructs_continuation_args/d' %s \
// RUN:   | not obelisk-opt - \
// RUN:   --pass-pipeline='builtin.module(obelisk_sim.design(obelisk-sim-materialize-graph-regions,obelisk-sim-materialize-compute-fusion,obelisk-sim-specialize-static-state-nba,obelisk-sim-plan-static-superstep),convert-obelisk-sim-processes-to-llvm-coroutines)' \
// RUN:   2>&1 | FileCheck %s --check-prefix=UNCERTIFIED-REGION

// Exercise the production Tier-1 -> Tier-2 path.  The hand-authored graph
// keeps the convergence ownership stable so this test characterizes lowering,
// not the graph builder.  The generated coordinator must invoke the direct SCC
// owner and poll the branch-only termination word before its dirty backedge.
module attributes {
  llvm.data_layout = "e-m:e-p:64:64-i64:64-n8:16:32:64-S128",
  llvm.target_triple = "x86_64-unknown-linux-gnu",
  obelisk.native_scheduler = 3 : i32
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
              width = 1, dynamic = false, deferred = false,
              trigger = change>]>,
        #obelisk_sim.fragment<id = 2, function = @loop, block = 2,
          region = active, action = continue, tier = native, cost = 1,
          lane = 0, twoState = true, effects = [
            #obelisk_sim.effect<effect = read, resource = storage,
              target = descriptor, descriptor = 0, formal = 0, low = 0,
              width = 1, dynamic = false, deferred = false, trigger = none>,
            #obelisk_sim.effect<effect = write, resource = storage,
              target = descriptor, descriptor = 0, formal = 0, low = 0,
              width = 1, dynamic = false, deferred = false, trigger = none>]>,
        #obelisk_sim.fragment<id = 3, function = @root, block = 0,
          region = active, action = terminate, tier = native, cost = 1,
          lane = 0, twoState = true, effects = []>,
        #obelisk_sim.fragment<id = 4, function = @reset, block = 0,
          region = active, action = continue, tier = native, cost = 1,
          lane = 0, twoState = true, effects = []>,
        #obelisk_sim.fragment<id = 5, function = @reset, block = 1,
          region = active, action = suspend_change, tier = native, cost = 1,
          lane = 0, twoState = true, effects = [
            #obelisk_sim.effect<effect = watch, resource = storage,
              target = descriptor, descriptor = 0, formal = 0, low = 0,
              width = 1, dynamic = false, deferred = false,
              trigger = change>]>,
        #obelisk_sim.fragment<id = 6, function = @reset, block = 2,
          region = active, action = terminate, tier = native, cost = 1,
          lane = 0, twoState = true, effects = []>],
      edges = [],
      regions = [
        #obelisk_sim.region<kind = active, groups = [
          #obelisk_sim.group<fragments = [0], schedule = acyclic,
            feedback = []>,
          #obelisk_sim.group<fragments = [1, 2], schedule = convergence,
            feedback = [#obelisk_sim.effect<effect = write,
              resource = storage, target = descriptor, descriptor = 0,
              formal = 0, low = 0, width = 1, dynamic = false,
              deferred = false, trigger = none>]>,
          #obelisk_sim.group<fragments = [3], schedule = acyclic,
            feedback = []>,
          #obelisk_sim.group<fragments = [4], schedule = acyclic,
            feedback = []>,
          #obelisk_sim.group<fragments = [5], schedule = acyclic,
            feedback = []>,
          #obelisk_sim.group<fragments = [6], schedule = acyclic,
            feedback = []>]>,
        #obelisk_sim.region<kind = nba, groups = []>,
        #obelisk_sim.region<kind = observed, groups = []>,
        #obelisk_sim.region<kind = reactive, groups = []>,
        #obelisk_sim.region<kind = postponed, groups = []>]>
  } {
    obelisk_sim.scope.decl 0
    obelisk_sim.code_unit.decl 1 in 0 root_initializer hierarchy "scc.root"
    obelisk_sim.code_unit.decl 2 in 0 always hierarchy "scc.loop"
    obelisk_sim.code_unit.decl 3 in 0 always hierarchy "scc.clock"
    obelisk_sim.code_unit.decl 4 in 0 initial hierarchy "scc.reset"
    obelisk_sim.storage.decl 0 in 0 : !obelisk_sim.logic<1> design

    obelisk_sim.func @root(
        %ctx: !obelisk_sim.context {obelisk_sim.capture_kind = 0 : i32})
        attributes {entry_kind = 0 : i32, code_unit_id = 1 : i64} {
      %storage = obelisk_sim.context.storage %ctx[0] :
          !obelisk_sim.ref<!obelisk_sim.logic<1>>
      %loop = obelisk_sim.spawn @loop(%ctx, %storage) :
          !obelisk_sim.context, !obelisk_sim.ref<!obelisk_sim.logic<1>>
          -> !obelisk_sim.process
      %clock = obelisk_sim.spawn @clock(%ctx, %storage) :
          !obelisk_sim.context, !obelisk_sim.ref<!obelisk_sim.logic<1>>
          -> !obelisk_sim.process
      %remaining = arith.constant true
      %reset = obelisk_sim.spawn @reset(%ctx, %storage, %remaining) :
          !obelisk_sim.context, !obelisk_sim.ref<!obelisk_sim.logic<1>>, i1
          -> !obelisk_sim.process
      obelisk_sim.return
    }

    // The loop-carried continuation makes this a finite Tier-3 bootstrap
    // owner. It must be drained before periodic handoff, not represented by a
    // null executor in the closed Tier-1/Tier-2 coordinator.
    obelisk_sim.func @reset(
        %ctx: !obelisk_sim.context {obelisk_sim.capture_kind = 0 : i32},
        %state: !obelisk_sim.ref<!obelisk_sim.logic<1>>
            {obelisk_sim.capture_kind = 3 : i32,
             obelisk_sim.descriptor_id = 0 : i64},
        %remaining: i1 {obelisk_sim.capture_kind = 2 : i32})
        attributes {entry_kind = 1 : i32, code_unit_id = 4 : i64} {
      cf.br ^wait
    ^wait:
      obelisk_sim.suspend.change %state to ^done(%remaining : i1)
          {site = #obelisk_sim.continuation<id = 3>} :
          !obelisk_sim.ref<!obelisk_sim.logic<1>>
    ^done(%ignored: i1):
      obelisk_sim.return
    }

    obelisk_sim.func @loop(
        %ctx: !obelisk_sim.context {obelisk_sim.capture_kind = 0 : i32},
        %state: !obelisk_sim.ref<!obelisk_sim.logic<1>>
            {obelisk_sim.capture_kind = 3 : i32,
             obelisk_sim.descriptor_id = 0 : i64})
        attributes {entry_kind = 3 : i32, code_unit_id = 2 : i64,
                    obelisk.native.region_body,
                    obelisk.eval.reconstructs_continuation_args
        } {
      %carried = arith.constant true
      cf.br ^wait(%carried : i1)
    ^wait(%keep: i1):
      // Region outlining reloads canonical instance state on each activation,
      // so this actor-side block argument must not make the generated region
      // look like a Tier-3 coroutine continuation.
      obelisk_sim.suspend.change %state to ^resume(%keep : i1)
          {site = #obelisk_sim.continuation<id = 1>} :
          !obelisk_sim.ref<!obelisk_sim.logic<1>>
    ^resume(%ignored: i1):
      %value = obelisk_sim.ref.load %state :
          !obelisk_sim.ref<!obelisk_sim.logic<1>> -> !obelisk_sim.logic<1>
      %next = obelisk_sim.logic.unary bit_not %value :
          (!obelisk_sim.logic<1>) -> !obelisk_sim.logic<1>
      obelisk_sim.ref.store %next to %state : !obelisk_sim.logic<1>,
          !obelisk_sim.ref<!obelisk_sim.logic<1>>
      cf.br ^wait(%ignored : i1)
    }

    obelisk_sim.func @clock(
        %ctx: !obelisk_sim.context {obelisk_sim.capture_kind = 0 : i32},
        %state: !obelisk_sim.ref<!obelisk_sim.logic<1>>
            {obelisk_sim.capture_kind = 3 : i32,
             obelisk_sim.descriptor_id = 0 : i64})
        attributes {entry_kind = 3 : i32, code_unit_id = 3 : i64} {
      cf.br ^wait
    ^wait:
      %delay = obelisk_sim.time.constant 1
      obelisk_sim.suspend.delay %delay to ^toggle
          {site = #obelisk_sim.continuation<id = 2>,
           timing = #obelisk_sim.timing_site<id = 0, kind = calendar>}
    ^toggle:
      %old = obelisk_sim.ref.load %state :
          !obelisk_sim.ref<!obelisk_sim.logic<1>> -> !obelisk_sim.logic<1>
      %new = obelisk_sim.logic.unary bit_not %old :
          (!obelisk_sim.logic<1>) -> !obelisk_sim.logic<1>
      obelisk_sim.ref.store %new to %state : !obelisk_sim.logic<1>,
          !obelisk_sim.ref<!obelisk_sim.logic<1>>
      cf.br ^wait
    }
  }
}

// CHECK-NOT: llvm.func @__obelisk_direct_fragment_3_3
// CHECK-DAG: llvm.mlir.global internal @__obelisk_eval_periodic_promotion_scanned_v1
// CHECK-LABEL: llvm.func @loop.__obelisk_eval_body_0(
// CHECK: llvm.xor
// CHECK: llvm.store
// CHECK: llvm.mlir.addressof @__obelisk_aot_model_ingress_v1
// CHECK: llvm.store
// CHECK-NOT: llvm.call @obelisk_rt_
// CHECK-LABEL: llvm.func @__obelisk_direct_fragment_1_1.__obelisk_execute(
// CHECK-SAME: attributes {
// CHECK-SAME: obelisk.eval.tier2_convergence
// CHECK-NOT: llvm.call @obelisk_rt_
// The first quiescent boundary scans the exact periodic closure once. Later
// boundaries test only the pending mask; asynchronous invalidation clears the
// scan latch and makes the next boundary rescan.
// CHECK-LABEL: llvm.func @__obelisk_eval_periodic_promotion_ready_v1
// CHECK: llvm.mlir.addressof @__obelisk_eval_periodic_promotion_scanned_v1
// CHECK: llvm.cond_br
// CHECK: llvm.call @__obelisk_eval_kernel_promotion_ready_v1_0
// CHECK: llvm.store {{.*}}, {{.*}} : i8, !llvm.ptr
// CHECK: llvm.mlir.addressof @__obelisk_eval_promotion_pending_mask_v1
// CHECK: llvm.load
// CHECK-LABEL: llvm.func @__obelisk_eval_promotion_invalidate_v1
// CHECK: %[[SCAN_LATCH:.*]] = llvm.mlir.addressof @__obelisk_eval_periodic_promotion_scanned_v1
// CHECK: %[[SCAN_ZERO:.*]] = llvm.mlir.constant(0 : i8)
// CHECK: llvm.store %[[SCAN_ZERO]], %[[SCAN_LATCH]]
// CHECK-LABEL: llvm.func @__obelisk_aot_schedule_run_v1
// CHECK: llvm.call @obelisk_rt_v1_scheduler_prepare_periodic_aot
// CHECK-LABEL: llvm.func @__obelisk_eval_fast_coordinator_v1
// CHECK-NOT: llvm.call @obelisk_rt_
// CHECK: llvm.switch
// CHECK: %[[INGRESS:.*]] = llvm.mlir.addressof @__obelisk_aot_model_ingress_v1
// CHECK: %[[QUEUED:.*]] = llvm.load %[[INGRESS]]
// CHECK: %[[CLEARED:.*]] = llvm.and %[[QUEUED]],
// CHECK: llvm.store %[[CLEARED]], %[[INGRESS]]
// CHECK: llvm.call @__obelisk_direct_fragment_1_1.__obelisk_execute
// CHECK: llvm.mlir.addressof @__obelisk_periodic_termination_v1
// CHECK: llvm.load
// CHECK: llvm.cond_br
// CHECK-NOT: llvm.call @obelisk_rt_
// CHECK-LABEL: llvm.func @__obelisk_eval_fast_coordinator_two_state_v1
// A pending SCC owner exits to the hybrid path. Once that owner is known, the
// accepted steady path calls a statically specialized wrapper rather than its
// mutable promotion route.
// CHECK-LABEL: llvm.func @__obelisk_eval_steady_two_state_coordinator_v1
// CHECK-SAME: obelisk.eval.trusted_two_state_coordinator
// CHECK: llvm.mlir.addressof @__obelisk_eval_promotion_pending_mask_v1
// CHECK: llvm.load
// CHECK: llvm.or
// CHECK: "llvm.intr.cttz"
// CHECK: llvm.and
// CHECK: llvm.cond_br
// CHECK-NOT: llvm.mlir.addressof @__obelisk_eval_function_route_v1_
// CHECK: llvm.call @__obelisk_direct_fragment_1_1.__obelisk_execute.two_state.__obelisk_trusted
// LIVE-ARG: eval exact owner miss: actor=3 continuation=3 function=reset
// LIVE-ARG-SAME: candidates=
// UNCERTIFIED-REGION: a Tier-2 SCC has no direct eval owner
