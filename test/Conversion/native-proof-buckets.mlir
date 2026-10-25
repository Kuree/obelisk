// RUN: obelisk-opt %s --materialize-native-promotion-range-index='bucket-min-entries=1' -o %t.mlir
// RUN: FileCheck %s < %t.mlir
// RUN: obelisk-opt %s --mlir-disable-threading --materialize-native-promotion-range-index='bucket-min-entries=1' | diff %t.mlir -
// RUN: obelisk-opt %s --materialize-native-promotion-range-index | FileCheck %s --check-prefix=FALLBACK --implicit-check-not='proof_bucket' --implicit-check-not='promotion_invalidate_buckets' --implicit-check-not='promotion_recheck_buckets'

// RUN: obelisk-opt %s '--test-obelisk-set-module-attributes=attrs="{llvm.data_layout = \"e-p:32:32-i64:64\", llvm.target_triple = \"wasm32-unknown-unknown\"}"' --materialize-native-promotion-range-index='bucket-min-entries=1' -o %t.wasm.mlir
// RUN: FileCheck %s < %t.wasm.mlir
// RUN: obelisk-translate --mlir-to-llvmir %t.wasm.mlir | %llvm_dist/bin/opt -passes=verify -disable-output

// Lower the cost threshold to exercise the same representation on a small,
// hand-authored fixture. The default threshold retains its interval-only path.
// Eight disjoint ranges of one certificate share the index with a second
// overlapping certificate. The first range crosses the 63/64 boundary, so its
// two clipped records precede the second certificate in bucket one. Both
// narrow helpers receive the same immutable tables and target-layout actions.
// CHECK-DAG: llvm.mlir.global internal constant @__obelisk_eval_proof_bucket_starts_v1(dense<[0, 1, 3, 4,
// CHECK-DAG: llvm.mlir.global internal constant @__obelisk_eval_proof_bucket_records_v1(dense<[16192, 1, 65538,
// CHECK-DAG: llvm.mlir.global internal constant @__obelisk_eval_proof_dependencies_v1
// CHECK-LABEL: llvm.func @__obelisk_eval_promotion_recheck_range_v1(
// CHECK: llvm.call @obelisk_rt_v1_native_promotion_recheck_ranges
// CHECK: llvm.mlir.addressof @__obelisk_eval_proof_aggregate_recheck_v1
// CHECK: llvm.call @obelisk_rt_v1_native_promotion_recheck_buckets
// CHECK-LABEL: llvm.func @__obelisk_eval_promotion_invalidate_range_v1(
// CHECK: llvm.mlir.constant(64 : i64)
// CHECK: llvm.icmp "ule" %arg1,
// CHECK: llvm.cond_br
// CHECK: llvm.call @obelisk_rt_v1_native_promotion_invalidate_ranges
// CHECK: llvm.mlir.addressof @__obelisk_eval_proof_bucket_starts_v1
// CHECK: llvm.mlir.addressof @__obelisk_eval_proof_bucket_records_v1
// CHECK: llvm.mlir.constant(16 : i64)
// CHECK: llvm.mlir.addressof @__obelisk_eval_proof_certificates_v1
// CHECK: llvm.mlir.addressof @__obelisk_eval_proof_aggregate_invalidate_v1
// CHECK: llvm.call @obelisk_rt_v1_native_promotion_invalidate_buckets
// FALLBACK-LABEL: llvm.func @__obelisk_eval_promotion_recheck_range_v1(
// FALLBACK: llvm.call @obelisk_rt_v1_native_promotion_recheck_ranges
// FALLBACK-LABEL: llvm.func @__obelisk_eval_promotion_invalidate_range_v1(
// FALLBACK: llvm.call @obelisk_rt_v1_native_promotion_invalidate_ranges

module attributes {runtime.state_bits = 1024 : i64} {
  llvm.func @__obelisk_eval_promotion_invalidate_range_v1(i64, i64)
  llvm.mlir.global internal @__obelisk_eval_promotion_latched_v1(1 : i8) : i8
  llvm.mlir.global internal @__obelisk_eval_route_promotion_dirty_v1(0 : i8) : i8
  llvm.mlir.global internal @__obelisk_eval_fast_nba_roots_v1(dense<3> : tensor<1xi64>) {
    schedule.eval.nba_proof_dependencies = [
      #schedule.nba_proof_dependency<bit = 0 : i64, ranges = [63, 2, 191, 2, 319, 2, 447, 2, 575, 2, 703, 2, 831, 2, 959, 2]>,
      #schedule.nba_proof_dependency<bit = 1 : i64, ranges = [64, 2]>
    ]} : !llvm.array<1 x i64>
  llvm.func @proof_known() -> i64 {
    %p = llvm.mlir.addressof @__obelisk_eval_fast_nba_roots_v1 : !llvm.ptr
    %v = llvm.load %p : !llvm.ptr -> i64
    llvm.return %v : i64
  }
  llvm.func @proof_latched() -> i8 {
    %p = llvm.mlir.addressof @__obelisk_eval_promotion_latched_v1 : !llvm.ptr
    %v = llvm.load %p : !llvm.ptr -> i8
    llvm.return %v : i8
  }
  llvm.func @proof_dirty() -> i8 {
    %p = llvm.mlir.addressof @__obelisk_eval_route_promotion_dirty_v1 : !llvm.ptr
    %v = llvm.load %p : !llvm.ptr -> i8
    llvm.return %v : i8
  }
  llvm.func @proof_reset() {
    %p3 = llvm.mlir.addressof @__obelisk_eval_fast_nba_roots_v1 : !llvm.ptr
    %v3 = llvm.mlir.constant(3 : i64) : i64
    llvm.store %v3, %p3 : i64, !llvm.ptr
    %p1 = llvm.mlir.addressof @__obelisk_eval_promotion_latched_v1 : !llvm.ptr
    %v1 = llvm.mlir.constant(1 : i8) : i8
    llvm.store %v1, %p1 : i8, !llvm.ptr
    %p0 = llvm.mlir.addressof @__obelisk_eval_route_promotion_dirty_v1 : !llvm.ptr
    %v0 = llvm.mlir.constant(0 : i8) : i8
    llvm.store %v0, %p0 : i8, !llvm.ptr
    llvm.return
  }
}
