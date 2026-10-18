// RUN: obelisk-opt %s --split-input-file --schedule-coalesce-native-ready | FileCheck %s
// RUN: obelisk-opt %s --split-input-file --schedule-coalesce-native-ready --convert-native-schedule-actions-to-llvm | FileCheck %s --check-prefix=LOWER
// RUN: obelisk-opt %s --split-input-file --schedule-coalesce-native-ready | FileCheck %s --check-prefix=RESULT
// RUN: obelisk-opt %s --split-input-file --schedule-coalesce-native-ready --convert-native-schedule-actions-to-llvm | FileCheck %s --check-prefix=RESULT-LOWER
// Function arguments and early returns survive region formation. Branch
// paths retain their own additions; ordinary state stores are not observers.
// CHECK-LABEL: llvm.func @form(
// CHECK-SAME: %[[ARG:.*]]: i1)
// CHECK: schedule.ready.region
// CHECK: cf.cond_br %[[ARG]]
// CHECK: schedule.ready.update
// CHECK: schedule.ready.yield
// CHECK: schedule.ready.update
// CHECK: schedule.ready.yield
// CHECK: llvm.return
// LOWER-NOT: schedule.ready
// LOWER-NOT: llvm.alloca
module {
  llvm.mlir.global internal @ready(dense<0> : tensor<3xi64>) : !llvm.array<3 x i64>
  llvm.func @form(%early: i1) {
    %base = llvm.mlir.addressof @ready : !llvm.ptr
    %one = arith.constant 1 : i64
    cf.cond_br %early, ^left, ^right
  ^left:
    schedule.ready.update %base, %one {capacity = 128 : i64, word = 0 : i64, consume = false} : (!llvm.ptr, i64) -> ()
    llvm.return
  ^right:
    schedule.ready.update %base, %one {capacity = 128 : i64, word = 1 : i64, consume = false} : (!llvm.ptr, i64) -> ()
    llvm.return
  }
}
// -----
// A function result is yielded out of the region, so a body that returns a
// status still accumulates its updates in one region.
// RESULT-LABEL: llvm.func @status(
// RESULT: %[[REGION:.*]] = schedule.ready.region {
// RESULT: schedule.ready.update
// RESULT: schedule.ready.yield %[[ONE:.*]] : i32
// RESULT: schedule.ready.update
// RESULT: schedule.ready.yield %[[ZERO:.*]] : i32
// RESULT: } : i32
// RESULT-NEXT: llvm.return %[[REGION]] : i32
// RESULT-LOWER-LABEL: llvm.func @status(
// RESULT-LOWER-NOT: schedule.ready
// RESULT-LOWER: cf.cond_br %{{.*}}, ^[[LEFT:bb[0-9]+]], ^[[RIGHT:bb[0-9]+]]
// RESULT-LOWER: ^[[LEFT]]:
// RESULT-LOWER: llvm.store
// RESULT-LOWER: cf.br ^[[EXIT:bb[0-9]+]](%{{.*}} : i32)
// RESULT-LOWER: ^[[RIGHT]]:
// RESULT-LOWER: llvm.store
// RESULT-LOWER: cf.br ^[[EXIT]](%{{.*}} : i32)
// RESULT-LOWER: ^[[EXIT]](%[[STATUS:.*]]: i32):
// RESULT-LOWER-NEXT: llvm.return %[[STATUS]] : i32
module {
  llvm.mlir.global internal @ready(dense<0> : tensor<3xi64>) : !llvm.array<3 x i64>
  llvm.func @status(%early: i1) -> i32 {
    %base = llvm.mlir.addressof @ready : !llvm.ptr
    %one = arith.constant 1 : i64
    %ok = arith.constant 0 : i32
    %failed = arith.constant 1 : i32
    cf.cond_br %early, ^left, ^right
  ^left:
    schedule.ready.update %base, %one {capacity = 128 : i64, word = 0 : i64, consume = false} : (!llvm.ptr, i64) -> ()
    llvm.return %failed : i32
  ^right:
    schedule.ready.update %base, %one {capacity = 128 : i64, word = 0 : i64, consume = false} : (!llvm.ptr, i64) -> ()
    llvm.return %ok : i32
  }
}
