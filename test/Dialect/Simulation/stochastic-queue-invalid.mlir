// RUN: obelisk-opt %s --split-input-file --verify-diagnostics

module {
  func.func @invalid_action(%ctx: !simulation.context) {
    %value = simulation.logic.constant 0 : i32, 0 : i32 :
        !simulation.logic<32>
    // expected-error @+1 {{attribute 'action' failed to satisfy constraint}}
    %primary, %secondary, %status = simulation.stochastic_queue
        %ctx, %value, %value, %value
        {action = 5 : i32, unit_scale = 1 : i64} :
        (!simulation.context, !simulation.logic<32>,
         !simulation.logic<32>, !simulation.logic<32>) ->
        (!simulation.logic<64>, !simulation.logic<64>, i32)
    return
  }
}

// -----

module {
  func.func @zero_scale(%ctx: !simulation.context) {
    %value = simulation.logic.constant 0 : i32, 0 : i32 :
        !simulation.logic<32>
    // expected-error @+1 {{unit_scale must be positive}}
    %primary, %secondary, %status = simulation.stochastic_queue
        %ctx, %value, %value, %value
        {action = #simulation.stochastic_queue_action<initialize>, unit_scale = 0 : i64} :
        (!simulation.context, !simulation.logic<32>,
         !simulation.logic<32>, !simulation.logic<32>) ->
        (!simulation.logic<64>, !simulation.logic<64>, i32)
    return
  }
}

// -----

module {
  func.func @wrong_width(%ctx: !simulation.context) {
    %narrow = simulation.logic.constant 0 : i31, 0 : i31 :
        !simulation.logic<31>
    %value = simulation.logic.constant 0 : i32, 0 : i32 :
        !simulation.logic<32>
    // expected-error @+1 {{id must be !simulation.logic<32>}}
    %primary, %secondary, %status = simulation.stochastic_queue
        %ctx, %narrow, %value, %value
        {action = #simulation.stochastic_queue_action<initialize>, unit_scale = 1 : i64} :
        (!simulation.context, !simulation.logic<31>,
         !simulation.logic<32>, !simulation.logic<32>) ->
        (!simulation.logic<64>, !simulation.logic<64>, i32)
    return
  }
}
