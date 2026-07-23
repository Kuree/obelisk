// RUN: obelisk -emit-sim %s | FileCheck %s

module unsupported_event_iff;
  logic clk;
  logic enable;
  logic value;
  always @(posedge clk iff enable)
    value = enable;
endmodule

// CHECK: obelisk_sim.suspend.edge_iff posedge
// CHECK: obelisk_sim.ref.store
// CHECK-NOT: cf.cond_br
