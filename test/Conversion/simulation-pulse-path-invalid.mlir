// RUN: obelisk-opt %s --split-input-file --verify-diagnostics

module {
  simulation.design @bad_pulse_transition_width {
    simulation.scope.decl 0
    simulation.net.decl 0 in 0 : !simulation.logic<1> design
    simulation.driver.decl 0 in 0 drives 0 : !simulation.logic<1> design
    simulation.func @test(
        %ctx: !simulation.context {simulation.capture_kind = 0 : i32})
        attributes {entry_kind = 8 : i32, code_unit_id = 1 : i64} {
      %driver = simulation.context.driver %ctx[0] :
          !simulation.driver<!simulation.logic<1>>
      %value = simulation.logic.constant 0 : i1, 0 : i1 :
          !simulation.logic<1>
      %mask = arith.constant 1 : i1
      %bad_transitions = arith.constant 3 : i2
      %delay = simulation.time.constant 1
      // expected-error @+1 {{packed pulse transition masks must have twelve times the driven width}}
      simulation.driver.drive_inertial_path %driver = %value active %mask
          masks[%mask, %mask, %mask] after[%delay, %delay, %delay]
          site 1 : 0 group 0 of 1 pulse_transitions %bad_transitions : i2
          {pulse_error = 1 : i64, pulse_reject = 1 : i64} :
          !simulation.driver<!simulation.logic<1>>,
          !simulation.logic<1>, i1
      simulation.return
    }
  }
}

// -----

module {
  simulation.design @pulse_policy_without_transition_masks {
    simulation.scope.decl 0
    simulation.net.decl 0 in 0 : !simulation.logic<1> design
    simulation.driver.decl 0 in 0 drives 0 : !simulation.logic<1> design
    simulation.func @test(
        %ctx: !simulation.context {simulation.capture_kind = 0 : i32})
        attributes {entry_kind = 8 : i32, code_unit_id = 3 : i64} {
      %driver = simulation.context.driver %ctx[0] :
          !simulation.driver<!simulation.logic<1>>
      %value = simulation.logic.constant 0 : i1, 0 : i1 :
          !simulation.logic<1>
      %mask = arith.constant 1 : i1
      %delay = simulation.time.constant 1
      // expected-error @+1 {{explicit pulse policy requires packed pulse transition masks}}
      simulation.driver.drive_inertial_path %driver = %value active %mask
          masks[%mask, %mask, %mask] after[%delay, %delay, %delay]
          site 3 : 0 group 0 of 1
          {pulse_error = 1 : i64, pulse_reject = 1 : i64} :
          !simulation.driver<!simulation.logic<1>>,
          !simulation.logic<1>, i1
      simulation.return
    }
  }
}

// -----

module {
  simulation.design @bad_pulse_limit_order {
    simulation.scope.decl 0
    simulation.storage.decl 0 in 0 : !simulation.logic<1> design
    simulation.func @test(
        %ctx: !simulation.context {simulation.capture_kind = 0 : i32})
        attributes {entry_kind = 8 : i32, code_unit_id = 2 : i64} {
      %reference = simulation.context.storage %ctx[0] :
          !simulation.ref<!simulation.logic<1>>
      %value = simulation.logic.constant 0 : i1, 0 : i1 :
          !simulation.logic<1>
      %mask = arith.constant 1 : i1
      %transitions = arith.constant 4095 : i12
      %delay = simulation.time.constant 1
      // expected-error @+1 {{pulse limits must be defaulted or nonnegative with error not less than reject}}
      simulation.ref.store_inertial_path %value to %reference write %mask
          active %mask masks[%mask, %mask, %mask]
          after[%delay, %delay, %delay] site 2 : 0 group 0 of 1
          nonblocking = false pulse_transitions %transitions : i12
          {pulse_error = 2 : i64, pulse_reject = 3 : i64} :
          !simulation.ref<!simulation.logic<1>>,
          !simulation.logic<1>, i1
      simulation.return
    }
  }
}
