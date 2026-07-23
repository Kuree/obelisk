// RUN: obelisk -emit-sim %s | FileCheck %s

module simulation_computed_wait;
  logic left;
  logic right;
  initial
    wait (left && right)
      left = 0;
endmodule

// CHECK: obelisk_sim.code_unit.decl {{[0-9]+}} in 1 observer
// CHECK: obelisk_sim.observer.bind @observer_{{[0-9]+}}_{{[0-9]+}}
// CHECK-SAME: captures 2 : <i1>
// CHECK: obelisk_sim.suspend.observe
// CHECK-SAME: conditions 0 edges [0] indices [-1]
// CHECK: obelisk_sim.func private @observer_
// CHECK-SAME: -> i1
// CHECK-SAME: entry_kind = 14
