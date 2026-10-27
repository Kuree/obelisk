// RUN: talos-opt %s --materialize-native-promotion-range-index='bucket-min-entries=1' | FileCheck %s --implicit-check-not='proof_bucket' --implicit-check-not='promotion_invalidate_buckets' --implicit-check-not='promotion_recheck_buckets'

// A high sparse offset would need an enormous prefix array. The metadata
// budget rejects that expansion before allocating it, retaining the tree.
// CHECK-LABEL: llvm.func @__talos_eval_promotion_recheck_range_v1(
// CHECK: llvm.call @talos_rt_v1_native_promotion_recheck_ranges
// CHECK-LABEL: llvm.func @__talos_eval_promotion_invalidate_range_v1(
// CHECK: llvm.call @talos_rt_v1_native_promotion_invalidate_ranges
module attributes {runtime.state_bits = 1073741825 : i64} {
  llvm.func @__talos_eval_promotion_invalidate_range_v1(i64, i64)
  llvm.mlir.global internal @__talos_eval_fast_nba_roots_v1(dense<1> : tensor<1xi64>) {
    schedule.eval.nba_proof_dependencies = [
      #schedule.nba_proof_dependency<bit = 0 : i64, ranges = [1073741824, 1]>
    ]} : !llvm.array<1 x i64>
}
