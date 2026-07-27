// RUN: obelisk -emit-sim %s | FileCheck %s

module logical_short_circuit(
    input logic lhs,
    input logic rhs,
    output logic result);
  int calls;

  function automatic logic side_effect(input logic value);
    calls++;
    return value;
  endfunction

  always_comb
    result = lhs && side_effect(rhs);
endmodule

// The controlling known-false value bypasses the block containing the
// right-hand function body.  Both paths pass a logic<1> result to the merge.
// CHECK-LABEL: obelisk_sim.func {{.*}}@unit_
// CHECK: %[[LHS:.*]] = obelisk_sim.ref.load
// CHECK: %[[LHS_PRED:.*]] = obelisk_sim.logic.reduction or %[[LHS]]
// CHECK: %[[FALSE:.*]] = obelisk_sim.logic.constant false, false
// CHECK: %[[CONTROL:.*]] = obelisk_sim.logic.compare case_eq %[[LHS_PRED]], %[[FALSE]]
// CHECK: %[[SHORT_RESULT:.*]] = obelisk_sim.logic.constant false, false
// CHECK: cf.cond_br %[[CONTROL]], ^[[MERGE:.*]](%[[SHORT_RESULT]] : !obelisk_sim.logic<1>), ^[[RHS:.*]]
// CHECK: ^[[RHS]]:
// CHECK: %[[RHS_VALUE:.*]] = obelisk_sim.ref.load
// CHECK: %[[CALLS:.*]] = obelisk_sim.ref.load
// CHECK: %[[NEXT:.*]] = arith.addi %[[CALLS]]
// CHECK: obelisk_sim.ref.store %[[NEXT]]
// CHECK: ^[[MERGE]](%[[RESULT:.*]]: !obelisk_sim.logic<1>):
// CHECK: obelisk_sim.ref.store %[[RESULT]]
// CHECK: %[[RHS_PRED:.*]] = obelisk_sim.logic.reduction or %[[RHS_VALUE]]
// CHECK: %[[LOGICAL:.*]] = obelisk_sim.logic.logical and %[[LHS_PRED]], %[[RHS_PRED]]
// CHECK: cf.br ^[[MERGE]](%[[LOGICAL]] : !obelisk_sim.logic<1>)
