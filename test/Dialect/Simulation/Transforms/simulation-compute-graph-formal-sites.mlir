// RUN: obelisk-opt %s --pass-pipeline='builtin.module(simulation.design(obelisk-sim-build-compute-graph,obelisk-sim-verify-compute-graph))' | FileCheck %s

module {
  simulation.design @formal_sites {
    simulation.code_unit.decl 9000001 in 0 initial hierarchy "test.formal_sites.process.9000001"
    simulation.scope.decl 0

    // Process formals are legal dynamic handles. Their stage effects retain
    // formal identity, while the shared commit root is conservatively unknown.
    // CHECK-LABEL: simulation.func @process
    // CHECK-SAME: effect_summary = [#schedule.effect<effect = nba, resource = storage, target = formal
    // CHECK-SAME: effect = trigger, resource = event, target = formal
    simulation.func @process(
        %ctx: !simulation.context {simulation.capture_kind = 0 : i32},
        %destination: !simulation.ref<!simulation.logic<8>> {simulation.capture_kind = 1 : i32},
        %event: !simulation.event {simulation.capture_kind = 1 : i32})
        attributes {entry_kind = 1 : i32, code_unit_id = 9000001 : i64} {
      %value = simulation.logic.constant 1 : i8, 0 : i8 : !simulation.logic<8>
      // CHECK: simulation.nba.enqueue
      // CHECK-SAME: site = #schedule.nba_site<id = 0
      simulation.nba.enqueue %value to %destination : (!simulation.logic<8>, !simulation.ref<!simulation.logic<8>>) -> ()
      // CHECK: simulation.event.trigger
      // CHECK-SAME: site = #schedule.event_site<id = 0
      simulation.event.trigger %event nonblocking = true
      simulation.return
    }
  }
}
