// RUN: obelisk-opt %s --split-input-file --verify-diagnostics

module {
  obelisk_sim.design @bad_strength_pair_mask_width {
    obelisk_sim.scope.decl 0
    obelisk_sim.net.decl 0 in 0 : !obelisk_sim.logic<2> design
    obelisk_sim.driver.decl 0 in 0 drives 0 : !obelisk_sim.logic<2> design
    obelisk_sim.driver.decl 1 in 0 drives 0 : !obelisk_sim.logic<2> design
    obelisk_sim.func @test(
        %ctx: !obelisk_sim.context {obelisk_sim.capture_kind = 0 : i32})
        attributes {entry_kind = 8 : i32, code_unit_id = 1 : i64} {
      %low = obelisk_sim.context.driver %ctx[0] :
          !obelisk_sim.driver<!obelisk_sim.logic<2>>
      %high = obelisk_sim.context.driver %ctx[1] :
          !obelisk_sim.driver<!obelisk_sim.logic<2>>
      %value = obelisk_sim.logic.constant 0 : i2, 0 : i2 :
          !obelisk_sim.logic<2>
      %bad_mask = arith.constant true
      %delay = obelisk_sim.time.constant 1
      // expected-error @+1 {{all masks must match the paired driver width}}
      obelisk_sim.driver.drive_inertial_path_strength_pair
          %low = %value, %high = %value transition %value active %bad_mask
          masks [%bad_mask, %bad_mask, %bad_mask]
          after [%delay, %delay, %delay]
          site 1 : 0 group 0 of 1 :
          !obelisk_sim.driver<!obelisk_sim.logic<2>>,
          !obelisk_sim.logic<2>,
          !obelisk_sim.driver<!obelisk_sim.logic<2>>,
          !obelisk_sim.logic<2>, !obelisk_sim.logic<2>, i1
      obelisk_sim.return
    }
  }
}

// -----

module {
  obelisk_sim.design @bad_strength_pair_pulse_mask_width {
    obelisk_sim.scope.decl 0
    obelisk_sim.net.decl 0 in 0 : !obelisk_sim.logic<2> design
    obelisk_sim.driver.decl 0 in 0 drives 0 : !obelisk_sim.logic<2> design
    obelisk_sim.driver.decl 1 in 0 drives 0 : !obelisk_sim.logic<2> design
    obelisk_sim.func @test(
        %ctx: !obelisk_sim.context {obelisk_sim.capture_kind = 0 : i32})
        attributes {entry_kind = 8 : i32, code_unit_id = 3 : i64} {
      %low = obelisk_sim.context.driver %ctx[0] :
          !obelisk_sim.driver<!obelisk_sim.logic<2>>
      %high = obelisk_sim.context.driver %ctx[1] :
          !obelisk_sim.driver<!obelisk_sim.logic<2>>
      %value = obelisk_sim.logic.constant 0 : i2, 0 : i2 :
          !obelisk_sim.logic<2>
      %mask = arith.constant 3 : i2
      %bad_pulse_masks = arith.constant 0 : i23
      %delay = obelisk_sim.time.constant 1
      // IEEE 1800-2017 30.7 applies pulse limits to each of the twelve
      // possible four-state output transitions selected by this path.
      // expected-error @+1 {{packed pulse transition masks must have twelve times the paired driver width}}
      obelisk_sim.driver.drive_inertial_path_strength_pair
          %low = %value, %high = %value transition %value active %mask
          masks [%mask, %mask, %mask] after [%delay, %delay, %delay]
          site 3 : 0 group 0 of 1
          pulse_transitions %bad_pulse_masks : i23 :
          !obelisk_sim.driver<!obelisk_sim.logic<2>>,
          !obelisk_sim.logic<2>,
          !obelisk_sim.driver<!obelisk_sim.logic<2>>,
          !obelisk_sim.logic<2>, !obelisk_sim.logic<2>, i2
      obelisk_sim.return
    }
  }
}

// -----

module {
  obelisk_sim.design @bad_strength_pair_pulse_policy {
    obelisk_sim.scope.decl 0
    obelisk_sim.net.decl 0 in 0 : !obelisk_sim.logic<1> design
    obelisk_sim.driver.decl 0 in 0 drives 0 : !obelisk_sim.logic<1> design
    obelisk_sim.driver.decl 1 in 0 drives 0 : !obelisk_sim.logic<1> design
    obelisk_sim.func @test(
        %ctx: !obelisk_sim.context {obelisk_sim.capture_kind = 0 : i32})
        attributes {entry_kind = 8 : i32, code_unit_id = 4 : i64} {
      %low = obelisk_sim.context.driver %ctx[0] :
          !obelisk_sim.driver<!obelisk_sim.logic<1>>
      %high = obelisk_sim.context.driver %ctx[1] :
          !obelisk_sim.driver<!obelisk_sim.logic<1>>
      %value = obelisk_sim.logic.constant false, false :
          !obelisk_sim.logic<1>
      %mask = arith.constant true
      %delay = obelisk_sim.time.constant 1
      // expected-error @+1 {{explicit pulse policy requires packed pulse transition masks}}
      obelisk_sim.driver.drive_inertial_path_strength_pair
          %low = %value, %high = %value transition %value active %mask
          masks [%mask, %mask, %mask] after [%delay, %delay, %delay]
          site 4 : 0 group 0 of 1 {pulse_reject = 1 : i64} :
          !obelisk_sim.driver<!obelisk_sim.logic<1>>,
          !obelisk_sim.logic<1>,
          !obelisk_sim.driver<!obelisk_sim.logic<1>>,
          !obelisk_sim.logic<1>, !obelisk_sim.logic<1>, i1
      obelisk_sim.return
    }
  }
}

