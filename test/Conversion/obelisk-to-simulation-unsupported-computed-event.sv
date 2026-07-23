// RUN: obelisk -emit-sim %s | FileCheck %s

module unsupported_computed_event;
  logic lhs;
  logic rhs;
  logic result;

  always @(posedge (lhs & rhs))
    result <= 1'b1;
endmodule

// CHECK: obelisk_sim.observer.bind
// CHECK: obelisk_sim.suspend.observe
// CHECK-SAME: conditions 0 edges [1] indices [-1]
// CHECK: obelisk_sim.func private @observer_
// CHECK: obelisk_sim.logic.binary and
