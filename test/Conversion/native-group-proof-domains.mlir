// RUN: obelisk-opt %s --materialize-native-group-promotion-readiness --materialize-native-promotion-range-index | %llvm_dist/bin/mlir-opt --convert-arith-to-llvm --convert-cf-to-llvm -o %t.mlir
// RUN: FileCheck %s < %t.mlir
// RUN: obelisk-opt %s --mlir-disable-threading --materialize-native-group-promotion-readiness --materialize-native-promotion-range-index | %llvm_dist/bin/mlir-opt --convert-arith-to-llvm --convert-cf-to-llvm | diff %t.mlir -
// RUN: obelisk-opt %s '--test-obelisk-set-module-attributes=attrs="{llvm.data_layout = \"e-p:32:32-i64:64\", llvm.target_triple = \"wasm32-unknown-unknown\"}"' --materialize-native-group-promotion-readiness --materialize-native-promotion-range-index | %llvm_dist/bin/mlir-opt --convert-arith-to-llvm --convert-cf-to-llvm | obelisk-translate --mlir-to-llvmir | %llvm_dist/bin/opt -passes=verify -disable-output
// RUN: not obelisk-opt %s --pass-pipeline='builtin.module(materialize-native-group-promotion-readiness,materialize-native-group-promotion-readiness)' 2>&1 | FileCheck %s --check-prefix=REPEAT
// REPEAT: group readiness was materialized twice


// Group IDs 0/1 are independent of runnable owners 0/65. Overlap at bit 9
// merges exactly; holes at bits 1 and 71 stay X/Z and must not block promotion.
// Both groups depend on member 65, so its unknown writes revoke both latches.
// Runtime checks in ../Runtime/native-group-proof-domains.test also retain
// the unqueried member latches and pending bits across successful group scans.
// CHECK-DAG: @__obelisk_eval_group_promotion_domains_v1(dense<[0, 3, 3, 5]>
// CHECK-DAG: @__obelisk_eval_group_promotion_ranges_v1(dense<[0, 1, -1, 1, 0, 1, -512, 1023, 1, 2, -64, 127, 0, 1, -512, 1023, 1, 2, -64, 127]>
// CHECK-DAG: @__obelisk_eval_group_promotion_hints_v1(dense<0> : tensor<4xi64>)
// CHECK-DAG: @__obelisk_eval_kernel_promotion_latched_v1(dense<0> : tensor<66xi8>)
// CHECK-DAG: @__obelisk_eval_promotion_pending_mask_v1(dense<[1, 2]> : tensor<2xi64>)
// CHECK-LABEL: llvm.func @__obelisk_eval_group_promotion_scan_v1(
// CHECK-NOT: __obelisk_eval_promotion_pending_mask_v1
// CHECK: llvm.mlir.addressof @__obelisk_eval_group_promotion_hints_v1
// CHECK: llvm.load
// CHECK: llvm.load
// CHECK: llvm.load
// CHECK: llvm.and
// CHECK: llvm.cond_br
// CHECK-NOT: __obelisk_eval_promotion_pending_mask_v1
// CHECK: llvm.mlir.addressof @__obelisk_eval_group_promotion_latched_v1
// CHECK: llvm.store
// CHECK-NOT: __obelisk_eval_promotion_pending_mask_v1
// CHECK-LABEL: llvm.func @__obelisk_eval_group_promotion_ready_v1(
// CHECK: llvm.mlir.addressof @__obelisk_eval_group_promotion_latched_v1
// CHECK: llvm.load
// CHECK: llvm.cond_br
// CHECK: llvm.call @__obelisk_eval_group_promotion_scan_v1

#member0 = #schedule.kernel_proof_dependency<latch = 0 : i64, pendingBit = 0 : i64, ranges = [0, 1, 9, 1]>
#member65 = #schedule.kernel_proof_dependency<latch = 65 : i64, pendingBit = 65 : i64, ranges = [9, 1, 70, 1]>
#group0 = #schedule.group_proof_dependency<domain = 0 : i64, members = [0, 65], ranges = [0, 1, 9, 1, 70, 1]>
#group1 = #schedule.group_proof_dependency<domain = 1 : i64, members = [65], ranges = [9, 1, 70, 1]>
module attributes {runtime.state_bits = 128 : i64} {
  llvm.mlir.global internal @__obelisk_state_unknown(dense<[2, 128, 0]> : tensor<3xi64>) : !llvm.array<3 x i64>
  llvm.mlir.global internal @__obelisk_eval_kernel_promotion_latched_v1(dense<0> : tensor<66xi8>) {schedule.eval.kernel_proof_dependencies = [#member0, #member65]} : !llvm.array<66 x i8>
  llvm.mlir.global internal @__obelisk_eval_promotion_pending_mask_v1(dense<[1, 2]> : tensor<2xi64>) : !llvm.array<2 x i64>
  llvm.mlir.global internal @__obelisk_eval_group_promotion_latched_v1(dense<0> : tensor<2xi8>) {schedule.eval.group_proof_dependencies = [#group0, #group1]} : !llvm.array<2 x i8>
  llvm.func @__obelisk_eval_promotion_invalidate_range_v1(i64, i64)
  llvm.func @group_test_unknown() -> !llvm.ptr {
    %p = llvm.mlir.addressof @__obelisk_state_unknown : !llvm.ptr
    llvm.return %p : !llvm.ptr
  }
  llvm.func @group_test_latches() -> !llvm.ptr {
    %p = llvm.mlir.addressof @__obelisk_eval_group_promotion_latched_v1 : !llvm.ptr
    llvm.return %p : !llvm.ptr
  }
  llvm.func @group_test_member_latches() -> !llvm.ptr {
    %p = llvm.mlir.addressof @__obelisk_eval_kernel_promotion_latched_v1 : !llvm.ptr
    llvm.return %p : !llvm.ptr
  }
  llvm.func @group_test_pending() -> !llvm.ptr {
    %p = llvm.mlir.addressof @__obelisk_eval_promotion_pending_mask_v1 : !llvm.ptr
    llvm.return %p : !llvm.ptr
  }
}
