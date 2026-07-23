// RUN: obelisk -O0 -emit-sim %s | FileCheck %s

module simulation_intra_assignment_event;
  logic clk;
  logic lhs;
  logic rhs;

  initial begin
    lhs = @(posedge clk) rhs;
    lhs = repeat (2) @(posedge clk) rhs;
  end
endmodule

// The RHS is loaded before each wait, while the blocking destination store is
// emitted only in its continuation.
// CHECK: %[[EVENT_RHS:.*]] = obelisk_sim.ref.load
// CHECK: obelisk_sim.suspend.edge posedge {{.*}} to ^[[EVENT_COMMIT:.*]](%[[EVENT_RHS]]
// CHECK: ^[[EVENT_COMMIT]](%[[EVENT_COMMIT_RHS:.*]]: !obelisk_sim.logic<1>):
// CHECK: obelisk_sim.ref.store %[[EVENT_COMMIT_RHS]]
// CHECK: %[[REPEAT_RHS:.*]] = obelisk_sim.ref.load
// CHECK: cf.br ^[[REPEAT_HEADER:.*]](%{{.*}}, %[[REPEAT_RHS]]
// CHECK: ^[[REPEAT_COMMIT:.*]](%[[REPEAT_COMMIT_RHS:.*]]: !obelisk_sim.logic<1>):
// CHECK: obelisk_sim.ref.store %[[REPEAT_COMMIT_RHS]]
// CHECK: ^[[REPEAT_HEADER]](%[[REPEAT_COUNT:.*]]: i64, %[[REPEAT_VALUE:.*]]: !obelisk_sim.logic<1>):
// CHECK: obelisk_sim.suspend.edge posedge {{.*}} to ^[[REPEAT_STEP:.*]](%[[REPEAT_COUNT]], %[[REPEAT_VALUE]]
// CHECK: ^[[REPEAT_STEP]](%[[STEP_COUNT:.*]]: i64, %[[STEP_VALUE:.*]]: !obelisk_sim.logic<1>):
// CHECK: %[[NEXT_COUNT:.*]] = arith.subi %[[STEP_COUNT]]
// CHECK: %[[CONTINUE:.*]] = arith.cmpi sgt, %[[NEXT_COUNT]]
// CHECK: cf.cond_br %[[CONTINUE]], ^[[REPEAT_HEADER]](%[[NEXT_COUNT]], %[[STEP_VALUE]] {{.*}}), ^[[REPEAT_COMMIT]](%[[STEP_VALUE]]
