// RUN: obelisk -O0 -emit-sim %s -o %t.mlir
// RUN: FileCheck %s < %t.mlir

module assertion_control_zero_overhead;
  initial begin
    assert (1'b1);
    labeled: assert #0 (1'b1);
    #1;
    $finish;
  end
endmodule

// CHECK-NOT: obelisk_sim.assert.enabled
// CHECK-NOT: obelisk_sim.assert.action_state
// CHECK: obelisk_sim.assert.deferred_enqueue
// CHECK-NOT: obelisk_sim.assert.enabled
// CHECK-NOT: obelisk_sim.assert.action_state
