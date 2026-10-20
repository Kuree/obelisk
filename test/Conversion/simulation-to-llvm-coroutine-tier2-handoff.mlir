// RUN: obelisk-opt %s \
// RUN:   --pass-pipeline='builtin.module(simulation.design(obelisk-sim-materialize-graph-regions,obelisk-sim-materialize-compute-fusion,obelisk-sim-specialize-static-state-nba,obelisk-sim-plan-static-superstep),convert-obelisk-sim-processes-to-llvm-coroutines)' \
// RUN:   -o %t.mlir
// RUN: FileCheck %s < %t.mlir
// RUN: FileCheck %s --check-prefix=PUBLICATION < %t.mlir
// RUN: obelisk-opt %s --verify-each=0 --pass-pipeline='builtin.module(\
// RUN:   test-obelisk-edit-attributes{op=simulation.code_unit.decl match={id = 4 : i64} set={code_unit_kind = 3 : i32}},\
// RUN:   test-obelisk-edit-attributes{match={code_unit_id = 4 : i64} set={entry_kind = 3 : i32}},\
// RUN:   simulation.design(obelisk-sim-materialize-graph-regions,obelisk-sim-materialize-compute-fusion,obelisk-sim-specialize-static-state-nba,obelisk-sim-plan-static-superstep),convert-obelisk-sim-processes-to-llvm-coroutines)' \
// RUN:   | FileCheck %s --check-prefix=LIVE-ARG
// RUN: obelisk-opt %s --pass-pipeline='builtin.module(\
// RUN:   test-obelisk-edit-attributes{has=schedule.eval.reconstructs_continuation_args remove=schedule.eval.reconstructs_continuation_args},\
// RUN:   simulation.design(obelisk-sim-materialize-graph-regions,obelisk-sim-materialize-compute-fusion,obelisk-sim-specialize-static-state-nba,obelisk-sim-plan-static-superstep),convert-obelisk-sim-processes-to-llvm-coroutines)' \
// RUN:   | FileCheck %s --check-prefix=UNCERTIFIED
// RUN: obelisk-opt %s --pass-pipeline='builtin.module(\
// RUN:   test-obelisk-edit-attributes{match={site = #schedule.continuation<id = 1>} set={schedule.procedural_event_wait}},\
// RUN:   simulation.design(obelisk-sim-materialize-graph-regions,obelisk-sim-materialize-compute-fusion,obelisk-sim-specialize-static-state-nba,obelisk-sim-plan-static-superstep),convert-obelisk-sim-processes-to-llvm-coroutines)' \
// RUN:   | FileCheck %s --check-prefix=EVENT

