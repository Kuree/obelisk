// RUN: obelisk-opt %s --allow-unregistered-dialect --split-input-file --mlir-disable-threading --obelisk-materialize-native-eval-groups -o %t.serial.mlir
// RUN: obelisk-opt %s --allow-unregistered-dialect --split-input-file --obelisk-materialize-native-eval-groups -o %t.parallel.mlir
// RUN: diff -u %t.serial.mlir %t.parallel.mlir
// RUN: FileCheck %s < %t.parallel.mlir

// Group callees are mutable. Both direct and transitive callers must see the
// callee after its ordered materialization, including the changed inline cost.
// Inlining a pre-materialization copy would exhaust the budget before @leaf.
module attributes {schedule.native.max_inline_ops = 4 : i64} {
  llvm.func @leaf() -> i32 attributes {schedule.eval.infallible} {
    %one = llvm.mlir.constant(1 : i32) : i32
    %two = llvm.mlir.constant(2 : i32) : i32
    %value = llvm.add %one, %two : i32
    llvm.return %value : i32
  }
  // CHECK-LABEL: llvm.func @producer()
  // CHECK-SAME: schedule.eval.materialized_group_calls = 1 : i64
  // CHECK-NOT: llvm.call
  // CHECK: llvm.return
  llvm.func @producer() -> i32 attributes {schedule.eval.infallible, schedule.eval.ranked_members = array<i32: 0>} {
    %value = llvm.call @leaf() {schedule.eval.group_member = 0 : i32} : () -> i32
    llvm.return %value : i32
  }
  // CHECK-LABEL: llvm.func @direct()
  // CHECK-NOT: llvm.call
  // CHECK: llvm.return
  llvm.func @direct() -> i32 attributes {schedule.eval.ranked_members = array<i32: 0>} {
    %value = llvm.call @producer() {schedule.eval.group_member = 0 : i32} : () -> i32
    llvm.return %value : i32
  }
  llvm.func @bridge() -> i32 attributes {schedule.eval.infallible} {
    %value = llvm.call @producer() : () -> i32
    llvm.return %value : i32
  }
  // CHECK-LABEL: llvm.func @transitive()
  // CHECK: llvm.call @producer()
  // CHECK-NOT: llvm.call @bridge
  // CHECK-NOT: llvm.call @leaf
  // CHECK: llvm.return
  llvm.func @transitive() -> i32 attributes {schedule.eval.ranked_members = array<i32: 0>} {
    %value = llvm.call @bridge() {schedule.eval.group_member = 0 : i32} : () -> i32
    llvm.return %value : i32
  }
}

// -----

// Candidate metadata introduces a write to @original without a call edge.
// The preceding candidate must replace its body before @original's turn.
// Its untouched slow body, including the call, becomes @original.fallback.
module {
  llvm.mlir.global internal @__obelisk_state_value(dense<0> : tensor<1xi8>) : !llvm.array<1 x i8>
  llvm.mlir.global internal @ready(dense<0> : tensor<1xi64>) : !llvm.array<1 x i64>
  llvm.mlir.global internal @__obelisk_eval_promotion_pending_mask_v1(dense<0> : tensor<1xi64>) : !llvm.array<1 x i64>
  llvm.func @leaf() attributes {schedule.eval.infallible} {
    llvm.return
  }
  // CHECK-LABEL: llvm.func @original.fallback()
  // CHECK: llvm.call @leaf()
  // CHECK: llvm.return
  llvm.func @candidate() attributes {schedule.eval.ranked_members = array<i32: 0>, schedule.eval.group_ingress = @ready, schedule.eval.dataflow_candidate = @original} {
    %base = llvm.mlir.addressof @__obelisk_state_value : !llvm.ptr
    %one = llvm.mlir.constant(1 : i8) : i8
    llvm.store %one, %base : i8, !llvm.ptr
    llvm.return
  }
  // CHECK-LABEL: llvm.func @original()
  // CHECK-SAME: schedule.eval.dataflow_fallback = @original.fallback
  // CHECK-SAME: schedule.eval.predicated_dataflow
  // CHECK: llvm.call @original.fallback()
  llvm.func @original() attributes {schedule.eval.ranked_members = array<i32: 0>, schedule.eval.group_ingress = @ready} {
    llvm.call @leaf() {schedule.eval.group_member = 0 : i32} : () -> ()
    llvm.return
  }
}

// -----

// Unknown symbol scopes force candidate retention. Ordinary groups can still
// materialize their bodies, and both threading modes preserve the same symbols.
module {
  llvm.func @leaf() attributes {schedule.eval.infallible} {
    llvm.return
  }
  // CHECK-LABEL: llvm.func @group()
  // CHECK-SAME: schedule.eval.materialized_group_calls = 1 : i64
  // CHECK-NOT: llvm.call
  // CHECK: llvm.return
  llvm.func @group() attributes {schedule.eval.ranked_members = array<i32: 0>} {
    llvm.call @leaf() {schedule.eval.group_member = 0 : i32} : () -> ()
    llvm.return
  }
  // CHECK-LABEL: llvm.func @candidate()
  // CHECK-NOT: dataflow_candidate
  // CHECK: llvm.return
  llvm.func @candidate() attributes {schedule.eval.ranked_members = array<i32: 0>, schedule.eval.dataflow_candidate = @group} {
    llvm.return
  }
  "unknown.scope"() ({
    "unknown.use"() {target = @candidate} : () -> ()
  }) : () -> ()
}

// -----

// Symbol uses on a nested symbol-table root belong to the outer scope; uses
// inside its region resolve in the nested scope, including shadowed names.
module {
  llvm.func @group() attributes {schedule.eval.ranked_members = array<i32: 0>} {
    llvm.return
  }
  // CHECK-LABEL: llvm.func @root_use()
  // CHECK-NOT: dataflow_candidate
  // CHECK: llvm.return
  llvm.func @root_use() attributes {schedule.eval.ranked_members = array<i32: 0>, schedule.eval.dataflow_candidate = @group} {
    llvm.return
  }
  // CHECK-NOT: llvm.func @shadowed
  // CHECK: module @nested
  // CHECK-SAME: test.target = @root_use
  // CHECK: llvm.func @shadowed()
  llvm.func @shadowed() attributes {schedule.eval.ranked_members = array<i32: 0>, schedule.eval.dataflow_candidate = @group} {
    llvm.return
  }
  module @nested attributes {test.target = @root_use} {
    llvm.func @shadowed() {
      llvm.return
    }
    llvm.func @caller() {
      llvm.call @shadowed() : () -> ()
      llvm.return
    }
  }
}
