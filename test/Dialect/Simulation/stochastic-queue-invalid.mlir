// RUN: obelisk-opt %s --split-input-file --verify-diagnostics

module {
  func.func @invalid_action(%ctx: !obelisk_sim.context) {
    %value = obelisk_sim.logic.constant 0 : i32, 0 : i32 :
        !obelisk_sim.logic<32>
    // expected-error @+1 {{action must select initialize, add, remove, full, or exam}}
    %primary, %secondary, %status = obelisk_sim.stochastic_queue
        %ctx, %value, %value, %value
        {action = 5 : i32, unit_scale = 1 : i64} :
        (!obelisk_sim.context, !obelisk_sim.logic<32>,
         !obelisk_sim.logic<32>, !obelisk_sim.logic<32>) ->
        (!obelisk_sim.logic<64>, !obelisk_sim.logic<64>, i32)
    return
  }
}

// -----

module {
  func.func @zero_scale(%ctx: !obelisk_sim.context) {
    %value = obelisk_sim.logic.constant 0 : i32, 0 : i32 :
        !obelisk_sim.logic<32>
    // expected-error @+1 {{unit_scale must be positive}}
    %primary, %secondary, %status = obelisk_sim.stochastic_queue
        %ctx, %value, %value, %value
        {action = 0 : i32, unit_scale = 0 : i64} :
        (!obelisk_sim.context, !obelisk_sim.logic<32>,
         !obelisk_sim.logic<32>, !obelisk_sim.logic<32>) ->
        (!obelisk_sim.logic<64>, !obelisk_sim.logic<64>, i32)
    return
  }
}

// -----

module {
  func.func @wrong_width(%ctx: !obelisk_sim.context) {
    %narrow = obelisk_sim.logic.constant 0 : i31, 0 : i31 :
        !obelisk_sim.logic<31>
    %value = obelisk_sim.logic.constant 0 : i32, 0 : i32 :
        !obelisk_sim.logic<32>
    // expected-error @+1 {{id must be !obelisk_sim.logic<32>}}
    %primary, %secondary, %status = obelisk_sim.stochastic_queue
        %ctx, %narrow, %value, %value
        {action = 0 : i32, unit_scale = 1 : i64} :
        (!obelisk_sim.context, !obelisk_sim.logic<31>,
         !obelisk_sim.logic<32>, !obelisk_sim.logic<32>) ->
        (!obelisk_sim.logic<64>, !obelisk_sim.logic<64>, i32)
    return
  }
}
