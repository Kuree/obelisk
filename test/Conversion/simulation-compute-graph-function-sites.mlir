// RUN: obelisk-opt %s --pass-pipeline='builtin.module(obelisk_sim.design(obelisk-sim-build-compute-graph,obelisk-sim-verify-compute-graph))' | FileCheck %s
// RUN: obelisk-opt %s --pass-pipeline='builtin.module(obelisk_sim.design(obelisk-sim-build-compute-graph,obelisk-sim-verify-compute-graph))' | obelisk-opt -o /dev/null

module {
  // A SystemVerilog function may not consume time, but nonblocking assignment
  // and `->>` are legal inside one. They still need compiled sites even though
  // the function is not itself a schedulable actor.
  obelisk_sim.design @function_sites {
    obelisk_sim.scope.decl 0
    obelisk_sim.storage.decl 0 in 0 : !obelisk_sim.logic<8> design

    // CHECK-LABEL: obelisk_sim.func @stage
    // CHECK-SAME: effect_summary = [#obelisk_sim.effect<effect = nba
    obelisk_sim.func @stage(
        %ctx: !obelisk_sim.context {obelisk_sim.capture_kind = 0 : i32},
        %destination: !obelisk_sim.ref<!obelisk_sim.logic<8>> {obelisk_sim.capture_kind = 1 : i32},
        %event: !obelisk_sim.event {obelisk_sim.capture_kind = 1 : i32})
        attributes {entry_kind = 8 : i32} {
      %value = obelisk_sim.logic.constant 1 : i8, 0 : i8 : !obelisk_sim.logic<8>
      // A function body may run any number of times, so its staging site can
      // never own a fixed slot.
      // CHECK: obelisk_sim.nba.enqueue
      // CHECK-SAME: site = #obelisk_sim.nba_site<id = 0, commit = {{[0-9]+}}, storage = dynamic_frontier>
      obelisk_sim.nba.enqueue %value to %destination : (!obelisk_sim.logic<8>, !obelisk_sim.ref<!obelisk_sim.logic<8>>) -> ()
      // CHECK: obelisk_sim.event.trigger
      // CHECK-SAME: site = #obelisk_sim.event_site<id = 0
      obelisk_sim.event.trigger %event nonblocking = true
      obelisk_sim.return
    }

    // A staged site in a shared function keeps one stable unknown-root commit
    // until call-graph specialization clones it. Other effect kinds may still
    // specialize onto caller descriptors.
    // CHECK-LABEL: obelisk_sim.func @process
    // CHECK-SAME: effect_summary = [#obelisk_sim.effect<effect = nba, resource = unknown
    obelisk_sim.func @process(
        %ctx: !obelisk_sim.context {obelisk_sim.capture_kind = 0 : i32},
        %storage: !obelisk_sim.ref<!obelisk_sim.logic<8>> {obelisk_sim.capture_kind = 3 : i32, obelisk_sim.descriptor_id = 0 : i64})
        attributes {entry_kind = 1 : i32} {
      %event = obelisk_sim.context.event %ctx[7] : !obelisk_sim.event
      obelisk_sim.call @stage(%ctx, %storage, %event) : (!obelisk_sim.context, !obelisk_sim.ref<!obelisk_sim.logic<8>>, !obelisk_sim.event) -> ()
      obelisk_sim.return
    }
  }

  // An absent provenance fact is not a path that a CFG join may ignore. The
  // call result currently has unknown event provenance, so joining it with a
  // concrete event must remain unknown rather than selecting descriptor 0.
  obelisk_sim.design @provenance_join {
    obelisk_sim.scope.decl 0

    obelisk_sim.func @other_event(
        %ctx: !obelisk_sim.context {obelisk_sim.capture_kind = 0 : i32})
        -> !obelisk_sim.event attributes {entry_kind = 8 : i32} {
      %event = obelisk_sim.context.event %ctx[1] : !obelisk_sim.event
      obelisk_sim.return %event : !obelisk_sim.event
    }

    // CHECK-LABEL: obelisk_sim.func @join_event
    // CHECK-SAME: effect_summary = [#obelisk_sim.effect<effect = trigger, resource = unknown
    obelisk_sim.func @join_event(
        %ctx: !obelisk_sim.context {obelisk_sim.capture_kind = 0 : i32},
        %condition: i1 {obelisk_sim.capture_kind = 2 : i32})
        attributes {entry_kind = 1 : i32} {
      %known = obelisk_sim.context.event %ctx[0] : !obelisk_sim.event
      %other = obelisk_sim.call @other_event(%ctx) : (!obelisk_sim.context) -> !obelisk_sim.event
      cf.cond_br %condition, ^join(%known : !obelisk_sim.event), ^join(%other : !obelisk_sim.event)
    ^join(%event: !obelisk_sim.event):
      obelisk_sim.event.trigger %event nonblocking = false
      obelisk_sim.return
    }
  }
}
