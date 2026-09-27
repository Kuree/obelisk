// RUN: obelisk-opt %s --split-input-file --verify-diagnostics

module {
  simulation.design @bad_strength_pair_mask_width {
    simulation.scope.decl 0
    simulation.net.decl 0 in 0 : !simulation.logic<2> design
    simulation.driver.decl 0 in 0 drives 0 : !simulation.logic<2> design
    simulation.driver.decl 1 in 0 drives 0 : !simulation.logic<2> design
    simulation.func @test(
        %ctx: !simulation.context {simulation.capture_kind = 0 : i32})
        attributes {entry_kind = 8 : i32, code_unit_id = 1 : i64} {
      %low = simulation.context.driver %ctx[0] :
          !simulation.driver<!simulation.logic<2>>
      %high = simulation.context.driver %ctx[1] :
          !simulation.driver<!simulation.logic<2>>
      %value = simulation.logic.constant 0 : i2, 0 : i2 :
          !simulation.logic<2>
      %bad_mask = arith.constant true
      %delay = simulation.time.constant 1
      // expected-error @+1 {{all masks must match the paired driver width}}
      simulation.driver.drive_inertial_path_strength_pair
          %low = %value, %high = %value transition %value active %bad_mask
          masks [%bad_mask, %bad_mask, %bad_mask]
          after [%delay, %delay, %delay]
          site 1 : 0 group 0 of 1 :
          !simulation.driver<!simulation.logic<2>>,
          !simulation.logic<2>,
          !simulation.driver<!simulation.logic<2>>,
          !simulation.logic<2>, !simulation.logic<2>, i1
      simulation.return
    }
  }
}

// -----

module {
  simulation.design @bad_strength_pair_pulse_mask_width {
    simulation.scope.decl 0
    simulation.net.decl 0 in 0 : !simulation.logic<2> design
    simulation.driver.decl 0 in 0 drives 0 : !simulation.logic<2> design
    simulation.driver.decl 1 in 0 drives 0 : !simulation.logic<2> design
    simulation.func @test(
        %ctx: !simulation.context {simulation.capture_kind = 0 : i32})
        attributes {entry_kind = 8 : i32, code_unit_id = 3 : i64} {
      %low = simulation.context.driver %ctx[0] :
          !simulation.driver<!simulation.logic<2>>
      %high = simulation.context.driver %ctx[1] :
          !simulation.driver<!simulation.logic<2>>
      %value = simulation.logic.constant 0 : i2, 0 : i2 :
          !simulation.logic<2>
      %mask = arith.constant 3 : i2
      %bad_pulse_masks = arith.constant 0 : i23
      %delay = simulation.time.constant 1
      // IEEE 1800-2017 30.7 applies pulse limits to each of the twelve
      // possible four-state output transitions selected by this path.
      // expected-error @+1 {{packed pulse transition masks must have twelve times the paired driver width}}
      simulation.driver.drive_inertial_path_strength_pair
          %low = %value, %high = %value transition %value active %mask
          masks [%mask, %mask, %mask] after [%delay, %delay, %delay]
          site 3 : 0 group 0 of 1
          pulse_transitions %bad_pulse_masks : i23 :
          !simulation.driver<!simulation.logic<2>>,
          !simulation.logic<2>,
          !simulation.driver<!simulation.logic<2>>,
          !simulation.logic<2>, !simulation.logic<2>, i2
      simulation.return
    }
  }
}

// -----

module {
  simulation.design @bad_strength_pair_pulse_policy {
    simulation.scope.decl 0
    simulation.net.decl 0 in 0 : !simulation.logic<1> design
    simulation.driver.decl 0 in 0 drives 0 : !simulation.logic<1> design
    simulation.driver.decl 1 in 0 drives 0 : !simulation.logic<1> design
    simulation.func @test(
        %ctx: !simulation.context {simulation.capture_kind = 0 : i32})
        attributes {entry_kind = 8 : i32, code_unit_id = 4 : i64} {
      %low = simulation.context.driver %ctx[0] :
          !simulation.driver<!simulation.logic<1>>
      %high = simulation.context.driver %ctx[1] :
          !simulation.driver<!simulation.logic<1>>
      %value = simulation.logic.constant false, false :
          !simulation.logic<1>
      %mask = arith.constant true
      %delay = simulation.time.constant 1
      // expected-error @+1 {{explicit pulse policy requires packed pulse transition masks}}
      simulation.driver.drive_inertial_path_strength_pair
          %low = %value, %high = %value transition %value active %mask
          masks [%mask, %mask, %mask] after [%delay, %delay, %delay]
          site 4 : 0 group 0 of 1 {pulse_reject = 1 : i64} :
          !simulation.driver<!simulation.logic<1>>,
          !simulation.logic<1>,
          !simulation.driver<!simulation.logic<1>>,
          !simulation.logic<1>, !simulation.logic<1>, i1
      simulation.return
    }
  }
}

