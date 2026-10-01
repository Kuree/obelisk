// RUN: obelisk-opt --convert-obelisk-sim-to-runtime %s | FileCheck %s

// Keep the direct-$realtime classification through the typed runtime ABI.
module {
  func.func @display_realtime(%ctx: !simulation.context, %value: f64) {
    %stdout = arith.constant 1 : i32
    simulation.display %ctx to %stdout(%value) newline = true radix = <decimal>
        flags = [8196]
        {time_multiplier = 1000 : i64, time_precision = -12 : i32} : f64
    return
  }
}

// CHECK-LABEL: func.func @display_realtime
// CHECK: %[[ARG:.*]] = runtime.argument.real %arg1 {is_time = true}
// CHECK: runtime.argument.array(%[[ARG]])
// CHECK: runtime.format.environment
// CHECK-SAME: time_multiplier = 1000 : i64
// CHECK-SAME: time_precision = -12 : i32
// CHECK: runtime.display
