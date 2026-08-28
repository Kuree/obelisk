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
