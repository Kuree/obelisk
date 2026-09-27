// RUN: obelisk-opt %s --pass-pipeline='builtin.module(simulation.design(obelisk-sim-build-compute-graph,obelisk-sim-verify-compute-graph))' | FileCheck %s
// RUN: obelisk-opt %s --pass-pipeline='builtin.module(simulation.design(obelisk-sim-build-compute-graph,obelisk-sim-verify-compute-graph))' | obelisk-opt -o /dev/null

module {
  // A SystemVerilog function may not consume time, but nonblocking assignment
  // and `->>` are legal inside one. They still need compiled sites even though
  // the function is not itself a schedulable actor.
  simulation.design @function_sites {
    simulation.code_unit.decl 9000001 in 0 function hierarchy "test.function_sites.stage.9000001"
    simulation.code_unit.decl 9000002 in 0 initial hierarchy "test.function_sites.process.9000002"
    simulation.scope.decl 0
    simulation.storage.decl 0 in 0 : !simulation.logic<8> design

    // CHECK-LABEL: simulation.func @stage
    // CHECK-SAME: effect_summary = [#schedule.effect<effect = nba
    simulation.func @stage(
        %ctx: !simulation.context {simulation.capture_kind = 0 : i32},
        %destination: !simulation.ref<!simulation.logic<8>> {simulation.capture_kind = 1 : i32},
        %event: !simulation.event {simulation.capture_kind = 1 : i32})
        attributes {entry_kind = 8 : i32, code_unit_id = 9000001 : i64} {
      %value = simulation.logic.constant 1 : i8, 0 : i8 : !simulation.logic<8>
      // A function body may run any number of times, so its staging site can
      // never own a fixed slot.
      // CHECK: simulation.nba.enqueue
      // CHECK-SAME: site = #schedule.nba_site<id = 0, commit = {{[0-9]+}}, storage = dynamic_frontier>
      simulation.nba.enqueue %value to %destination : (!simulation.logic<8>, !simulation.ref<!simulation.logic<8>>) -> ()
      // CHECK: simulation.event.trigger
      // CHECK-SAME: site = #schedule.event_site<id = 0
      simulation.event.trigger %event nonblocking = true
      simulation.return
    }

    // A staged site in a shared function keeps one stable unknown-root commit
    // until call-graph specialization clones it. Other effect kinds may still
    // specialize onto caller descriptors.
    // CHECK-LABEL: simulation.func @process
    // CHECK-SAME: effect_summary = [#schedule.effect<effect = nba, resource = unknown
    simulation.func @process(
        %ctx: !simulation.context {simulation.capture_kind = 0 : i32},
        %storage: !simulation.ref<!simulation.logic<8>> {simulation.capture_kind = 3 : i32, simulation.descriptor_id = 0 : i64})
        attributes {entry_kind = 1 : i32, code_unit_id = 9000002 : i64} {
      %event = simulation.context.event %ctx[7] : !simulation.event
      simulation.call @stage(%ctx, %storage, %event) : (!simulation.context, !simulation.ref<!simulation.logic<8>>, !simulation.event) -> ()
      simulation.return
    }
  }

  // An absent provenance fact is not a path that a CFG join may ignore. The
  // call result currently has unknown event provenance, so joining it with a
  // concrete event must remain unknown rather than selecting descriptor 0.
  simulation.design @provenance_join {
    simulation.code_unit.decl 9000001 in 0 function hierarchy "test.provenance_join.other_event.9000001"
    simulation.code_unit.decl 9000002 in 0 initial hierarchy "test.provenance_join.join_event.9000002"
    simulation.scope.decl 0

    simulation.func @other_event(
        %ctx: !simulation.context {simulation.capture_kind = 0 : i32})
        -> !simulation.event attributes {entry_kind = 8 : i32, code_unit_id = 9000001 : i64} {
      %event = simulation.context.event %ctx[1] : !simulation.event
      simulation.return %event : !simulation.event
    }

    // CHECK-LABEL: simulation.func @join_event
    // CHECK-SAME: effect_summary = [#schedule.effect<effect = trigger, resource = unknown
    simulation.func @join_event(
        %ctx: !simulation.context {simulation.capture_kind = 0 : i32},
        %condition: i1 {simulation.capture_kind = 2 : i32})
        attributes {entry_kind = 1 : i32, code_unit_id = 9000002 : i64} {
      %known = simulation.context.event %ctx[0] : !simulation.event
      %other = simulation.call @other_event(%ctx) : (!simulation.context) -> !simulation.event
      cf.cond_br %condition, ^join(%known : !simulation.event), ^join(%other : !simulation.event)
    ^join(%event: !simulation.event):
      simulation.event.trigger %event nonblocking = false
      simulation.return
    }
  }
}
