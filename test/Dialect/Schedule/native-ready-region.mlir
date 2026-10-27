// RUN: talos-opt %s --schedule-coalesce-native-ready --convert-native-schedule-actions-to-llvm -o %t.lowered
// RUN: FileCheck %s < %t.lowered
// RUN: %llvm_dist/bin/mlir-opt %t.lowered --convert-arith-to-llvm --convert-cf-to-llvm | talos-translate --mlir-to-llvmir > %t.ll
// RUN: %llvm_dist/bin/mlir-opt %S/Inputs/native-ready-region-harness.mlir --convert-arith-to-llvm --convert-cf-to-llvm | talos-translate --mlir-to-llvmir > %t.harness.ll
// RUN: %llvm_dist/bin/clang -O0 %t.ll %t.harness.ll -o %t.o0
// RUN: %t.o0
// RUN: %llvm_dist/bin/clang -O3 %t.ll %t.harness.ll -o %t.o3
// RUN: %t.o3
// Ordered additions/consumes cross loop backedges and joins. Alias reads and
// callbacks see materialized leaves AND indexes. Callback changes survive.
// CHECK-NOT: schedule.ready
// CHECK-LABEL: llvm.func @exercise
// CHECK-NOT: llvm.alloca
// CHECK: %[[ORDINARY:.*]] = llvm.mlir.addressof @ordinary
// CHECK: cf.cond_br {{.*}}, ^[[BODY:bb[0-9]+]], ^[[FINISH:bb[0-9]+]]
// CHECK: ^[[BODY]]
// CHECK: llvm.store %{{.*}}, %[[ORDINARY]]
// CHECK-NOT: llvm.store
// CHECK: ^[[FINISH]]
// CHECK: llvm.call @observe
// CHECK-NOT: schedule.ready
module {
  llvm.mlir.global external @ready() : !llvm.array<66 x i64> {
    %zero = llvm.mlir.zero : !llvm.array<66 x i64>
    llvm.return %zero : !llvm.array<66 x i64>
  }
  llvm.mlir.alias external @ready_alias : !llvm.array<66 x i64> {
    %base = llvm.mlir.addressof @ready : !llvm.ptr
    llvm.return %base : !llvm.ptr
  }
  llvm.mlir.global external @ordinary() : i64
  llvm.mlir.global external @seen_before() : i64
  llvm.mlir.global external @seen_after() : i64
  llvm.func @observe(i64)
  llvm.func @exercise(%n: i64, %alias: !llvm.ptr) {
    %base = llvm.mlir.addressof @ready : !llvm.ptr
    %ordinary = llvm.mlir.addressof @ordinary : !llvm.ptr
    %before = llvm.mlir.addressof @seen_before : !llvm.ptr
    %after = llvm.mlir.addressof @seen_after : !llvm.ptr
    %symbol_alias = llvm.mlir.addressof @ready_alias : !llvm.ptr
    %zero = arith.constant 0 : i64
    %one = arith.constant 1 : i64
    %eight = arith.constant 256 : i64
    %sixteen = arith.constant 65536 : i64
    %thirtytwo = arith.constant 4294967296 : i64
    %high = arith.constant -9223372036854775808 : i64
    schedule.ready.region {
      cf.br ^loop(%zero : i64)
    ^loop(%i: i64):
      %more = arith.cmpi ult, %i, %n : i64
      cf.cond_br %more, ^body, ^finish
    ^body:
      %mask = arith.shli %one, %i : i64
      schedule.ready.update %base, %mask {capacity = 4096 : i64, word = 0 : i64, consume = false} : (!llvm.ptr, i64) -> ()
      schedule.ready.update %base, %high {capacity = 4096 : i64, word = 63 : i64, consume = false} : (!llvm.ptr, i64) -> ()
      llvm.store %i, %ordinary : i64, !llvm.ptr
      %odd_bit = arith.andi %i, %one : i64
      %odd = arith.cmpi ne, %odd_bit, %zero : i64
      cf.cond_br %odd, ^add, ^consume
    ^add:
      schedule.ready.update %base, %one {capacity = 4096 : i64, word = 0 : i64, consume = false} : (!llvm.ptr, i64) -> ()
      cf.br ^advance
    ^consume:
      schedule.ready.update %base, %one {capacity = 4096 : i64, word = 0 : i64, consume = true} : (!llvm.ptr, i64) -> ()
      cf.br ^advance
    ^advance:
      %next = arith.addi %i, %one : i64
      cf.br ^loop(%next : i64)
    ^finish:
      %prior = llvm.load %alias : !llvm.ptr -> i64
      llvm.store %prior, %before : i64, !llvm.ptr
      llvm.call @observe(%n) : (i64) -> ()
      schedule.ready.update %base, %high {capacity = 4096 : i64, word = 63 : i64, consume = true} : (!llvm.ptr, i64) -> ()
      schedule.ready.update %base, %eight {capacity = 4096 : i64, word = 0 : i64, consume = false} : (!llvm.ptr, i64) -> ()
      %current = llvm.load %symbol_alias : !llvm.ptr -> i64
      llvm.store %current, %after : i64, !llvm.ptr
      schedule.ready.update %base, %sixteen {capacity = 4096 : i64, word = 0 : i64, consume = true} : (!llvm.ptr, i64) -> ()
      schedule.ready.update %base, %thirtytwo {capacity = 4096 : i64, word = 0 : i64, consume = false} : (!llvm.ptr, i64) -> ()
      schedule.ready.yield
    }
    llvm.return
  }
  // Distinct symbols are not sufficient proof of disjointness. Neither the
  // straight-line pass nor the region may coalesce across an alias consume.
  llvm.func @alias_updates() {
    %base = llvm.mlir.addressof @ready : !llvm.ptr
    %alias = llvm.mlir.addressof @ready_alias : !llvm.ptr
    %one = arith.constant 1 : i64
    %two = arith.constant 2 : i64
    schedule.ready.update %base, %one {capacity = 4096 : i64, word = 0 : i64, consume = false} : (!llvm.ptr, i64) -> ()
    schedule.ready.update %alias, %one {capacity = 4096 : i64, word = 0 : i64, consume = true} : (!llvm.ptr, i64) -> ()
    schedule.ready.update %base, %two {capacity = 4096 : i64, word = 0 : i64, consume = false} : (!llvm.ptr, i64) -> ()
    llvm.return
  }
}
