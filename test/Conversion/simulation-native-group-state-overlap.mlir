// RUN: obelisk-opt %s --obelisk-materialize-native-eval-groups | FileCheck %s

// Differently sized overlapping accesses share one physical SSA window.
// A byte read observes the preceding wide blocking store (LRM 4.9.3),
// while a separate range remains a separate slot.
module attributes {llvm.data_layout = "e-m:e-p:64:64-i64:64-n8:16:32:64-S128"} {
  llvm.mlir.global internal @__obelisk_state_value(dense<0> : tensor<8xi8>) : !llvm.array<8 x i8>

  llvm.func @wide() attributes {schedule.eval.infallible} {
    %base = llvm.mlir.addressof @__obelisk_state_value : !llvm.ptr
    %v = llvm.load %base {alignment = 1 : i64} : !llvm.ptr -> i16
    %one = llvm.mlir.constant(1 : i16) : i16
    %next = llvm.add %v, %one : i16
    llvm.store %next, %base {alignment = 1 : i64} : i16, !llvm.ptr
    %separate = llvm.getelementptr %base[4] : (!llvm.ptr) -> !llvm.ptr, i8
    %narrow = llvm.trunc %next : i16 to i8
    llvm.store %narrow, %separate : i8, !llvm.ptr
    llvm.return
  }
  llvm.func @overlapping() attributes {schedule.eval.infallible} {
    %base = llvm.mlir.addressof @__obelisk_state_value : !llvm.ptr
    %v = llvm.load %base {alignment = 1 : i64} : !llvm.ptr -> i16
    %partial = llvm.getelementptr %base[1] : (!llvm.ptr) -> !llvm.ptr, i8
    %hi = llvm.load %partial : !llvm.ptr -> i8
    %ext = llvm.zext %hi : i8 to i16
    %next = llvm.add %v, %ext : i16
    llvm.store %next, %base {alignment = 1 : i64} : i16, !llvm.ptr
    %separate = llvm.getelementptr %base[4] : (!llvm.ptr) -> !llvm.ptr, i8
    %old = llvm.load %separate : !llvm.ptr -> i8
    %result = llvm.add %old, %hi : i8
    llvm.store %result, %separate : i8, !llvm.ptr
    llvm.return
  }
  // CHECK-LABEL: llvm.func @group()
  // CHECK-SAME: schedule.eval.ssa_value_ranges = 2 : i64
  // CHECK: llvm.getelementptr {{.*}}[4]
  // CHECK: llvm.load {{.*}} -> i16
  // CHECK: llvm.load {{.*}} -> i8
  // CHECK-NOT: llvm.load
  // CHECK: llvm.lshr {{.*}} : i16
  // CHECK: llvm.trunc {{.*}} : i16 to i8
  // CHECK-NOT: llvm.load
  // CHECK: llvm.store {{.*}} : i16
  // CHECK-NOT: llvm.load
  // CHECK: llvm.store {{.*}} : i8
  // CHECK: llvm.return
  llvm.func @group() attributes {schedule.eval.ranked_members = array<i32: 0>} {
    llvm.call @wide() {schedule.eval.group_member = 0 : i32} : () -> ()
    llvm.call @overlapping() {schedule.eval.group_member = 1 : i32} : () -> ()
    llvm.return
  }
}
