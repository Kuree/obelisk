// RUN: obelisk-opt %s --convert-obelisk-sim-to-runtime | FileCheck %s

// A shared task can call $finish from either an ordinary or final process, so
// final-phase detection belongs to the common runtime call rather than a
// call-site specialization. This single pass checks that tier-independent ABI.

module {
  func.func @shared_finish(%ctx: !obelisk_sim.context) {
    %verbosity = arith.constant 0 : i32
    obelisk_sim.finish %ctx, %verbosity
    return
  }
}

// CHECK-LABEL: func.func @shared_finish
// CHECK: %[[VERBOSITY:.*]] = arith.constant 0 : i32
// CHECK: %[[STATUS:.*]] = obelisk_rt.finish %{{.*}}, %[[VERBOSITY]]
// CHECK-NEXT: obelisk_sim.status.check %[[STATUS]]
// CHECK-NEXT: return
