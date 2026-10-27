// RUN: talos-opt %s --convert-native-schedule-actions-to-llvm -o %t
// RUN: FileCheck %s < %t
// RUN: talos-opt %s --mlir-disable-threading --convert-native-schedule-actions-to-llvm | diff %t -
// RUN: talos-opt %t --convert-native-schedule-actions-to-llvm | diff %t -
// RUN: talos-opt %s --schedule-coalesce-native-ready --convert-native-schedule-actions-to-llvm | FileCheck %s --check-prefix=OBS

// Bulk publication has a bounded CFG, no callback/runtime helper, and no
// per-leaf SSA expansion. The runtime companion compares ascending merges
// against ReadySetView for empty, sparse, dense, partial and aliased sources.
module {
  llvm.func @merge_1(%base: !llvm.ptr, %source: !llvm.ptr) {
    schedule.ready.merge %base, %source {capacity = 1 : i64} : (!llvm.ptr, !llvm.ptr) -> ()
    llvm.return
  }
  llvm.func @merge_63(%base: !llvm.ptr, %source: !llvm.ptr) {
    schedule.ready.merge %base, %source {capacity = 63 : i64} : (!llvm.ptr, !llvm.ptr) -> ()
    llvm.return
  }
  llvm.func @merge_64(%base: !llvm.ptr, %source: !llvm.ptr) {
    schedule.ready.merge %base, %source {capacity = 64 : i64} : (!llvm.ptr, !llvm.ptr) -> ()
    llvm.return
  }
  llvm.func @merge_65(%base: !llvm.ptr, %source: !llvm.ptr) {
    schedule.ready.merge %base, %source {capacity = 65 : i64} : (!llvm.ptr, !llvm.ptr) -> ()
    llvm.return
  }
  llvm.func @merge_128(%base: !llvm.ptr, %source: !llvm.ptr) {
    schedule.ready.merge %base, %source {capacity = 128 : i64} : (!llvm.ptr, !llvm.ptr) -> ()
    llvm.return
  }
  llvm.func @merge_2048(%base: !llvm.ptr, %source: !llvm.ptr) {
    schedule.ready.merge %base, %source {capacity = 2048 : i64} : (!llvm.ptr, !llvm.ptr) -> ()
    llvm.return
  }
  llvm.func @merge_2049(%base: !llvm.ptr, %source: !llvm.ptr) {
    schedule.ready.merge %base, %source {capacity = 2049 : i64} : (!llvm.ptr, !llvm.ptr) -> ()
    llvm.return
  }
  llvm.func @merge_4096(%base: !llvm.ptr, %source: !llvm.ptr) {
    schedule.ready.merge %base, %source {capacity = 4096 : i64} : (!llvm.ptr, !llvm.ptr) -> ()
    llvm.return
  }
  // CHECK-LABEL: llvm.func @merge_262145(
  // CHECK-NOT: llvm.call
  // CHECK: cf.br ^[[HEAD:bb[0-9]+]]({{.*}} : i64)
  // CHECK: ^[[HEAD]](%[[WORD:.*]]: i64):
  // CHECK: llvm.getelementptr %arg1[%[[WORD]]]
  // CHECK: %[[SOURCE:.*]] = llvm.load
  // CHECK: arith.cmpi eq, %[[WORD]],
  // CHECK: arith.andi %[[SOURCE]],
  // CHECK: %[[SELECTED:.*]] = arith.select
  // CHECK: %[[EMPTY:.*]] = arith.cmpi eq, %[[SELECTED]],
  // CHECK: cf.cond_br %[[EMPTY]], ^[[ADVANCE:bb[0-9]+]], ^[[UPDATE:bb[0-9]+]]
  // CHECK: ^[[UPDATE]]:
  // CHECK: %[[PREVIOUS:.*]] = llvm.load
  // CHECK: llvm.store
  // CHECK: arith.cmpi eq, %[[PREVIOUS]],
  // CHECK: cf.cond_br
  // CHECK: ^[[ADVANCE]]:
  // CHECK: arith.addi %[[WORD]],
  // CHECK: arith.cmpi ult,
  // CHECK: cf.cond_br {{.*}}, ^[[HEAD]]({{.*}} : i64),
  // CHECK: arith.minui
  // CHECK: arith.shrui
  // CHECK: llvm.store
  // CHECK: cf.cond_br
  // CHECK: arith.shrui
  // CHECK: llvm.store
  // CHECK: cf.cond_br
  // CHECK: arith.shrui
  // CHECK: llvm.store
  // CHECK-NOT: llvm.call
  // CHECK-NOT: schedule.ready
  // CHECK: llvm.return
  llvm.func @merge_262145(%base: !llvm.ptr, %source: !llvm.ptr) {
    schedule.ready.merge %base, %source {capacity = 262145 : i64} : (!llvm.ptr, !llvm.ptr) -> ()
    llvm.return
  }
  llvm.mlir.global external @ready_merge_observed() : !llvm.array<3 x i64>
  llvm.func @ready_merge_observe(!llvm.ptr)
  // A merge observes pending scalar updates, and its effects are visible to
  // callbacks and subsequent reads. It is a boundary for ready.region too.
  // OBS-LABEL: llvm.func @merge_observed(
  // OBS: llvm.store
  // OBS: cf.br
  // OBS: llvm.load
  // OBS: cf.cond_br
  // OBS: llvm.call @ready_merge_observe
  // OBS: llvm.store
  // OBS-NOT: schedule.ready
  // OBS: llvm.return
  llvm.func @merge_observed(%source: !llvm.ptr) -> i64 {
    %base = llvm.mlir.addressof @ready_merge_observed : !llvm.ptr
    %one = arith.constant 1 : i64
    %eight = arith.constant 8 : i64
    schedule.ready.update %base, %one {capacity = 128 : i64, word = 0 : i64, consume = false} : (!llvm.ptr, i64) -> ()
    schedule.ready.merge %base, %source {capacity = 128 : i64} : (!llvm.ptr, !llvm.ptr) -> ()
    llvm.call @ready_merge_observe(%base) : (!llvm.ptr) -> ()
    schedule.ready.update %base, %eight {capacity = 128 : i64, word = 1 : i64, consume = false} : (!llvm.ptr, i64) -> ()
    %result = schedule.ready.read %base {capacity = 128 : i64, word = 1 : i64} : (!llvm.ptr) -> i64
    llvm.return %result : i64
  }
}
