// RUN: obelisk-opt %s | FileCheck %s

module {
  obelisk_sim.design @clock_condition_observer {
    obelisk_sim.code_unit.decl 9000001 in 0 observer hierarchy "test.clock.condition"
    obelisk_sim.code_unit.decl 9000002 in 0 always hierarchy "test.clock.wait"
    obelisk_sim.scope.decl 0

    obelisk_sim.func private @condition(
        %ctx: !obelisk_sim.context {obelisk_sim.capture_kind = 0 : i32},
        %source: !obelisk_sim.ref<!obelisk_sim.logic<1>>
            {obelisk_sim.capture_kind = 2 : i32}) -> i1
        attributes {entry_kind = 14 : i32, code_unit_id = 9000001 : i64} {
      %value = obelisk_sim.ref.load %source :
          !obelisk_sim.ref<!obelisk_sim.logic<1>> -> !obelisk_sim.logic<1>
      %truth = obelisk_sim.logic.is_true %value : !obelisk_sim.logic<1>
      obelisk_sim.return %truth : i1
    }

    obelisk_sim.func private @wait(
        %ctx: !obelisk_sim.context {obelisk_sim.capture_kind = 0 : i32},
        %clock: !obelisk_sim.ref<!obelisk_sim.logic<1>>
            {obelisk_sim.capture_kind = 1 : i32},
        %source: !obelisk_sim.ref<!obelisk_sim.logic<1>>
            {obelisk_sim.capture_kind = 1 : i32})
        attributes {entry_kind = 3 : i32, code_unit_id = 9000002 : i64,
                    domain = 0 : i32, home_region = 8 : i32,
                    obelisk_sim.timing_check_coordinator} {
      %condition = obelisk_sim.observer.bind @condition
          values(%source, %source : !obelisk_sim.ref<!obelisk_sim.logic<1>>,
                 !obelisk_sim.ref<!obelisk_sim.logic<1>>) captures 1 :
          !obelisk_sim.observer<i1>
      obelisk_sim.suspend.clock_set %clock, %condition conditions 1
          edges [1] indices [0] site 1 to ^done :
          !obelisk_sim.ref<!obelisk_sim.logic<1>>, !obelisk_sim.observer<i1>
    ^done:
      obelisk_sim.return
    }
  }
}

// CHECK: %[[CONDITION:.*]] = obelisk_sim.observer.bind @condition
// CHECK: obelisk_sim.suspend.clock_set %{{.*}}, %[[CONDITION]] conditions 1
// CHECK-SAME: !obelisk_sim.observer<i1>
// CHECK-NOT: obelisk_sim.suspend.observe