// -----

module {
  obelisk_sim.design @bad_strength_pair_pulse_limits {
    obelisk_sim.scope.decl 0
    obelisk_sim.net.decl 0 in 0 : !obelisk_sim.logic<1> design
    obelisk_sim.driver.decl 0 in 0 drives 0 : !obelisk_sim.logic<1> design
    obelisk_sim.driver.decl 1 in 0 drives 0 : !obelisk_sim.logic<1> design
    obelisk_sim.func @one_defaulted(
        %ctx: !obelisk_sim.context {obelisk_sim.capture_kind = 0 : i32})
        attributes {entry_kind = 8 : i32, code_unit_id = 5 : i64} {
      %low = obelisk_sim.context.driver %ctx[0] :
          !obelisk_sim.driver<!obelisk_sim.logic<1>>
      %high = obelisk_sim.context.driver %ctx[1] :
          !obelisk_sim.driver<!obelisk_sim.logic<1>>
      %value = obelisk_sim.logic.constant false, false :
          !obelisk_sim.logic<1>
      %mask = arith.constant true
      %pulse_masks = arith.constant 4095 : i12
      %delay = obelisk_sim.time.constant 1
      // expected-error @+1 {{pulse limits must be defaulted or nonnegative with error not less than reject}}
      obelisk_sim.driver.drive_inertial_path_strength_pair
          %low = %value, %high = %value transition %value active %mask
          masks [%mask, %mask, %mask] after [%delay, %delay, %delay]
          site 5 : 0 group 0 of 1 pulse_transitions %pulse_masks : i12 {
            pulse_reject = 1 : i64
          } : !obelisk_sim.driver<!obelisk_sim.logic<1>>,
          !obelisk_sim.logic<1>,
          !obelisk_sim.driver<!obelisk_sim.logic<1>>,
          !obelisk_sim.logic<1>, !obelisk_sim.logic<1>, i1
      obelisk_sim.return
    }

    obelisk_sim.func @error_below_reject(
        %ctx: !obelisk_sim.context {obelisk_sim.capture_kind = 0 : i32})
        attributes {entry_kind = 8 : i32, code_unit_id = 6 : i64} {
      %low = obelisk_sim.context.driver %ctx[0] :
          !obelisk_sim.driver<!obelisk_sim.logic<1>>
      %high = obelisk_sim.context.driver %ctx[1] :
          !obelisk_sim.driver<!obelisk_sim.logic<1>>
      %value = obelisk_sim.logic.constant false, false :
          !obelisk_sim.logic<1>
      %mask = arith.constant true
      %pulse_masks = arith.constant 4095 : i12
      %delay = obelisk_sim.time.constant 1
      // expected-error @+1 {{pulse limits must be defaulted or nonnegative with error not less than reject}}
      obelisk_sim.driver.drive_inertial_path_strength_pair
          %low = %value, %high = %value transition %value active %mask
          masks [%mask, %mask, %mask] after [%delay, %delay, %delay]
          site 6 : 0 group 0 of 1 pulse_transitions %pulse_masks : i12 {
            pulse_error = 1 : i64, pulse_reject = 2 : i64
          } : !obelisk_sim.driver<!obelisk_sim.logic<1>>,
          !obelisk_sim.logic<1>,
          !obelisk_sim.driver<!obelisk_sim.logic<1>>,
          !obelisk_sim.logic<1>, !obelisk_sim.logic<1>, i1
      obelisk_sim.return
    }
  }
}

// -----

module {
  obelisk_sim.design @bad_strength_pair_group {
    obelisk_sim.scope.decl 0
    obelisk_sim.net.decl 0 in 0 : !obelisk_sim.logic<1> design
    obelisk_sim.driver.decl 0 in 0 drives 0 : !obelisk_sim.logic<1> design
    obelisk_sim.driver.decl 1 in 0 drives 0 : !obelisk_sim.logic<1> design
    obelisk_sim.func @test(
        %ctx: !obelisk_sim.context {obelisk_sim.capture_kind = 0 : i32})
        attributes {entry_kind = 8 : i32, code_unit_id = 2 : i64} {
      %low = obelisk_sim.context.driver %ctx[0] :
          !obelisk_sim.driver<!obelisk_sim.logic<1>>
      %high = obelisk_sim.context.driver %ctx[1] :
          !obelisk_sim.driver<!obelisk_sim.logic<1>>
      %value = obelisk_sim.logic.constant false, false :
          !obelisk_sim.logic<1>
      %mask = arith.constant true
      %delay = obelisk_sim.time.constant 1
      // expected-error @+1 {{group index must be within a nonempty batch}}
      obelisk_sim.driver.drive_inertial_path_strength_pair
          %low = %value, %high = %value transition %value active %mask
          masks [%mask, %mask, %mask] after [%delay, %delay, %delay]
          site 2 : 0 group 1 of 1 :
          !obelisk_sim.driver<!obelisk_sim.logic<1>>,
          !obelisk_sim.logic<1>,
          !obelisk_sim.driver<!obelisk_sim.logic<1>>,
          !obelisk_sim.logic<1>, !obelisk_sim.logic<1>, i1
      obelisk_sim.return
    }
  }
}
