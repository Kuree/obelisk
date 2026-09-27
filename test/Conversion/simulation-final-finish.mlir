// RUN: obelisk-opt %s --convert-obelisk-sim-to-runtime | FileCheck %s

// A shared task can call $finish from either an ordinary or final process, so
// final-phase detection belongs to the common runtime call rather than a
// call-site specialization. This single pass checks that tier-independent ABI.

module {
  func.func @shared_finish(%ctx: !simulation.context) {
    %verbosity = arith.constant 0 : i32
    simulation.finish %ctx, %verbosity
    return
  }
}

// CHECK-LABEL: func.func @shared_finish
// CHECK: %[[VERBOSITY:.*]] = arith.constant 0 : i32
// CHECK: %[[STATUS:.*]] = runtime.finish %{{.*}}, %[[VERBOSITY]]
// CHECK-NEXT: simulation.status.check %[[STATUS]]
// CHECK-NEXT: return
