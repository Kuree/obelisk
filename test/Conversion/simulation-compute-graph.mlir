// RUN: obelisk-opt %s --pass-pipeline='builtin.module(obelisk_sim.design(obelisk-sim-build-compute-graph,obelisk-sim-verify-compute-graph))' | FileCheck %s
// RUN: obelisk-opt %s --pass-pipeline='builtin.module(obelisk_sim.design(obelisk-sim-build-compute-graph,obelisk-sim-verify-compute-graph,obelisk_sim.func(obelisk-sim-thread-suspension),obelisk-sim-verify-compute-graph))' > /dev/null

module {
  // The compute graph is a late analysis result attached to the design, not a
  // replacement gate/netlist IR. It contains all standard event-region plans.
  // CHECK: compute_graph = #obelisk_sim.graph<version = 1, vpi = off, workers = 1
  // CHECK-SAME: #obelisk_sim.fragment<
  // CHECK-SAME: function = @unknown_div{{.*}}twoState = false
  // CHECK-SAME: #obelisk_sim.nba_commit<id = [[COMMIT:[0-9]+]], slots = [0, 1, 2]
  // CHECK-SAME: accumulatorSites = [3, 4], staticJournalSites = [], frontierSites = [5]
  // CHECK-SAME: #obelisk_sim.event_commit<
  // CHECK-SAME: sites = [0, 1]
  // CHECK-SAME: kind = conflict
  // CHECK-SAME: kind = nba_stage
  // CHECK-SAME: kind = deferred_stage
  // CHECK-SAME: #obelisk_sim.region<kind = active
  // CHECK-SAME: schedule = convergence, feedback = [
  // CHECK-SAME: schedule = control_loop, feedback = []
  // CHECK-SAME: #obelisk_sim.region<kind = nba
  // CHECK-SAME: #obelisk_sim.region<kind = observed
  // CHECK-SAME: #obelisk_sim.region<kind = reactive
  // CHECK-SAME: #obelisk_sim.region<kind = postponed
  obelisk_sim.design @graph {
    obelisk_sim.scope.decl 0
    obelisk_sim.storage.decl 0 in 0 : !obelisk_sim.logic<16> design
    obelisk_sim.storage.decl 1 in 0 : !obelisk_sim.logic<8> design

    // Formal-handle summaries are parametric and retain the selected range.
    // CHECK-LABEL: obelisk_sim.func @read_nibble
    // CHECK-SAME: effect_summary = [#obelisk_sim.effect<effect = read, resource = storage, target = formal, descriptor = 0, formal = 1, low = 4, width = 4
    obelisk_sim.func @read_nibble(
        %ctx: !obelisk_sim.context {obelisk_sim.capture_kind = 0 : i32},
        %formal: !obelisk_sim.ref<!obelisk_sim.logic<16>> {obelisk_sim.capture_kind = 1 : i32})
        -> !obelisk_sim.logic<4> attributes {entry_kind = 8 : i32} {
      %slice = obelisk_sim.ref.extract %formal from 4 : !obelisk_sim.ref<!obelisk_sim.logic<16>> -> !obelisk_sim.ref<!obelisk_sim.logic<4>>
      %value = obelisk_sim.ref.load %slice : !obelisk_sim.ref<!obelisk_sim.logic<4>> -> !obelisk_sim.logic<4>
      obelisk_sim.return %value : !obelisk_sim.logic<4>
    }

    // CHECK-LABEL: obelisk_sim.func @process
    // The callee formal is substituted with concrete storage #0.
    // CHECK-SAME: effect_summary = [#obelisk_sim.effect<effect = read, resource = storage, target = descriptor, descriptor = 0, formal = 0, low = 4, width = 4
    // A dynamic destination conservatively covers its input handle.
    // CHECK-SAME: effect = write, resource = storage, target = descriptor, descriptor = 0, formal = 0, low = 0, width = 16, dynamic = true
    // A dynamic select through a static subhandle stays within that subhandle.
    // CHECK-SAME: effect = write, resource = storage, target = descriptor, descriptor = 0, formal = 0, low = 4, width = 8, dynamic = true
    // CHECK-SAME: effect = write, resource = storage, target = descriptor, descriptor = 1
    // CHECK-SAME: effect = watch, resource = storage, target = descriptor, descriptor = 1
    // CHECK-SAME: trigger = change
    // CHECK-SAME: effect = nba, resource = storage, target = descriptor, descriptor = 1, formal = 0, low = 0, width = 4
    obelisk_sim.func @process(
        %ctx: !obelisk_sim.context {obelisk_sim.capture_kind = 0 : i32},
        %wide: !obelisk_sim.ref<!obelisk_sim.logic<16>> {obelisk_sim.capture_kind = 3 : i32, obelisk_sim.descriptor_id = 0 : i64},
        %result: !obelisk_sim.ref<!obelisk_sim.logic<8>> {obelisk_sim.capture_kind = 3 : i32, obelisk_sim.descriptor_id = 1 : i64},
        %index: i8 {obelisk_sim.capture_kind = 2 : i32})
        attributes {entry_kind = 1 : i32} {
      %value = obelisk_sim.call @read_nibble(%ctx, %wide) : (!obelisk_sim.context, !obelisk_sim.ref<!obelisk_sim.logic<16>>) -> !obelisk_sim.logic<4>
      %dynamic = obelisk_sim.ref.dyn_extract %wide from %index : (!obelisk_sim.ref<!obelisk_sim.logic<16>>, i8) -> !obelisk_sim.ref<!obelisk_sim.logic<4>>
      obelisk_sim.ref.store %value to %dynamic : !obelisk_sim.logic<4>, !obelisk_sim.ref<!obelisk_sim.logic<4>>
      %middle = obelisk_sim.ref.extract %wide from 4 : !obelisk_sim.ref<!obelisk_sim.logic<16>> -> !obelisk_sim.ref<!obelisk_sim.logic<8>>
      %middle_dynamic = obelisk_sim.ref.dyn_extract %middle from %index : (!obelisk_sim.ref<!obelisk_sim.logic<8>>, i8) -> !obelisk_sim.ref<!obelisk_sim.logic<4>>
      obelisk_sim.ref.store %value to %middle_dynamic : !obelisk_sim.logic<4>, !obelisk_sim.ref<!obelisk_sim.logic<4>>
      %nba_target = obelisk_sim.ref.extract %result from 0 : !obelisk_sim.ref<!obelisk_sim.logic<8>> -> !obelisk_sim.ref<!obelisk_sim.logic<4>>
      // CHECK: obelisk_sim.nba.enqueue
      // CHECK-SAME: site = #obelisk_sim.nba_site<id = 0, commit = [[COMMIT]], storage = fixed_slot>
      obelisk_sim.nba.enqueue %value to %nba_target : (!obelisk_sim.logic<4>, !obelisk_sim.ref<!obelisk_sim.logic<4>>) -> ()
      %overlap_target = obelisk_sim.ref.extract %result from 2 : !obelisk_sim.ref<!obelisk_sim.logic<8>> -> !obelisk_sim.ref<!obelisk_sim.logic<4>>
      // Overlapping destinations share one root journal and preserve site order.
      // CHECK: obelisk_sim.nba.enqueue
      // CHECK-SAME: site = #obelisk_sim.nba_site<id = 1, commit = [[COMMIT]], storage = fixed_slot>
      obelisk_sim.nba.enqueue %value to %overlap_target : (!obelisk_sim.logic<4>, !obelisk_sim.ref<!obelisk_sim.logic<4>>) -> ()
      %delay = obelisk_sim.time.constant 5
      // A statically single-shot delayed NBA keeps a fixed staging slot and a
      // generated timing site.
      // CHECK: obelisk_sim.nba.enqueue
      // CHECK-SAME: site = #obelisk_sim.nba_site<id = 2, commit = [[COMMIT]], storage = fixed_slot, timing = <id = 1, kind = delayed_nba>>
      obelisk_sim.nba.enqueue %value to %nba_target after %delay : (!obelisk_sim.logic<4>, !obelisk_sim.ref<!obelisk_sim.logic<4>>, !obelisk_sim.time) -> ()
      %event = obelisk_sim.context.event %ctx[0] : !obelisk_sim.event
      // CHECK: obelisk_sim.event.trigger
      // CHECK-SAME: site = #obelisk_sim.event_site<id = 1, commit = {{[0-9]+}}>
      obelisk_sim.event.trigger %event nonblocking = true
      // CHECK: obelisk_sim.suspend.delay
      // CHECK-SAME: site = #obelisk_sim.continuation<id = [[CONT:[0-9]+]]>
      // CHECK-SAME: timing = #obelisk_sim.timing_site<id = 2, kind = calendar>
      obelisk_sim.suspend.delay %delay to ^resume
    ^resume:
      // A self-activation is represented as a convergence SCC.
      %resume_target = obelisk_sim.ref.extract %result from 0 : !obelisk_sim.ref<!obelisk_sim.logic<8>> -> !obelisk_sim.ref<!obelisk_sim.logic<4>>
      obelisk_sim.ref.store %value to %resume_target : !obelisk_sim.logic<4>, !obelisk_sim.ref<!obelisk_sim.logic<4>>
      obelisk_sim.suspend.change %result to ^resume : !obelisk_sim.ref<!obelisk_sim.logic<8>>
    }

    // A repeated immediate site uses a generated root accumulator. It records
    // final value/unknown/mask and transition masks without queue allocation.
    // CHECK-LABEL: obelisk_sim.func @unbounded_nba
    obelisk_sim.func @unbounded_nba(
        %ctx: !obelisk_sim.context {obelisk_sim.capture_kind = 0 : i32},
        %result: !obelisk_sim.ref<!obelisk_sim.logic<8>> {obelisk_sim.capture_kind = 3 : i32, obelisk_sim.descriptor_id = 1 : i64})
        attributes {entry_kind = 1 : i32} {
      %value = obelisk_sim.logic.constant 1 : i8, 0 : i8 : !obelisk_sim.logic<8>
      cf.br ^loop
    ^loop:
      // CHECK: obelisk_sim.nba.enqueue
      // CHECK-SAME: site = #obelisk_sim.nba_site<id = 3, commit = [[COMMIT]], storage = root_accumulator>
      obelisk_sim.nba.enqueue %value to %result : (!obelisk_sim.logic<8>, !obelisk_sim.ref<!obelisk_sim.logic<8>>) -> ()
      cf.br ^loop
    }

    // Division by a known zero is not proof of a two-state result.
    obelisk_sim.func @unknown_div(
        %ctx: !obelisk_sim.context {obelisk_sim.capture_kind = 0 : i32})
        attributes {entry_kind = 1 : i32} {
      %one = obelisk_sim.logic.constant 1 : i8, 0 : i8 : !obelisk_sim.logic<8>
      %zero = obelisk_sim.logic.constant 0 : i8, 0 : i8 : !obelisk_sim.logic<8>
      %quotient = obelisk_sim.logic.binary udiv %one, %zero : !obelisk_sim.logic<8>
      obelisk_sim.return
    }

    // Recursive calls receive conservative unknown mod/ref effects. The two
    // process callers therefore require an explicit conflict edge.
    obelisk_sim.func @recursive(
        %ctx: !obelisk_sim.context {obelisk_sim.capture_kind = 0 : i32})
        attributes {entry_kind = 8 : i32} {
      obelisk_sim.call @recursive(%ctx) : (!obelisk_sim.context) -> ()
      obelisk_sim.return
    }
    obelisk_sim.func @caller_a(
        %ctx: !obelisk_sim.context {obelisk_sim.capture_kind = 0 : i32})
        attributes {entry_kind = 1 : i32} {
      obelisk_sim.call @recursive(%ctx) : (!obelisk_sim.context) -> ()
      obelisk_sim.return
    }
    obelisk_sim.func @caller_b(
        %ctx: !obelisk_sim.context {obelisk_sim.capture_kind = 0 : i32})
        attributes {entry_kind = 1 : i32} {
      obelisk_sim.call @recursive(%ctx) : (!obelisk_sim.context) -> ()
      obelisk_sim.return
    }

    // The second verifier runs after suspension threading. The event handle
    // then arrives in ^resume as a continuation block argument, and must keep
    // the same concrete event provenance as its defining context.event op.
    obelisk_sim.func @event_threaded(
        %ctx: !obelisk_sim.context {obelisk_sim.capture_kind = 0 : i32})
        attributes {entry_kind = 1 : i32} {
      %event = obelisk_sim.context.event %ctx[0] : !obelisk_sim.event
      %delay = obelisk_sim.time.constant 1
      obelisk_sim.suspend.delay %delay to ^resume
    ^resume:
      obelisk_sim.event.trigger %event nonblocking = true
      obelisk_sim.return
    }

    // Same-process blocks are ordered by CFG edges, not by their textual block
    // IDs. A backward CFG edge must not acquire the opposite synthetic
    // inter-process conflict edge and become a false SCC.
    obelisk_sim.func @backward_cfg(
        %ctx: !obelisk_sim.context {obelisk_sim.capture_kind = 0 : i32},
        %result: !obelisk_sim.ref<!obelisk_sim.logic<8>> {obelisk_sim.capture_kind = 3 : i32, obelisk_sim.descriptor_id = 1 : i64})
        attributes {entry_kind = 1 : i32} {
      %value = obelisk_sim.logic.constant 1 : i8, 0 : i8 : !obelisk_sim.logic<8>
      cf.br ^high
    ^low:
      obelisk_sim.ref.store %value to %result : !obelisk_sim.logic<8>, !obelisk_sim.ref<!obelisk_sim.logic<8>>
      obelisk_sim.return
    ^high:
      obelisk_sim.ref.store %value to %result : !obelisk_sim.logic<8>, !obelisk_sim.ref<!obelisk_sim.logic<8>>
      cf.br ^low
    }

    // Suspension is not an NBA commit boundary: another active-region update
    // may re-arm this process before the NBA region drains. The generated root
    // accumulator preserves final update and edge-activation semantics.
    // CHECK-LABEL: obelisk_sim.func @z_clocked_nba
    obelisk_sim.func @z_clocked_nba(
        %ctx: !obelisk_sim.context {obelisk_sim.capture_kind = 0 : i32},
        %result: !obelisk_sim.ref<!obelisk_sim.logic<8>> {obelisk_sim.capture_kind = 3 : i32, obelisk_sim.descriptor_id = 1 : i64})
        attributes {entry_kind = 5 : i32} {
      %value = obelisk_sim.logic.constant 1 : i8, 0 : i8 : !obelisk_sim.logic<8>
      cf.br ^clock
    ^clock:
      // CHECK: obelisk_sim.nba.enqueue
      // CHECK-SAME: site = #obelisk_sim.nba_site<id = 4, commit = [[COMMIT]], storage = root_accumulator>
      obelisk_sim.nba.enqueue %value to %result : (!obelisk_sim.logic<8>, !obelisk_sim.ref<!obelisk_sim.logic<8>>) -> ()
      obelisk_sim.suspend.change %result to ^clock : !obelisk_sim.ref<!obelisk_sim.logic<8>>
    }

    // A delayed site that can run again after suspension may have multiple
    // outstanding updates and therefore requires the unbounded frontier.
    // CHECK-LABEL: obelisk_sim.func @z_repeated_delayed_nba
    obelisk_sim.func @z_repeated_delayed_nba(
        %ctx: !obelisk_sim.context {obelisk_sim.capture_kind = 0 : i32},
        %result: !obelisk_sim.ref<!obelisk_sim.logic<8>> {obelisk_sim.capture_kind = 3 : i32, obelisk_sim.descriptor_id = 1 : i64})
        attributes {entry_kind = 1 : i32} {
      %value = obelisk_sim.logic.constant 1 : i8, 0 : i8 : !obelisk_sim.logic<8>
      %delay = obelisk_sim.time.constant 100
      %tick = obelisk_sim.time.constant 1
      cf.br ^loop
    ^loop:
      // CHECK: obelisk_sim.nba.enqueue
      // CHECK-SAME: site = #obelisk_sim.nba_site<id = 5, commit = [[COMMIT]], storage = dynamic_frontier, timing = <id = 3, kind = delayed_nba>>
      obelisk_sim.nba.enqueue %value to %result after %delay : (!obelisk_sim.logic<8>, !obelisk_sim.ref<!obelisk_sim.logic<8>>, !obelisk_sim.time) -> ()
      obelisk_sim.suspend.delay %tick to ^loop
    }
  }
}
