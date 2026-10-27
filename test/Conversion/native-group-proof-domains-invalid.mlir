// RUN: talos-opt %s --split-input-file --materialize-native-group-promotion-readiness --verify-diagnostics

// Dense domain IDs are independent of owner numbers.
#member = #schedule.kernel_proof_dependency<latch = 0 : i64, pendingBit = 0 : i64, ranges = [0, 1, 9, 1]>
#domain = #schedule.group_proof_dependency<domain = 2 : i64, members = [0], ranges = [0, 1, 9, 1]>

module attributes {runtime.state_bits = 128 : i64} {
  llvm.mlir.global internal @__talos_state_unknown(dense<0> : tensor<3xi64>) : !llvm.array<3 x i64>
  llvm.mlir.global internal @__talos_eval_kernel_promotion_latched_v1(dense<0> : tensor<2xi8>) {schedule.eval.kernel_proof_dependencies = [#member]} : !llvm.array<2 x i8>
  llvm.mlir.global internal @__talos_eval_promotion_pending_mask_v1(dense<1> : tensor<1xi64>) : !llvm.array<1 x i64>
  // expected-error@+1 {{group proof has invalid dense domain identity}}
  llvm.mlir.global internal @__talos_eval_group_promotion_latched_v1(dense<0> : tensor<1xi8>) {schedule.eval.group_proof_dependencies = [#domain]} : !llvm.array<1 x i8>

}

// -----

// Union construction must not fill holes.
#member = #schedule.kernel_proof_dependency<latch = 0 : i64, pendingBit = 0 : i64, ranges = [0, 1, 9, 1]>
#domain = #schedule.group_proof_dependency<domain = 0 : i64, members = [0], ranges = [0, 10]>

module attributes {runtime.state_bits = 128 : i64} {
  llvm.mlir.global internal @__talos_state_unknown(dense<0> : tensor<3xi64>) : !llvm.array<3 x i64>
  llvm.mlir.global internal @__talos_eval_kernel_promotion_latched_v1(dense<0> : tensor<2xi8>) {schedule.eval.kernel_proof_dependencies = [#member]} : !llvm.array<2 x i8>
  llvm.mlir.global internal @__talos_eval_promotion_pending_mask_v1(dense<1> : tensor<1xi64>) : !llvm.array<1 x i64>
  // expected-error@+1 {{group proof ranges must equal the exact member certificate union}}
  llvm.mlir.global internal @__talos_eval_group_promotion_latched_v1(dense<0> : tensor<1xi8>) {schedule.eval.group_proof_dependencies = [#domain]} : !llvm.array<1 x i8>

}

// -----

// An absent owner proof is not evidence of an empty, known certificate.
#member = #schedule.kernel_proof_dependency<latch = 0 : i64, pendingBit = 0 : i64, ranges = [0, 1, 9, 1]>
#domain = #schedule.group_proof_dependency<domain = 0 : i64, members = [0, 1], ranges = [0, 1, 9, 1]>

module attributes {runtime.state_bits = 128 : i64} {
  llvm.mlir.global internal @__talos_state_unknown(dense<0> : tensor<3xi64>) : !llvm.array<3 x i64>
  llvm.mlir.global internal @__talos_eval_kernel_promotion_latched_v1(dense<0> : tensor<2xi8>) {schedule.eval.kernel_proof_dependencies = [#member]} : !llvm.array<2 x i8>
  llvm.mlir.global internal @__talos_eval_promotion_pending_mask_v1(dense<1> : tensor<1xi64>) : !llvm.array<1 x i64>
  // expected-error@+1 {{group proof member has no explicit certificate}}
  llvm.mlir.global internal @__talos_eval_group_promotion_latched_v1(dense<0> : tensor<1xi8>) {schedule.eval.group_proof_dependencies = [#domain]} : !llvm.array<1 x i8>

}

// -----

// No certificate may extend beyond canonical state.
#member = #schedule.kernel_proof_dependency<latch = 0 : i64, pendingBit = 0 : i64, ranges = [0, 1, 9, 1]>
#domain = #schedule.group_proof_dependency<domain = 0 : i64, members = [0], ranges = [0, 1, 127, 2]>

module attributes {runtime.state_bits = 128 : i64} {
  llvm.mlir.global internal @__talos_state_unknown(dense<0> : tensor<3xi64>) : !llvm.array<3 x i64>
  llvm.mlir.global internal @__talos_eval_kernel_promotion_latched_v1(dense<0> : tensor<2xi8>) {schedule.eval.kernel_proof_dependencies = [#member]} : !llvm.array<2 x i8>
  llvm.mlir.global internal @__talos_eval_promotion_pending_mask_v1(dense<1> : tensor<1xi64>) : !llvm.array<1 x i64>
  // expected-error@+1 {{group proof ranges must equal the exact member certificate union}}
  llvm.mlir.global internal @__talos_eval_group_promotion_latched_v1(dense<0> : tensor<1xi8>) {schedule.eval.group_proof_dependencies = [#domain]} : !llvm.array<1 x i8>

}

// -----

// Members are a set of exact existing identities.
#member = #schedule.kernel_proof_dependency<latch = 0 : i64, pendingBit = 0 : i64, ranges = [0, 1, 9, 1]>
#domain = #schedule.group_proof_dependency<domain = 0 : i64, members = [0, 0], ranges = [0, 1, 9, 1]>

module attributes {runtime.state_bits = 128 : i64} {
  llvm.mlir.global internal @__talos_state_unknown(dense<0> : tensor<3xi64>) : !llvm.array<3 x i64>
  llvm.mlir.global internal @__talos_eval_kernel_promotion_latched_v1(dense<0> : tensor<2xi8>) {schedule.eval.kernel_proof_dependencies = [#member]} : !llvm.array<2 x i8>
  llvm.mlir.global internal @__talos_eval_promotion_pending_mask_v1(dense<1> : tensor<1xi64>) : !llvm.array<1 x i64>
  // expected-error@+1 {{group proof has invalid member identity}}
  llvm.mlir.global internal @__talos_eval_group_promotion_latched_v1(dense<0> : tensor<1xi8>) {schedule.eval.group_proof_dependencies = [#domain]} : !llvm.array<1 x i8>

}

// -----

// A function cannot reference an out-of-range domain.
#member = #schedule.kernel_proof_dependency<latch = 0 : i64, pendingBit = 0 : i64, ranges = [0, 1, 9, 1]>
#domain = #schedule.group_proof_dependency<domain = 0 : i64, members = [0], ranges = [0, 1, 9, 1]>

module attributes {runtime.state_bits = 128 : i64} {
  llvm.mlir.global internal @__talos_state_unknown(dense<0> : tensor<3xi64>) : !llvm.array<3 x i64>
  llvm.mlir.global internal @__talos_eval_kernel_promotion_latched_v1(dense<0> : tensor<2xi8>) {schedule.eval.kernel_proof_dependencies = [#member]} : !llvm.array<2 x i8>
  llvm.mlir.global internal @__talos_eval_promotion_pending_mask_v1(dense<1> : tensor<1xi64>) : !llvm.array<1 x i64>

  llvm.mlir.global internal @__talos_eval_group_promotion_latched_v1(dense<0> : tensor<1xi8>) {schedule.eval.group_proof_dependencies = [#domain]} : !llvm.array<1 x i8>
  // expected-error@+1 {{group function has an invalid planned domain reference}}
  llvm.func @group() attributes {schedule.eval.ranked_members = array<i32: 0>, schedule.eval.group_proof_domain = #schedule.group_proof_dependency<domain = 1 : i64, members = [0], ranges = [0, 1, 9, 1]>} {
    llvm.return
  }
}

// -----

// A function cannot silently replace the planned ranges.
#member = #schedule.kernel_proof_dependency<latch = 0 : i64, pendingBit = 0 : i64, ranges = [0, 1, 9, 1]>
#domain = #schedule.group_proof_dependency<domain = 0 : i64, members = [0], ranges = [0, 1, 9, 1]>

module attributes {runtime.state_bits = 128 : i64} {
  llvm.mlir.global internal @__talos_state_unknown(dense<0> : tensor<3xi64>) : !llvm.array<3 x i64>
  llvm.mlir.global internal @__talos_eval_kernel_promotion_latched_v1(dense<0> : tensor<2xi8>) {schedule.eval.kernel_proof_dependencies = [#member]} : !llvm.array<2 x i8>
  llvm.mlir.global internal @__talos_eval_promotion_pending_mask_v1(dense<1> : tensor<1xi64>) : !llvm.array<1 x i64>

  llvm.mlir.global internal @__talos_eval_group_promotion_latched_v1(dense<0> : tensor<1xi8>) {schedule.eval.group_proof_dependencies = [#domain]} : !llvm.array<1 x i8>
  // expected-error@+1 {{group function has an invalid planned domain reference}}
  llvm.func @group() attributes {schedule.eval.ranked_members = array<i32: 0>, schedule.eval.group_proof_domain = #schedule.group_proof_dependency<domain = 0 : i64, members = [0], ranges = [0, 1]>} {
    llvm.return
  }
}

// -----

// A child cannot claim the parent certificate as a narrower unchecked domain.
#member = #schedule.kernel_proof_dependency<latch = 0 : i64, pendingBit = 0 : i64, ranges = [0, 1, 9, 1]>
#domain = #schedule.group_proof_dependency<domain = 0 : i64, members = [0], ranges = [0, 1, 9, 1]>

module attributes {runtime.state_bits = 128 : i64} {
  llvm.mlir.global internal @__talos_state_unknown(dense<0> : tensor<3xi64>) : !llvm.array<3 x i64>
  llvm.mlir.global internal @__talos_eval_kernel_promotion_latched_v1(dense<0> : tensor<2xi8>) {schedule.eval.kernel_proof_dependencies = [#member]} : !llvm.array<2 x i8>
  llvm.mlir.global internal @__talos_eval_promotion_pending_mask_v1(dense<1> : tensor<1xi64>) : !llvm.array<1 x i64>

  llvm.mlir.global internal @__talos_eval_group_promotion_latched_v1(dense<0> : tensor<1xi8>) {schedule.eval.group_proof_dependencies = [#domain]} : !llvm.array<1 x i8>
  // expected-error@+1 {{group function has an invalid planned domain reference}}
  llvm.func @group() attributes {schedule.eval.ranked_members = array<i32: 1>, schedule.eval.group_proof_domain = #domain} {
    llvm.return
  }
}

// -----

// Initial positive bytes are not proof. Only the scanner can establish them.
#member = #schedule.kernel_proof_dependency<latch = 0 : i64, pendingBit = 0 : i64, ranges = [0, 1]>
#domain = #schedule.group_proof_dependency<domain = 0 : i64, members = [0], ranges = [0, 1]>
module attributes {runtime.state_bits = 128 : i64} {
  llvm.mlir.global internal @__talos_state_unknown(dense<1> : tensor<3xi64>) : !llvm.array<3 x i64>
  llvm.mlir.global internal @__talos_eval_kernel_promotion_latched_v1(dense<0> : tensor<1xi8>) {schedule.eval.kernel_proof_dependencies = [#member]} : !llvm.array<1 x i8>
  llvm.mlir.global internal @__talos_eval_promotion_pending_mask_v1(dense<1> : tensor<1xi64>) : !llvm.array<1 x i64>
  // expected-error@+1 {{group proof latches must initialize to zero}}
  llvm.mlir.global internal @__talos_eval_group_promotion_latched_v1(dense<1> : tensor<1xi8>) {schedule.eval.group_proof_dependencies = [#domain]} : !llvm.array<1 x i8>
}