// -----

module {
  simulation.design @bad_strength_pair_pulse_limits {
    simulation.scope.decl 0
    simulation.net.decl 0 in 0 : !simulation.logic<1> design
    simulation.driver.decl 0 in 0 drives 0 : !simulation.logic<1> design
    simulation.driver.decl 1 in 0 drives 0 : !simulation.logic<1> design
    simulation.func @one_defaulted(
        %ctx: !simulation.context {simulation.capture_kind = 0 : i32})
        attributes {entry_kind = 8 : i32, code_unit_id = 5 : i64} {
      %low = simulation.context.driver %ctx[0] :
          !simulation.driver<!simulation.logic<1>>
      %high = simulation.context.driver %ctx[1] :
          !simulation.driver<!simulation.logic<1>>
      %value = simulation.logic.constant false, false :
          !simulation.logic<1>
      %mask = arith.constant true
      %pulse_masks = arith.constant 4095 : i12
      %delay = simulation.time.constant 1
      // expected-error @+1 {{pulse limits must be defaulted or nonnegative with error not less than reject}}
      simulation.driver.drive_inertial_path_strength_pair
          %low = %value, %high = %value transition %value active %mask
          masks [%mask, %mask, %mask] after [%delay, %delay, %delay]
          site 5 : 0 group 0 of 1 pulse_transitions %pulse_masks : i12 {
            pulse_reject = 1 : i64
          } : !simulation.driver<!simulation.logic<1>>,
          !simulation.logic<1>,
          !simulation.driver<!simulation.logic<1>>,
          !simulation.logic<1>, !simulation.logic<1>, i1
      simulation.return
    }

    simulation.func @error_below_reject(
        %ctx: !simulation.context {simulation.capture_kind = 0 : i32})
        attributes {entry_kind = 8 : i32, code_unit_id = 6 : i64} {
      %low = simulation.context.driver %ctx[0] :
          !simulation.driver<!simulation.logic<1>>
      %high = simulation.context.driver %ctx[1] :
          !simulation.driver<!simulation.logic<1>>
      %value = simulation.logic.constant false, false :
          !simulation.logic<1>
      %mask = arith.constant true
      %pulse_masks = arith.constant 4095 : i12
      %delay = simulation.time.constant 1
      // expected-error @+1 {{pulse limits must be defaulted or nonnegative with error not less than reject}}
      simulation.driver.drive_inertial_path_strength_pair
          %low = %value, %high = %value transition %value active %mask
          masks [%mask, %mask, %mask] after [%delay, %delay, %delay]
          site 6 : 0 group 0 of 1 pulse_transitions %pulse_masks : i12 {
            pulse_error = 1 : i64, pulse_reject = 2 : i64
          } : !simulation.driver<!simulation.logic<1>>,
          !simulation.logic<1>,
          !simulation.driver<!simulation.logic<1>>,
          !simulation.logic<1>, !simulation.logic<1>, i1
      simulation.return
    }
  }
}

// -----

module {
  simulation.design @bad_strength_pair_group {
    simulation.scope.decl 0
    simulation.net.decl 0 in 0 : !simulation.logic<1> design
    simulation.driver.decl 0 in 0 drives 0 : !simulation.logic<1> design
    simulation.driver.decl 1 in 0 drives 0 : !simulation.logic<1> design
    simulation.func @test(
        %ctx: !simulation.context {simulation.capture_kind = 0 : i32})
        attributes {entry_kind = 8 : i32, code_unit_id = 2 : i64} {
      %low = simulation.context.driver %ctx[0] :
          !simulation.driver<!simulation.logic<1>>
      %high = simulation.context.driver %ctx[1] :
          !simulation.driver<!simulation.logic<1>>
      %value = simulation.logic.constant false, false :
          !simulation.logic<1>
      %mask = arith.constant true
      %delay = simulation.time.constant 1
      // expected-error @+1 {{group index must be within a nonempty batch}}
      simulation.driver.drive_inertial_path_strength_pair
          %low = %value, %high = %value transition %value active %mask
          masks [%mask, %mask, %mask] after [%delay, %delay, %delay]
          site 2 : 0 group 1 of 1 :
          !simulation.driver<!simulation.logic<1>>,
          !simulation.logic<1>,
          !simulation.driver<!simulation.logic<1>>,
          !simulation.logic<1>, !simulation.logic<1>, i1
      simulation.return
    }
  }
}