// Exercise the production Tier-1 -> Tier-2 path.  The hand-authored graph
// keeps the convergence ownership stable so this test characterizes lowering,
// not the graph builder.  The generated coordinator must invoke the direct SCC
// owner and poll the branch-only termination word before its dirty backedge.
module attributes {
  llvm.data_layout = "e-m:e-p:64:64-i64:64-n8:16:32:64-S128",
  llvm.target_triple = "x86_64-unknown-linux-gnu",
  schedule.native_scheduler = 3 : i32
} {
  simulation.design @direct_scc attributes {
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
          region = active, action = continue, tier = native, cost = 1,
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
        #schedule.fragment<id = 4, function = @reset, block = 0,
          region = active, action = continue, tier = native, cost = 1,
          lane = 0, twoState = true, effects = []>,
        #schedule.fragment<id = 5, function = @reset, block = 1,
          region = active, action = suspend_change, tier = native, cost = 1,
          lane = 0, twoState = true, effects = [
            #schedule.effect<effect = watch, resource = storage,
              target = descriptor, descriptor = 0, formal = 0, low = 0,
              width = 1, dynamic = false, deferred = false,
              trigger = change>]>,
        #schedule.fragment<id = 6, function = @reset, block = 2,
          region = active, action = terminate, tier = native, cost = 1,
          lane = 0, twoState = true, effects = []>],
      edges = [],
      regions = [
        #schedule.region<kind = active, groups = [
          #schedule.group<fragments = [0], schedule = acyclic,
            feedback = []>,
          #schedule.group<fragments = [1, 2], schedule = convergence,
            feedback = [#schedule.effect<effect = write,
              resource = storage, target = descriptor, descriptor = 0,
              formal = 0, low = 0, width = 1, dynamic = false,
              deferred = false, trigger = none>]>,
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
    simulation.code_unit.decl 1 in 0 root_initializer hierarchy "scc.root"
    simulation.code_unit.decl 2 in 0 always hierarchy "scc.loop"
    simulation.code_unit.decl 3 in 0 always hierarchy "scc.clock"
    simulation.code_unit.decl 4 in 0 initial hierarchy "scc.reset"
    simulation.code_unit.decl 5 in 0 fork hierarchy "scc.negative_commit"
    simulation.storage.decl 0 in 0 : !simulation.logic<1> design

    simulation.func @root(
        %ctx: !simulation.context {simulation.capture_kind = 0 : i32})
        attributes {entry_kind = 0 : i32, code_unit_id = 1 : i64} {
      %storage = simulation.context.storage %ctx[0] :
          !simulation.ref<!simulation.logic<1>>
      %loop = simulation.spawn @loop(%ctx, %storage) :
          !simulation.context, !simulation.ref<!simulation.logic<1>>
          -> !simulation.process
      %clock = simulation.spawn @clock(%ctx, %storage) :
          !simulation.context, !simulation.ref<!simulation.logic<1>>
          -> !simulation.process
      %remaining = arith.constant true
      %reset = simulation.spawn @reset(%ctx, %storage, %remaining) :
          !simulation.context, !simulation.ref<!simulation.logic<1>>, i1
          -> !simulation.process
      simulation.return
    }

    // The loop-carried continuation makes this a finite Tier-3 bootstrap
    // owner. It must be drained before periodic handoff, not represented by a
    // null executor in the closed Tier-1/Tier-2 coordinator.
    simulation.func @reset(
        %ctx: !simulation.context {simulation.capture_kind = 0 : i32},
        %state: !simulation.ref<!simulation.logic<1>>
            {simulation.capture_kind = 3 : i32,
             simulation.descriptor_id = 0 : i64},
        %remaining: i1 {simulation.capture_kind = 2 : i32})
        attributes {entry_kind = 1 : i32, code_unit_id = 4 : i64} {
      cf.br ^wait
    ^wait:
      simulation.suspend.change %state to ^done(%remaining : i1)
          {site = #schedule.continuation<id = 3>} :
          !simulation.ref<!simulation.logic<1>>
    ^done(%ignored: i1):
      simulation.return
    }

    simulation.func @loop(
        %ctx: !simulation.context {simulation.capture_kind = 0 : i32},
        %state: !simulation.ref<!simulation.logic<1>>
            {simulation.capture_kind = 3 : i32,
             simulation.descriptor_id = 0 : i64})
        attributes {entry_kind = 3 : i32, code_unit_id = 2 : i64,
                    schedule.native.region_body,
                    schedule.eval.reconstructs_continuation_args
        } {
      %carried = arith.constant true
      cf.br ^wait(%carried : i1)
    ^wait(%keep: i1):
      // Region outlining reloads canonical instance state on each activation,
      // so this actor-side block argument must not make the generated region
      // look like a Tier-3 coroutine continuation.
      simulation.suspend.change %state to ^resume(%keep : i1)
          {site = #schedule.continuation<id = 1>} :
          !simulation.ref<!simulation.logic<1>>
    ^resume(%ignored: i1):
      %value = simulation.ref.load %state :
          !simulation.ref<!simulation.logic<1>> -> !simulation.logic<1>
      %next = simulation.logic.unary bit_not %value :
          (!simulation.logic<1>) -> !simulation.logic<1>
      simulation.ref.store %next to %state : !simulation.logic<1>,
          !simulation.ref<!simulation.logic<1>>
      cf.br ^wait(%ignored : i1)
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

    // IEEE 1800-2017 Clause 31.9.1: this exact compiler-shaped transport
    // commit writes the same direct root as @clock. Its publication is chosen
    // statically for the runtime observer; no descriptor flag or hot runtime
    // branch may distinguish it from the ordinary direct writer.
    simulation.func private @negative_commit(
        %ctx: !simulation.context {simulation.capture_kind = 0 : i32},
        %value: !simulation.logic<1>
            {simulation.capture_kind = 2 : i32})
        attributes {entry_kind = 13 : i32, code_unit_id = 5 : i64,
                    domain = 0 : i32, home_region = 2 : i32, internal,
                    simulation.negative_timing_delay_commit} {
      %delay = simulation.time.constant 1
      simulation.suspend.delay %delay to ^publish(
          %value : !simulation.logic<1>)
          {resume_region = 2 : i32,
           site = #schedule.continuation<id = 4>,
           timing = #schedule.timing_site<id = 0, kind = calendar>}
    ^publish(%delayed: !simulation.logic<1>):
      %state = simulation.context.storage %ctx[0] :
          !simulation.ref<!simulation.logic<1>>
      simulation.ref.store %delayed to %state : !simulation.logic<1>,
          !simulation.ref<!simulation.logic<1>>
      simulation.return
    }
  }
}

// CHECK-NOT: llvm.func @__obelisk_direct_fragment_3_3
// CHECK-NOT: llvm.mlir.global internal @__obelisk_eval_periodic_promotion_scanned_v1
// CHECK-LABEL: llvm.func @loop.__obelisk_eval_body_0(
// CHECK: llvm.xor
// CHECK: llvm.store
// CHECK: llvm.mlir.addressof @__obelisk_aot_model_ingress_v1
// CHECK: llvm.store
// CHECK-NOT: llvm.call @obelisk_rt_
// CHECK-LABEL: llvm.func @__obelisk_direct_fragment_1_1.__obelisk_execute(
// CHECK-SAME: attributes {
// CHECK-SAME: schedule.eval.tier2_convergence
// CHECK-NOT: llvm.call @obelisk_rt_
// Invalidation retains owner-local pending state, with no periodic scan latch.
// CHECK-LABEL: llvm.func @__obelisk_eval_promotion_invalidate_v1
// CHECK: llvm.mlir.addressof @__obelisk_eval_promotion_pending_mask_v1
// CHECK: llvm.store
// CHECK-LABEL: llvm.func @__obelisk_aot_schedule_run_v1
// CHECK: llvm.call @obelisk_rt_v1_scheduler_prepare_periodic_aot
// CHECK-LABEL: llvm.func @__obelisk_eval_dispatch_v1
// CHECK: llvm.call @obelisk_rt_v1_scheduler_prepare_eval_nba
// CHECK: llvm.call @obelisk_rt_v1_scheduler_complete_eval_nba
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
// IEEE 1800-2023 4.4.2.3-4.4.2.4 and 4.5: the runtime calendar cut
// drains Inactive and the rest of the NBA batch before eval resumes.
// CHECK: llvm.call @__obelisk_aot_runtime_nba_commit_v1
// CHECK-LABEL: llvm.func @__obelisk_eval_four_state_nba_handoff_v1

// CHECK: llvm.return

// The ordinary writer retains the direct static call.
// PUBLICATION-LABEL: llvm.func @clock.__obelisk_coro_ramp
// PUBLICATION-NOT: llvm.call @obelisk_rt_v1_scheduler_signal_transition
// PUBLICATION: llvm.call @obelisk_rt_v1_scheduler_static_transition
// PUBLICATION-NOT: llvm.call @obelisk_rt_v1_scheduler_signal_transition
// PUBLICATION: llvm.return
// The structurally certified cold commit selects generic publication during
// lowering, without adding any runtime discriminator to ordinary transitions.
// PUBLICATION-LABEL: llvm.func @negative_commit.__obelisk_coro_ramp
// PUBLICATION-NOT: llvm.call @obelisk_rt_v1_scheduler_static_transition
// PUBLICATION: llvm.call @obelisk_rt_v1_scheduler_signal_transition
// PUBLICATION-NOT: llvm.call @obelisk_rt_v1_scheduler_static_transition
// PUBLICATION: llvm.return

// Reset becomes an always process (code unit 4). Its immutable forwarded
// captures need no coroutine state, even without a previously supplied
// certificate. Extraction proves their reconstruction.
// LIVE-ARG-LABEL: llvm.func @reset.__obelisk_eval_body_0(
// LIVE-ARG: llvm.return

// Without the reconstruction certificate the region body still lowers to an
// eval body.
// UNCERTIFIED-LABEL: llvm.func @loop.__obelisk_eval_body_0(
// UNCERTIFIED: llvm.xor
// UNCERTIFIED: llvm.return

// IEEE 1800-2023 9.4.2: when the loop's wait is a procedural event control,
// the source event control is inactive while its controlled statement runs.
// The loop's own write publishes only the other owner's ingress bit,
// suppressing its self wakeup without retaining a runtime transition callback.
// EVENT-LABEL: llvm.func @loop.__obelisk_eval_body_0(
// EVENT: %[[INGRESS:.*]] = llvm.mlir.addressof @__obelisk_aot_model_ingress_v1
// EVENT-DAG: %[[OTHER:.*]] = llvm.mlir.constant(2 : i64)
// EVENT-DAG: %[[NONE:.*]] = llvm.mlir.constant(0 : i64)
// EVENT-NEXT: %[[BITS:.*]] = llvm.select %{{.*}}, %[[OTHER]], %[[NONE]]
// EVENT-NEXT: %[[QUEUED:.*]] = llvm.load %[[INGRESS]]
// EVENT-NEXT: %[[MERGED:.*]] = llvm.or %[[QUEUED]], %[[BITS]]
// EVENT-NEXT: llvm.store %[[MERGED]], %[[INGRESS]]
// EVENT-NOT: llvm.call @obelisk_rt_
// EVENT: llvm.return
