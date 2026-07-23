// RUN: obelisk -emit-sim %s | FileCheck %s

module simulation_computed_iff;
  logic clock;
  logic left;
  logic right;
  initial
    @(posedge clock iff (left && right))
      left = 0;
endmodule

// CHECK: obelisk_sim.code_unit.decl {{[0-9]+}} in 1 observer
// CHECK: obelisk_sim.code_unit.decl {{[0-9]+}} in 1 observer
// CHECK: %[[PRIMARY:.*]] = obelisk_sim.observer.bind
// CHECK-SAME: : <!obelisk_sim.logic<1>>
// CHECK: %[[CONDITION:.*]] = obelisk_sim.observer.bind
// CHECK-SAME: : <i1>
// CHECK: obelisk_sim.suspend.observe %[[PRIMARY]], {{%.*}}, %[[CONDITION]]
// CHECK-SAME: conditions 1 edges [1] indices [0]
