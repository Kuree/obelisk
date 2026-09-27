// RUN: obelisk-opt %s --pass-pipeline='builtin.module(simulation.design(obelisk-sim-materialize-clocked-samples))' | FileCheck %s

// One source function may contain multiple concurrent assertions. Every
// observer request must survive parallel unit lowering and be finalized; a
// scalar request would silently lose @second.
// CHECK-LABEL: simulation.func private @requester(
// CHECK-NOT: schedule.concurrent_cancel_observer_request
// CHECK-LABEL: simulation.func private @first(
// CHECK-SAME: schedule.concurrent_cancel_observer
// CHECK-SAME: schedule.detached_controls
// CHECK-LABEL: simulation.func private @second(
// CHECK-SAME: schedule.concurrent_cancel_observer
// CHECK-SAME: schedule.detached_controls
// CHECK-NOT: schedule.concurrent_cancel_observer_request

module {
  simulation.design @observers {
    simulation.scope.decl 0 hierarchy "top"
    simulation.code_unit.decl 1 in 0 function hierarchy "top.requester"
    simulation.code_unit.decl 2 in 0 observer hierarchy "top.first"
    simulation.code_unit.decl 3 in 0 observer hierarchy "top.second"

    simulation.func private @requester(
        %context: !simulation.context {simulation.capture_kind = 0 : i32})
        attributes {
          code_unit_id = 1 : i64,
          entry_kind = 8 : i32,
          schedule.concurrent_cancel_observer_request = [@first, @second]
        } {
      simulation.return
    }

    simulation.func private @first(
        %context: !simulation.context {simulation.capture_kind = 0 : i32})
        -> i1 attributes {
          code_unit_id = 2 : i64,
          entry_kind = 14 : i32,
          schedule.observer_four_state = false,
          simulation.observer_result = 2 : i32,
          schedule.observer_width = 1 : i32
        } {
      %false = arith.constant false
      simulation.return %false : i1
    }

    simulation.func private @second(
        %context: !simulation.context {simulation.capture_kind = 0 : i32})
        -> i1 attributes {
          code_unit_id = 3 : i64,
          entry_kind = 14 : i32,
          schedule.observer_four_state = false,
          simulation.observer_result = 2 : i32,
          schedule.observer_width = 1 : i32
        } {
      %false = arith.constant false
      simulation.return %false : i1
    }
  }
}
