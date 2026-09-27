// RUN: obelisk-opt %s | FileCheck %s

module {
  simulation.design @clock_condition_observer {
    simulation.code_unit.decl 9000001 in 0 observer hierarchy "test.clock.condition"
    simulation.code_unit.decl 9000002 in 0 always hierarchy "test.clock.wait"
    simulation.scope.decl 0

    simulation.func private @condition(
        %ctx: !simulation.context {simulation.capture_kind = 0 : i32},
        %source: !simulation.ref<!simulation.logic<1>>
            {simulation.capture_kind = 2 : i32}) -> i1
        attributes {entry_kind = 14 : i32, code_unit_id = 9000001 : i64} {
      %value = simulation.ref.load %source :
          !simulation.ref<!simulation.logic<1>> -> !simulation.logic<1>
      %truth = simulation.logic.is_true %value : !simulation.logic<1>
      simulation.return %truth : i1
    }

    simulation.func private @wait(
        %ctx: !simulation.context {simulation.capture_kind = 0 : i32},
        %clock: !simulation.ref<!simulation.logic<1>>
            {simulation.capture_kind = 1 : i32},
        %source: !simulation.ref<!simulation.logic<1>>
            {simulation.capture_kind = 1 : i32})
        attributes {entry_kind = 3 : i32, code_unit_id = 9000002 : i64,
                    domain = 0 : i32, home_region = 8 : i32,
                    simulation.timing_check_coordinator} {
      %condition = simulation.observer.bind @condition
          values(%source, %source : !simulation.ref<!simulation.logic<1>>,
                 !simulation.ref<!simulation.logic<1>>) captures 1 :
          !simulation.observer<i1>
      simulation.suspend.clock_set %clock, %condition conditions 1
          edges [1] indices [0] site 1 to ^done :
          !simulation.ref<!simulation.logic<1>>, !simulation.observer<i1>
    ^done:
      simulation.return
    }
  }
}

// CHECK: %[[CONDITION:.*]] = simulation.observer.bind @condition
// CHECK: simulation.suspend.clock_set %{{.*}}, %[[CONDITION]] conditions 1
// CHECK-SAME: !simulation.observer<i1>
// CHECK-NOT: simulation.suspend.observe
