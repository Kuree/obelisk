// RUN: obelisk -emit-sim %s | FileCheck %s

module simulation_event_selection;
  logic [7:0] values;
  int index;
  initial
    @(values[index])
      values = '0;
endmodule

// CHECK: obelisk_sim.observer.bind
// CHECK-SAME: captures 2 : <!obelisk_sim.logic<1>>
// CHECK: obelisk_sim.suspend.observe
// CHECK-SAME: conditions 0 edges [0] indices [-1]
// CHECK: obelisk_sim.func private @observer_
// CHECK: obelisk_sim.array.extract_dynamic
