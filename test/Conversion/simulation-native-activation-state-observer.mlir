// RUN: obelisk-opt %s --obelisk-materialize-native-eval-groups | FileCheck %s

// IEEE 1800-2023 4.3, 4.6(a), 4.9.3: forward current blocking values
// through address arithmetic and explicit proof notifications. An unrelated
// byte copy is not an observation cut for this lane. An opaque observer must
// see the latest value, and its mutation must be acquired before continuing.
module attributes {llvm.data_layout = "e-m:e-p:64:64-i64:64-n8:16:32:64-S128"} {
  llvm.mlir.global internal @__obelisk_state_value(dense<0> : tensor<8xi8>) : !llvm.array<8 x i8>
  llvm.mlir.global internal @source(42 : i8) : i8
  llvm.mlir.global internal @proof(0 : i64) : i64
  llvm.func @observer()
  llvm.func @notify(%offset: i64) attributes {schedule.eval.state_metadata_only} {
    %proof = llvm.mlir.addressof @proof : !llvm.ptr
    llvm.store %offset, %proof : i64, !llvm.ptr
    llvm.return
  }
  llvm.func @activation() attributes {schedule.eval.raw_captures} {
    %base = llvm.mlir.addressof @__obelisk_state_value : !llvm.ptr
    %one = llvm.mlir.constant(1 : i8) : i8
    %old = llvm.load %base : !llvm.ptr -> i8
    %next = llvm.add %old, %one : i8
    llvm.store %next, %base : i8, !llvm.ptr
    %address = llvm.ptrtoint %base : !llvm.ptr to i64
    %offset = llvm.sub %address, %address : i64
    llvm.call @notify(%offset) : (i64) -> ()
    %current = llvm.load %base : !llvm.ptr -> i8
    %twice = llvm.add %current, %one : i8
    llvm.store %twice, %base : i8, !llvm.ptr
    %other = llvm.getelementptr %base[4] : (!llvm.ptr) -> !llvm.ptr, i8
    %source = llvm.mlir.addressof @source : !llvm.ptr
    %length = llvm.mlir.constant(1 : i64) : i64
    "llvm.intr.memcpy"(%other, %source, %length) {isVolatile = false} : (!llvm.ptr, !llvm.ptr, i64) -> ()
    llvm.call @observer() : () -> ()
    %observed = llvm.load %base : !llvm.ptr -> i8
    %final = llvm.add %observed, %one : i8
    llvm.store %final, %base : i8, !llvm.ptr
    llvm.return
  }
  llvm.func @read_state(%index: i64) -> i8 {
    %base = llvm.mlir.addressof @__obelisk_state_value : !llvm.ptr
    %address = llvm.getelementptr %base[%index] : (!llvm.ptr, i64) -> !llvm.ptr, i8
    %value = llvm.load %address : !llvm.ptr -> i8
    llvm.return %value : i8
  }
  llvm.func @write_state(%value: i8) {
    %base = llvm.mlir.addressof @__obelisk_state_value : !llvm.ptr
    llvm.store %value, %base : i8, !llvm.ptr
    llvm.return
  }
}

// CHECK-LABEL: llvm.func @activation()
// CHECK-SAME: schedule.eval.ssa_value_ranges = 1 : i64
// CHECK: %[[OLD:.*]] = llvm.load
// CHECK: %[[NEXT:.*]] = llvm.add %[[OLD]],
// CHECK-NOT: llvm.store
// CHECK: llvm.call @notify
// CHECK-NOT: llvm.load
// CHECK-NOT: llvm.store
// CHECK: %[[TWICE:.*]] = llvm.add %[[NEXT]],
// CHECK-NOT: llvm.store
// CHECK: "llvm.intr.memcpy"
// CHECK: llvm.store %[[TWICE]],
// CHECK-NEXT: llvm.call @observer
// CHECK-NEXT: %[[ACQUIRED:.*]] = llvm.load
// CHECK: %[[FINAL:.*]] = llvm.add %[[ACQUIRED]],
// CHECK: llvm.store %[[FINAL]],
// CHECK-NEXT: llvm.return
