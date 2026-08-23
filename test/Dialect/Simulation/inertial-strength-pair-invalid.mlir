// RUN: obelisk-opt %s --split-input-file --verify-diagnostics

module {
  func.func @mismatched_banks(
      %low: !obelisk_sim.driver<!obelisk_sim.logic<1>>,
      %high: !obelisk_sim.driver<!obelisk_sim.logic<2>>) {
    %low_value = obelisk_sim.logic.constant 0 : i1, 0 : i1 :
        !obelisk_sim.logic<1>
    %high_value = obelisk_sim.logic.constant 0 : i2, 0 : i2 :
        !obelisk_sim.logic<2>
    %transition = obelisk_sim.logic.constant 0 : i1, 0 : i1 :
        !obelisk_sim.logic<1>
    %delay = obelisk_sim.time.constant 1
    // expected-error @+1 {{requires matching low-bank, high-bank, and transition types}}
    obelisk_sim.driver.drive_inertial_strength_pair
        %low = %low_value, %high = %high_value transition %transition
        after[%delay, %delay, %delay] site 1 : 0 :
        !obelisk_sim.driver<!obelisk_sim.logic<1>>, !obelisk_sim.logic<1>,
        !obelisk_sim.driver<!obelisk_sim.logic<2>>, !obelisk_sim.logic<2>,
        !obelisk_sim.logic<1>
    return
  }
}

// -----

module {
  func.func @two_state_pair(%low: !obelisk_sim.driver<i1>,
                            %high: !obelisk_sim.driver<i1>, %value: i1) {
    %delay = obelisk_sim.time.constant 1
    // expected-error @+1 {{requires a fixed-width four-state logic element}}
    obelisk_sim.driver.drive_inertial_strength_pair
        %low = %value, %high = %value transition %value
        after[%delay, %delay, %delay] site 1 : 0 :
        !obelisk_sim.driver<i1>, i1, !obelisk_sim.driver<i1>, i1, i1
    return
  }
}
