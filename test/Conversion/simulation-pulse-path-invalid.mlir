// RUN: obelisk-opt %s --split-input-file --verify-diagnostics

module {
  obelisk_sim.design @bad_pulse_transition_width {
    obelisk_sim.scope.decl 0
    obelisk_sim.net.decl 0 in 0 : !obelisk_sim.logic<1> design
    obelisk_sim.driver.decl 0 in 0 drives 0 : !obelisk_sim.logic<1> design
    obelisk_sim.func @test(
        %ctx: !obelisk_sim.context {obelisk_sim.capture_kind = 0 : i32})
        attributes {entry_kind = 8 : i32, code_unit_id = 1 : i64} {
      %driver = obelisk_sim.context.driver %ctx[0] :
          !obelisk_sim.driver<!obelisk_sim.logic<1>>
      %value = obelisk_sim.logic.constant 0 : i1, 0 : i1 :
          !obelisk_sim.logic<1>
      %mask = arith.constant 1 : i1
      %bad_transitions = arith.constant 3 : i2
      %delay = obelisk_sim.time.constant 1
      // expected-error @+1 {{packed pulse transition masks must have twelve times the driven width}}
      obelisk_sim.driver.drive_inertial_path %driver = %value active %mask
          masks[%mask, %mask, %mask] after[%delay, %delay, %delay]
          site 1 : 0 group 0 of 1 pulse_transitions %bad_transitions : i2
          {pulse_error = 1 : i64, pulse_reject = 1 : i64} :
          !obelisk_sim.driver<!obelisk_sim.logic<1>>,
          !obelisk_sim.logic<1>, i1
      obelisk_sim.return
    }
  }
}

// -----

module {
  obelisk_sim.design @pulse_policy_without_transition_masks {
    obelisk_sim.scope.decl 0
    obelisk_sim.net.decl 0 in 0 : !obelisk_sim.logic<1> design
    obelisk_sim.driver.decl 0 in 0 drives 0 : !obelisk_sim.logic<1> design
    obelisk_sim.func @test(
        %ctx: !obelisk_sim.context {obelisk_sim.capture_kind = 0 : i32})
        attributes {entry_kind = 8 : i32, code_unit_id = 3 : i64} {
      %driver = obelisk_sim.context.driver %ctx[0] :
          !obelisk_sim.driver<!obelisk_sim.logic<1>>
      %value = obelisk_sim.logic.constant 0 : i1, 0 : i1 :
          !obelisk_sim.logic<1>
      %mask = arith.constant 1 : i1
      %delay = obelisk_sim.time.constant 1
      // expected-error @+1 {{explicit pulse policy requires packed pulse transition masks}}
      obelisk_sim.driver.drive_inertial_path %driver = %value active %mask
          masks[%mask, %mask, %mask] after[%delay, %delay, %delay]
          site 3 : 0 group 0 of 1
          {pulse_error = 1 : i64, pulse_reject = 1 : i64} :
          !obelisk_sim.driver<!obelisk_sim.logic<1>>,
          !obelisk_sim.logic<1>, i1
      obelisk_sim.return
    }
  }
}

// -----

module {
  obelisk_sim.design @bad_pulse_limit_order {
    obelisk_sim.scope.decl 0
    obelisk_sim.storage.decl 0 in 0 : !obelisk_sim.logic<1> design
    obelisk_sim.func @test(
        %ctx: !obelisk_sim.context {obelisk_sim.capture_kind = 0 : i32})
        attributes {entry_kind = 8 : i32, code_unit_id = 2 : i64} {
      %reference = obelisk_sim.context.storage %ctx[0] :
          !obelisk_sim.ref<!obelisk_sim.logic<1>>
      %value = obelisk_sim.logic.constant 0 : i1, 0 : i1 :
          !obelisk_sim.logic<1>
      %mask = arith.constant 1 : i1
      %transitions = arith.constant 4095 : i12
      %delay = obelisk_sim.time.constant 1
      // expected-error @+1 {{pulse limits must be defaulted or nonnegative with error not less than reject}}
      obelisk_sim.ref.store_inertial_path %value to %reference write %mask
          active %mask masks[%mask, %mask, %mask]
          after[%delay, %delay, %delay] site 2 : 0 group 0 of 1
          nonblocking = false pulse_transitions %transitions : i12
          {pulse_error = 2 : i64, pulse_reject = 3 : i64} :
          !obelisk_sim.ref<!obelisk_sim.logic<1>>,
          !obelisk_sim.logic<1>, i1
      obelisk_sim.return
    }
  }
}
