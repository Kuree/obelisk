// RUN: obelisk -O0 -emit-sim %s -o %t.mlir
// RUN: FileCheck %s < %t.mlir

module assertion_control_lock_zero_query;
  initial begin
    $assertcontrol(1, 2, 1, 0, target);
    $assertcontrol(2, 2, 1, 0, target);
    target: assert (1'b1);
    $finish;
  end
endmodule

// CHECK: obelisk_sim.assert.control
// CHECK-NOT: obelisk_sim.assert.enabled
// CHECK-NOT: obelisk_sim.assert.action_state
