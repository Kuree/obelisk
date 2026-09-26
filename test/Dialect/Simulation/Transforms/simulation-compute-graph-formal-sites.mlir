// RUN: obelisk-opt %s --pass-pipeline='builtin.module(obelisk_sim.design(obelisk-sim-build-compute-graph,obelisk-sim-verify-compute-graph))' | FileCheck %s

module {
  obelisk_sim.design @formal_sites {
    obelisk_sim.code_unit.decl 9000001 in 0 initial hierarchy "test.formal_sites.process.9000001"
    obelisk_sim.scope.decl 0

    // Process formals are legal dynamic handles. Their stage effects retain
    // formal identity, while the shared commit root is conservatively unknown.
    // CHECK-LABEL: obelisk_sim.func @process
    // CHECK-SAME: effect_summary = [#obelisk_sim.effect<effect = nba, resource = storage, target = formal
    // CHECK-SAME: effect = trigger, resource = event, target = formal
    obelisk_sim.func @process(
        %ctx: !obelisk_sim.context {obelisk_sim.capture_kind = 0 : i32},
        %destination: !obelisk_sim.ref<!obelisk_sim.logic<8>> {obelisk_sim.capture_kind = 1 : i32},
        %event: !obelisk_sim.event {obelisk_sim.capture_kind = 1 : i32})
        attributes {entry_kind = 1 : i32, code_unit_id = 9000001 : i64} {
      %value = obelisk_sim.logic.constant 1 : i8, 0 : i8 : !obelisk_sim.logic<8>
      // CHECK: obelisk_sim.nba.enqueue
      // CHECK-SAME: site = #obelisk_sim.nba_site<id = 0
      obelisk_sim.nba.enqueue %value to %destination : (!obelisk_sim.logic<8>, !obelisk_sim.ref<!obelisk_sim.logic<8>>) -> ()
      // CHECK: obelisk_sim.event.trigger
      // CHECK-SAME: site = #obelisk_sim.event_site<id = 0
      obelisk_sim.event.trigger %event nonblocking = true
      obelisk_sim.return
    }
  }
}
