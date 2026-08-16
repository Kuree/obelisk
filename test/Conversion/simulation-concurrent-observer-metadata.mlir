// RUN: obelisk-opt %s --pass-pipeline='builtin.module(obelisk_sim.design(obelisk-sim-materialize-clocked-samples))' | FileCheck %s

// One source function may contain multiple concurrent assertions. Every
// observer request must survive parallel unit lowering and be finalized; a
// scalar request would silently lose @second.
// CHECK-LABEL: obelisk_sim.func private @requester(
// CHECK-NOT: obelisk_sim.concurrent_cancel_observer_request
// CHECK-LABEL: obelisk_sim.func private @first(
// CHECK-SAME: obelisk_sim.concurrent_cancel_observer
// CHECK-SAME: obelisk_sim.detached_controls
// CHECK-LABEL: obelisk_sim.func private @second(
// CHECK-SAME: obelisk_sim.concurrent_cancel_observer
// CHECK-SAME: obelisk_sim.detached_controls
// CHECK-NOT: obelisk_sim.concurrent_cancel_observer_request

module {
  obelisk_sim.design @observers {
    obelisk_sim.scope.decl 0 hierarchy "top"
    obelisk_sim.code_unit.decl 1 in 0 function hierarchy "top.requester"
    obelisk_sim.code_unit.decl 2 in 0 observer hierarchy "top.first"
    obelisk_sim.code_unit.decl 3 in 0 observer hierarchy "top.second"

    obelisk_sim.func private @requester(
        %context: !obelisk_sim.context {obelisk_sim.capture_kind = 0 : i32})
        attributes {
          code_unit_id = 1 : i64,
          entry_kind = 8 : i32,
          obelisk_sim.concurrent_cancel_observer_request = [@first, @second]
        } {
      obelisk_sim.return
    }

    obelisk_sim.func private @first(
        %context: !obelisk_sim.context {obelisk_sim.capture_kind = 0 : i32})
        -> i1 attributes {
          code_unit_id = 2 : i64,
          entry_kind = 14 : i32,
          obelisk_sim.observer_four_state = false,
          obelisk_sim.observer_result = 2 : i32,
          obelisk_sim.observer_width = 1 : i32
        } {
      %false = arith.constant false
      obelisk_sim.return %false : i1
    }

    obelisk_sim.func private @second(
        %context: !obelisk_sim.context {obelisk_sim.capture_kind = 0 : i32})
        -> i1 attributes {
          code_unit_id = 3 : i64,
          entry_kind = 14 : i32,
          obelisk_sim.observer_four_state = false,
          obelisk_sim.observer_result = 2 : i32,
          obelisk_sim.observer_width = 1 : i32
        } {
      %false = arith.constant false
      obelisk_sim.return %false : i1
    }
  }
}
