// RUN: talos-opt %s --convert-native-schedule-actions-to-llvm='coalesce=true' -o %t.lowered
// RUN: FileCheck %s < %t.lowered
// RUN: %llvm_dist/bin/mlir-opt %t.lowered --convert-scf-to-cf --convert-arith-to-llvm --convert-cf-to-llvm | talos-translate --mlir-to-llvmir > %t.ll
// RUN: %llvm_dist/bin/clang -O0 %t.ll %S/Inputs/native-ready-consume.c -o %t.o0
// RUN: %t.o0
// RUN: %llvm_dist/bin/clang -O3 %t.ll %S/Inputs/native-ready-consume.c -o %t.o3
// RUN: %t.o3

// Consuming pending work preserves exact nonempty-child summaries. Once a
// child remains nonempty, every ancestor bit stays set and needs no update.
// An observer after the consume sees the leaf and all necessary summary
// changes together. The lower-bound cache may remain stale (LRM 4.5--4.7).
// CHECK-LABEL: llvm.func @consume(
// CHECK: %[[NEXT:.*]] = arith.andi
// CHECK: llvm.store %[[NEXT]], %arg0
// CHECK: %[[EMPTY:.*]] = arith.cmpi eq, %[[NEXT]], %[[ZERO:.*]]
// CHECK-NEXT: cf.cond_br %[[EMPTY]], ^[[PARENT:bb[0-9]+]], ^[[DONE:bb[0-9]+]]
// CHECK-NEXT: ^[[PARENT]]:
// CHECK: %[[ADDRESS:.*]] = llvm.getelementptr %arg0[32776]
// CHECK: %[[OLD:.*]] = llvm.load %[[ADDRESS]]
// CHECK: %[[PARENT_NEXT:.*]] = arith.andi %[[OLD]], %{{.*}}
// CHECK: llvm.store %[[PARENT_NEXT]],
// CHECK: %[[PARENT_EMPTY:.*]] = arith.cmpi eq, %[[PARENT_NEXT]], %[[ZERO]]
// CHECK-NEXT: cf.cond_br %[[PARENT_EMPTY]], ^[[TOP:bb[0-9]+]], ^[[DONE]]
// CHECK-NEXT: ^[[TOP]]:
// CHECK: llvm.getelementptr %arg0[33288]
// CHECK: llvm.store
// CHECK-NEXT: cf.br ^[[DONE]]
// CHECK-NEXT: ^[[DONE]]:
// CHECK-NEXT: llvm.call @observe(%arg0)
// CHECK-NEXT: llvm.return
// A structured single-block region retains straight-line index maintenance.
// CHECK-LABEL: llvm.func @consume_structured(
// CHECK: scf.if
// CHECK-NOT: cf.cond_br
// CHECK: llvm.call @observe
// A resolved global with multiple actions also passes through ready-region
// formation. Opaque calls observe each consume before the next one begins.
// The last leaf exercises bit 63 in both summary levels.
// CHECK-LABEL: llvm.func @consume_global(
// CHECK-NOT: llvm.alloca
// CHECK: %[[BASE:.*]] = llvm.mlir.addressof @ready
// CHECK: llvm.getelementptr %[[BASE]][32760]
// CHECK: cf.cond_br
// CHECK: llvm.getelementptr %[[BASE]][33280]
// CHECK: cf.cond_br
// CHECK: llvm.getelementptr %[[BASE]][33288]
// CHECK: llvm.call @observe(%[[BASE]])
// CHECK: cf.cond_br
// CHECK: cf.cond_br
// CHECK: llvm.call @observe(%[[BASE]])
// CHECK: llvm.return
module {
  llvm.mlir.global external @ready() : !llvm.array<4162 x i64>
  llvm.func @observe(!llvm.ptr)
  llvm.func @consume(%base: !llvm.ptr, %mask: i64) {
    schedule.ready.update %base, %mask {capacity = 262144 : i64, word = 0 : i64, consume = true} : (!llvm.ptr, i64) -> ()
    llvm.call @observe(%base) : (!llvm.ptr) -> ()
    llvm.return
  }
  llvm.func @consume_structured(%base: !llvm.ptr, %mask: i64, %guard: i64) {
    %zero = arith.constant 0 : i64
    %active = arith.cmpi ne, %guard, %zero : i64
    scf.if %active {
      schedule.ready.update %base, %mask {capacity = 262144 : i64, word = 0 : i64, consume = true} : (!llvm.ptr, i64) -> ()
    }
    llvm.call @observe(%base) : (!llvm.ptr) -> ()
    llvm.return
  }
  llvm.func @consume_global(%mask: i64) {
    %base = llvm.mlir.addressof @ready : !llvm.ptr
    schedule.ready.update %base, %mask {capacity = 262144 : i64, word = 4095 : i64, consume = true} : (!llvm.ptr, i64) -> ()
    llvm.call @observe(%base) : (!llvm.ptr) -> ()
    schedule.ready.update %base, %mask {capacity = 262144 : i64, word = 4095 : i64, consume = true} : (!llvm.ptr, i64) -> ()
    llvm.call @observe(%base) : (!llvm.ptr) -> ()
    llvm.return
  }
}
