// RUN: obelisk-opt %s --split-input-file --verify-diagnostics

module {
  func.func @mismatched_banks(
      %low: !simulation.driver<!simulation.logic<1>>,
      %high: !simulation.driver<!simulation.logic<2>>) {
    %low_value = simulation.logic.constant 0 : i1, 0 : i1 :
        !simulation.logic<1>
    %high_value = simulation.logic.constant 0 : i2, 0 : i2 :
        !simulation.logic<2>
    %transition = simulation.logic.constant 0 : i1, 0 : i1 :
        !simulation.logic<1>
    %delay = simulation.time.constant 1
    // expected-error @+1 {{requires matching low-bank, high-bank, and transition types}}
    simulation.driver.drive_inertial_strength_pair
        %low = %low_value, %high = %high_value transition %transition
        after[%delay, %delay, %delay] site 1 : 0 :
        !simulation.driver<!simulation.logic<1>>, !simulation.logic<1>,
        !simulation.driver<!simulation.logic<2>>, !simulation.logic<2>,
        !simulation.logic<1>
    return
  }
}

// -----

module {
  func.func @two_state_pair(%low: !simulation.driver<i1>,
                            %high: !simulation.driver<i1>, %value: i1) {
    %delay = simulation.time.constant 1
    // expected-error @+1 {{requires a fixed-width four-state logic element}}
    simulation.driver.drive_inertial_strength_pair
        %low = %value, %high = %value transition %value
        after[%delay, %delay, %delay] site 1 : 0 :
        !simulation.driver<i1>, i1, !simulation.driver<i1>, i1, i1
    return
  }
}
