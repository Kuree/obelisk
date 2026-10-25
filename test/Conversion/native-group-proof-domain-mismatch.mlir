// RUN: obelisk-opt %s --obelisk-materialize-native-eval-groups --verify-diagnostics

// Even well-typed references must not differ between a candidate and its
// ordinary original. Refined children intentionally have neither reference.
#first = #schedule.group_proof_dependency<domain = 0 : i64, members = [0], ranges = [0, 1]>
#second = #schedule.group_proof_dependency<domain = 1 : i64, members = [0], ranges = [0, 1]>
module {
  llvm.mlir.global internal @__obelisk_eval_promotion_pending_mask_v1(dense<1> : tensor<1xi64>) : !llvm.array<1 x i64>
  llvm.func @group() attributes {schedule.eval.ranked_members = array<i32: 0>, schedule.eval.group_proof_domain = #first} {
    llvm.return
  }
  // expected-error@+1 {{candidate and original group proof domains differ}}
  llvm.func @candidate() attributes {schedule.eval.ranked_members = array<i32: 0>, schedule.eval.group_proof_domain = #second, schedule.eval.dataflow_candidate = @group} {
    llvm.return
  }
}
