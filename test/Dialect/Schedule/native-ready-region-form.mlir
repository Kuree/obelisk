// RUN: obelisk-opt %s --schedule-coalesce-native-ready | FileCheck %s
// RUN: obelisk-opt %s --schedule-coalesce-native-ready --convert-native-schedule-actions-to-llvm | FileCheck %s --check-prefix=LOWER
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
