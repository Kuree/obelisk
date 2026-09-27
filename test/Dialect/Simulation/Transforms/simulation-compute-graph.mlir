// RUN: obelisk-opt %s --pass-pipeline='builtin.module(simulation.design(obelisk-sim-build-compute-graph,obelisk-sim-verify-compute-graph))' | FileCheck %s
// RUN: obelisk-opt %s --pass-pipeline='builtin.module(simulation.design(obelisk-sim-build-compute-graph,obelisk-sim-verify-compute-graph,simulation.func(obelisk-sim-thread-suspension),obelisk-sim-verify-compute-graph))' > /dev/null

module {
  // The compute graph is a late analysis result attached to the design, not a
  // replacement gate/netlist IR. It contains all standard event-region plans.
  // CHECK: compute_graph = #schedule.graph<version = 1, vpi = off, workers = 1
  // CHECK-SAME: #schedule.fragment<
  // CHECK-SAME: function = @unknown_div{{.*}}twoState = false
  // CHECK-SAME: #schedule.nba_commit<id = [[COMMIT:[0-9]+]], slots = [0, 1, 2]
  // CHECK-SAME: accumulatorSites = [3, 4], frontierSites = [5]
  // CHECK-SAME: #schedule.event_commit<
  // CHECK-SAME: sites = [0, 1]
  // CHECK-SAME: kind = conflict
  // CHECK-SAME: kind = nba_stage
  // CHECK-SAME: kind = deferred_stage
  // CHECK-SAME: #schedule.region<kind = active
  // CHECK-SAME: schedule = convergence, feedback = [
  // CHECK-SAME: schedule = control_loop, feedback = []
  // CHECK-SAME: #schedule.region<kind = nba
  // CHECK-SAME: #schedule.region<kind = observed
  // CHECK-SAME: #schedule.region<kind = reactive
  // CHECK-SAME: #schedule.region<kind = postponed
  simulation.design @graph {
    simulation.code_unit.decl 9000001 in 0 function hierarchy "test.graph.read_nibble.9000001"
    simulation.code_unit.decl 9000002 in 0 initial hierarchy "test.graph.process.9000002"
    simulation.code_unit.decl 9000003 in 0 initial hierarchy "test.graph.unbounded_nba.9000003"
    simulation.code_unit.decl 9000004 in 0 initial hierarchy "test.graph.unknown_div.9000004"
    simulation.code_unit.decl 9000005 in 0 function hierarchy "test.graph.recursive.9000005"
    simulation.code_unit.decl 9000006 in 0 initial hierarchy "test.graph.caller_a.9000006"
    simulation.code_unit.decl 9000007 in 0 initial hierarchy "test.graph.caller_b.9000007"
    simulation.code_unit.decl 9000008 in 0 initial hierarchy "test.graph.event_threaded.9000008"
    simulation.code_unit.decl 9000009 in 0 initial hierarchy "test.graph.backward_cfg.9000009"
    simulation.code_unit.decl 9000010 in 0 always_ff hierarchy "test.graph.z_clocked_nba.9000010"
    simulation.code_unit.decl 9000011 in 0 initial hierarchy "test.graph.z_repeated_delayed_nba.9000011"
    simulation.code_unit.decl 9000012 in 0 observer hierarchy "test.graph.observer_primary.9000012"
    simulation.code_unit.decl 9000013 in 0 observer hierarchy "test.graph.observer_condition_effect.9000013"
    simulation.code_unit.decl 9000014 in 0 initial hierarchy "test.graph.observer_effect_waiter.9000014"
    simulation.scope.decl 0
    simulation.storage.decl 0 in 0 : !simulation.logic<16> design
    simulation.storage.decl 1 in 0 : !simulation.logic<8> design
    simulation.storage.decl 2 in 0 : !simulation.logic<1> design

    // Formal-handle summaries are parametric and retain the selected range.
    // CHECK-LABEL: simulation.func @read_nibble
    // CHECK-SAME: effect_summary = [#schedule.effect<effect = read, resource = storage, target = formal, descriptor = 0, formal = 1, low = 4, width = 4
    simulation.func @read_nibble(
        %ctx: !simulation.context {simulation.capture_kind = 0 : i32},
        %formal: !simulation.ref<!simulation.logic<16>> {simulation.capture_kind = 1 : i32})
        -> !simulation.logic<4> attributes {entry_kind = 8 : i32, code_unit_id = 9000001 : i64} {
      %slice = simulation.ref.extract %formal from 4 : !simulation.ref<!simulation.logic<16>> -> !simulation.ref<!simulation.logic<4>>
      %value = simulation.ref.load %slice : !simulation.ref<!simulation.logic<4>> -> !simulation.logic<4>
      simulation.return %value : !simulation.logic<4>
    }

    // CHECK-LABEL: simulation.func @process
    // The callee formal is substituted with concrete storage #0.
    // CHECK-SAME: effect_summary = [#schedule.effect<effect = read, resource = storage, target = descriptor, descriptor = 0, formal = 0, low = 4, width = 4
    // A dynamic destination conservatively covers its input handle.
    // CHECK-SAME: effect = write, resource = storage, target = descriptor, descriptor = 0, formal = 0, low = 0, width = 16, dynamic = true
    // A dynamic select through a static subhandle stays within that subhandle.
    // CHECK-SAME: effect = write, resource = storage, target = descriptor, descriptor = 0, formal = 0, low = 4, width = 8, dynamic = true
    // CHECK-SAME: effect = write, resource = storage, target = descriptor, descriptor = 1
    // CHECK-SAME: effect = watch, resource = storage, target = descriptor, descriptor = 1
    // CHECK-SAME: trigger = change
    // CHECK-SAME: effect = nba, resource = storage, target = descriptor, descriptor = 1, formal = 0, low = 0, width = 4
    simulation.func @process(
        %ctx: !simulation.context {simulation.capture_kind = 0 : i32},
        %wide: !simulation.ref<!simulation.logic<16>> {simulation.capture_kind = 3 : i32, simulation.descriptor_id = 0 : i64},
        %result: !simulation.ref<!simulation.logic<8>> {simulation.capture_kind = 3 : i32, simulation.descriptor_id = 1 : i64},
        %index: i8 {simulation.capture_kind = 2 : i32})
        attributes {entry_kind = 1 : i32, code_unit_id = 9000002 : i64} {
      %value = simulation.call @read_nibble(%ctx, %wide) : (!simulation.context, !simulation.ref<!simulation.logic<16>>) -> !simulation.logic<4>
      %dynamic = simulation.ref.dyn_extract %wide from %index : (!simulation.ref<!simulation.logic<16>>, i8) -> !simulation.ref<!simulation.logic<4>>
      simulation.ref.store %value to %dynamic : !simulation.logic<4>, !simulation.ref<!simulation.logic<4>>
      %middle = simulation.ref.extract %wide from 4 : !simulation.ref<!simulation.logic<16>> -> !simulation.ref<!simulation.logic<8>>
      %middle_dynamic = simulation.ref.dyn_extract %middle from %index : (!simulation.ref<!simulation.logic<8>>, i8) -> !simulation.ref<!simulation.logic<4>>
      simulation.ref.store %value to %middle_dynamic : !simulation.logic<4>, !simulation.ref<!simulation.logic<4>>
      %nba_target = simulation.ref.extract %result from 0 : !simulation.ref<!simulation.logic<8>> -> !simulation.ref<!simulation.logic<4>>
      // CHECK: simulation.nba.enqueue
      // CHECK-SAME: site = #schedule.nba_site<id = 0, commit = [[COMMIT]], storage = fixed_slot>
      simulation.nba.enqueue %value to %nba_target : (!simulation.logic<4>, !simulation.ref<!simulation.logic<4>>) -> ()
      %overlap_target = simulation.ref.extract %result from 2 : !simulation.ref<!simulation.logic<8>> -> !simulation.ref<!simulation.logic<4>>
      // Overlapping destinations share one root journal and preserve site order.
      // CHECK: simulation.nba.enqueue
      // CHECK-SAME: site = #schedule.nba_site<id = 1, commit = [[COMMIT]], storage = fixed_slot>
      simulation.nba.enqueue %value to %overlap_target : (!simulation.logic<4>, !simulation.ref<!simulation.logic<4>>) -> ()
      %delay = simulation.time.constant 5
      // A statically single-shot delayed NBA keeps a fixed staging slot and a
      // generated timing site.
      // CHECK: simulation.nba.enqueue
      // CHECK-SAME: site = #schedule.nba_site<id = 2, commit = [[COMMIT]], storage = fixed_slot, timing = <id = 1, kind = delayed_nba>>
      simulation.nba.enqueue %value to %nba_target after %delay : (!simulation.logic<4>, !simulation.ref<!simulation.logic<4>>, !simulation.time) -> ()
      %event = simulation.context.event %ctx[0] : !simulation.event
      // CHECK: simulation.event.trigger
      // CHECK-SAME: site = #schedule.event_site<id = 1, commit = {{[0-9]+}}>
      simulation.event.trigger %event nonblocking = true
      // CHECK: simulation.suspend.delay
      // CHECK-SAME: site = #schedule.continuation<id = [[CONT:[0-9]+]]>
      // CHECK-SAME: timing = #schedule.timing_site<id = 2, kind = calendar>
      simulation.suspend.delay %delay to ^resume
    ^resume:
      // A self-activation is represented as a convergence SCC.
      %resume_target = simulation.ref.extract %result from 0 : !simulation.ref<!simulation.logic<8>> -> !simulation.ref<!simulation.logic<4>>
      simulation.ref.store %value to %resume_target : !simulation.logic<4>, !simulation.ref<!simulation.logic<4>>
      simulation.suspend.change %result to ^resume : !simulation.ref<!simulation.logic<8>>
    }

    simulation.func private @observer_primary(
        %ctx: !simulation.context {simulation.capture_kind = 0 : i32})
        -> !simulation.logic<8> attributes {entry_kind = 14 : i32, code_unit_id = 9000012 : i64} {
      %watched = simulation.context.storage %ctx[1] : !simulation.ref<!simulation.logic<8>>
      %value = simulation.ref.load %watched : !simulation.ref<!simulation.logic<8>> -> !simulation.logic<8>
      simulation.return %value : !simulation.logic<8>
    }

    simulation.func private @observer_condition_effect(
        %ctx: !simulation.context {simulation.capture_kind = 0 : i32},
        %target: !simulation.ref<!simulation.logic<1>> {simulation.capture_kind = 2 : i32})
        -> i1 attributes {entry_kind = 14 : i32, code_unit_id = 9000013 : i64} {
      %one = simulation.logic.constant true, false : !simulation.logic<1>
      simulation.ref.store %one to %target : !simulation.logic<1>, !simulation.ref<!simulation.logic<1>>
      %true = arith.constant true
      simulation.return %true : i1
    }

    // Observer evaluators execute as zero-time callees of the waiting
    // fragment. Their transitive effects, including an impure iff evaluator,
    // must therefore be substituted into the waiter's compute-graph summary.
    // CHECK-LABEL: simulation.func @observer_effect_waiter
    // CHECK-SAME: effect_summary = [
    // CHECK-SAME: effect = write, resource = storage, target = descriptor, descriptor = 2
    // CHECK-SAME: effect = watch, resource = storage, target = descriptor, descriptor = 1
    simulation.func @observer_effect_waiter(
        %ctx: !simulation.context {simulation.capture_kind = 0 : i32},
        %watched: !simulation.ref<!simulation.logic<8>> {simulation.capture_kind = 3 : i32, simulation.descriptor_id = 1 : i64},
        %target: !simulation.ref<!simulation.logic<1>> {simulation.capture_kind = 3 : i32, simulation.descriptor_id = 2 : i64})
        attributes {entry_kind = 1 : i32, code_unit_id = 9000014 : i64} {
      %primary = simulation.observer.bind @observer_primary values(%watched : !simulation.ref<!simulation.logic<8>>) captures 0 : !simulation.observer<!simulation.logic<8>>
      %condition = simulation.observer.bind @observer_condition_effect values(%target : !simulation.ref<!simulation.logic<1>>) captures 1 : !simulation.observer<i1>
      %initial = simulation.logic.constant 0 : i8, 0 : i8 : !simulation.logic<8>
      simulation.suspend.observe %primary, %initial, %condition conditions 1 edges [0] indices [0] to ^resume : !simulation.observer<!simulation.logic<8>>, !simulation.logic<8>, !simulation.observer<i1>
    ^resume:
      simulation.return
    }

    // A repeated immediate site uses a generated root accumulator. It records
    // final value/unknown/mask and transition masks without queue allocation.
    // CHECK-LABEL: simulation.func @unbounded_nba
    simulation.func @unbounded_nba(
        %ctx: !simulation.context {simulation.capture_kind = 0 : i32},
        %result: !simulation.ref<!simulation.logic<8>> {simulation.capture_kind = 3 : i32, simulation.descriptor_id = 1 : i64})
        attributes {entry_kind = 1 : i32, code_unit_id = 9000003 : i64} {
      %value = simulation.logic.constant 1 : i8, 0 : i8 : !simulation.logic<8>
      cf.br ^loop
    ^loop:
      // CHECK: simulation.nba.enqueue
      // CHECK-SAME: site = #schedule.nba_site<id = 3, commit = [[COMMIT]], storage = root_accumulator>
      simulation.nba.enqueue %value to %result : (!simulation.logic<8>, !simulation.ref<!simulation.logic<8>>) -> ()
      cf.br ^loop
    }

    // Division by a known zero is not proof of a two-state result.
    simulation.func @unknown_div(
        %ctx: !simulation.context {simulation.capture_kind = 0 : i32})
        attributes {entry_kind = 1 : i32, code_unit_id = 9000004 : i64} {
      %one = simulation.logic.constant 1 : i8, 0 : i8 : !simulation.logic<8>
      %zero = simulation.logic.constant 0 : i8, 0 : i8 : !simulation.logic<8>
      %quotient = simulation.logic.binary udiv %one, %zero : !simulation.logic<8>
      simulation.return
    }

    // Recursive calls receive conservative unknown mod/ref effects. The two
    // process callers therefore require an explicit conflict edge.
    simulation.func @recursive(
        %ctx: !simulation.context {simulation.capture_kind = 0 : i32})
        attributes {entry_kind = 8 : i32, code_unit_id = 9000005 : i64} {
      simulation.call @recursive(%ctx) : (!simulation.context) -> ()
      simulation.return
    }
    simulation.func @caller_a(
        %ctx: !simulation.context {simulation.capture_kind = 0 : i32})
        attributes {entry_kind = 1 : i32, code_unit_id = 9000006 : i64} {
      simulation.call @recursive(%ctx) : (!simulation.context) -> ()
      simulation.return
    }
    simulation.func @caller_b(
        %ctx: !simulation.context {simulation.capture_kind = 0 : i32})
        attributes {entry_kind = 1 : i32, code_unit_id = 9000007 : i64} {
      simulation.call @recursive(%ctx) : (!simulation.context) -> ()
      simulation.return
    }

    // The second verifier runs after suspension threading. The event handle
    // then arrives in ^resume as a continuation block argument, and must keep
    // the same concrete event provenance as its defining context.event op.
    simulation.func @event_threaded(
        %ctx: !simulation.context {simulation.capture_kind = 0 : i32})
        attributes {entry_kind = 1 : i32, code_unit_id = 9000008 : i64} {
      %event = simulation.context.event %ctx[0] : !simulation.event
      %delay = simulation.time.constant 1
      simulation.suspend.delay %delay to ^resume
    ^resume:
      simulation.event.trigger %event nonblocking = true
      simulation.return
    }

    // Same-process blocks are ordered by CFG edges, not by their textual block
    // IDs. A backward CFG edge must not acquire the opposite synthetic
    // inter-process conflict edge and become a false SCC.
    simulation.func @backward_cfg(
        %ctx: !simulation.context {simulation.capture_kind = 0 : i32},
        %result: !simulation.ref<!simulation.logic<8>> {simulation.capture_kind = 3 : i32, simulation.descriptor_id = 1 : i64})
        attributes {entry_kind = 1 : i32, code_unit_id = 9000009 : i64} {
      %value = simulation.logic.constant 1 : i8, 0 : i8 : !simulation.logic<8>
      cf.br ^high
    ^low:
      simulation.ref.store %value to %result : !simulation.logic<8>, !simulation.ref<!simulation.logic<8>>
      simulation.return
    ^high:
      simulation.ref.store %value to %result : !simulation.logic<8>, !simulation.ref<!simulation.logic<8>>
      cf.br ^low
    }

    // Suspension is not an NBA commit boundary: another active-region update
    // may re-arm this process before the NBA region drains. The generated root
    // accumulator preserves final update and edge-activation semantics.
    // CHECK-LABEL: simulation.func @z_clocked_nba
    simulation.func @z_clocked_nba(
        %ctx: !simulation.context {simulation.capture_kind = 0 : i32},
        %result: !simulation.ref<!simulation.logic<8>> {simulation.capture_kind = 3 : i32, simulation.descriptor_id = 1 : i64})
        attributes {entry_kind = 5 : i32, code_unit_id = 9000010 : i64} {
      %value = simulation.logic.constant 1 : i8, 0 : i8 : !simulation.logic<8>
      cf.br ^clock
    ^clock:
      // CHECK: simulation.nba.enqueue
      // CHECK-SAME: site = #schedule.nba_site<id = 4, commit = [[COMMIT]], storage = root_accumulator>
      simulation.nba.enqueue %value to %result : (!simulation.logic<8>, !simulation.ref<!simulation.logic<8>>) -> ()
      simulation.suspend.change %result to ^clock : !simulation.ref<!simulation.logic<8>>
    }

    // A delayed site that can run again after suspension may have multiple
    // outstanding updates and therefore requires the unbounded frontier.
    // CHECK-LABEL: simulation.func @z_repeated_delayed_nba
    simulation.func @z_repeated_delayed_nba(
        %ctx: !simulation.context {simulation.capture_kind = 0 : i32},
        %result: !simulation.ref<!simulation.logic<8>> {simulation.capture_kind = 3 : i32, simulation.descriptor_id = 1 : i64})
        attributes {entry_kind = 1 : i32, code_unit_id = 9000011 : i64} {
      %value = simulation.logic.constant 1 : i8, 0 : i8 : !simulation.logic<8>
      %delay = simulation.time.constant 100
      %tick = simulation.time.constant 1
      cf.br ^loop
    ^loop:
      // CHECK: simulation.nba.enqueue
      // CHECK-SAME: site = #schedule.nba_site<id = 5, commit = [[COMMIT]], storage = dynamic_frontier, timing = <id = 3, kind = delayed_nba>>
      simulation.nba.enqueue %value to %result after %delay : (!simulation.logic<8>, !simulation.ref<!simulation.logic<8>>, !simulation.time) -> ()
      simulation.suspend.delay %tick to ^loop
    }
  }
}
