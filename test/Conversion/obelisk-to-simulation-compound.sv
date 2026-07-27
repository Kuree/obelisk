// RUN: obelisk -O0 -emit-sim %s | FileCheck %s

module simulation_compound;
  logic [7:0] value;
  initial value += 1;
endmodule

// The semantic lvalue-reference placeholder is resolved to the value loaded
// from the captured destination, then ordinary binary conversion and writeback
// lowering takes over.
// CHECK: %[[OLD:.*]] = obelisk_sim.ref.load [[DEST:%[a-zA-Z0-9]+]]
// CHECK: %[[FLAT:.*]] = obelisk_sim.packed.flatten %[[OLD]]
// CHECK: %[[WIDE:.*]] = obelisk_sim.logic.resize %[[FLAT]]
// CHECK: %[[SUM:.*]] = obelisk_sim.logic.binary add %[[WIDE]]
// CHECK: %[[NARROW:.*]] = obelisk_sim.logic.resize %[[SUM]]
// CHECK: %[[STORED:.*]] = obelisk_sim.packed.unflatten %[[NARROW]]
// CHECK: obelisk_sim.ref.store %[[STORED]] to [[DEST]]
