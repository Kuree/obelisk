// RUN: obelisk-sim-standard-api-test %s \
// RUN:   | FileCheck %s --implicit-check-not=obelisk_sim \
// RUN:       --implicit-check-not=unrealized_conversion_cast

// The public packed-value population API and an independent time conversion
// share one TypeConverter and one applyFullConversion transaction.

// CHECK-NOT: obelisk_sim
// CHECK-NOT: unrealized_conversion_cast
// CHECK: func.func @composed() -> (i5, i5, i64)
// CHECK: arith.constant 3 : i5
// CHECK: arith.constant 4 : i5
// CHECK: arith.constant 7 : i64
// CHECK: return

module {
  func.func @composed() -> (!obelisk_sim.logic<5>, !obelisk_sim.time) {
    %logic = obelisk_sim.logic.constant 3 : i5, 4 : i5 : !obelisk_sim.logic<5>
    %time = obelisk_sim.time.constant 7
    return %logic, %time : !obelisk_sim.logic<5>, !obelisk_sim.time
  }
}
