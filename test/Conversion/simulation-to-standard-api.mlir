// RUN: obelisk-sim-standard-api-test %s \
// RUN:   | FileCheck %s --implicit-check-not=simulation \
// RUN:       --implicit-check-not=unrealized_conversion_cast

// The public packed-value population API and an independent time conversion
// share one TypeConverter and one applyFullConversion transaction.

// CHECK-NOT: simulation
// CHECK-NOT: unrealized_conversion_cast
// CHECK: func.func @composed() -> (i5, i5, i64)
// CHECK: arith.constant 3 : i5
// CHECK: arith.constant 4 : i5
// CHECK: arith.constant 7 : i64
// CHECK: return

module {
  func.func @composed() -> (!simulation.logic<5>, !simulation.time) {
    %logic = simulation.logic.constant 3 : i5, 4 : i5 : !simulation.logic<5>
    %time = simulation.time.constant 7
    return %logic, %time : !simulation.logic<5>, !simulation.time
  }
}
