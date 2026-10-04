// RUN: obelisk-opt %s --obelisk-materialize-native-eval-groups --convert-native-schedule-actions-to-llvm -o %t.mlir
// RUN: FileCheck %s < %t.mlir
// RUN: %llvm_dist/bin/mlir-opt %t.mlir --convert-arith-to-llvm --convert-cf-to-llvm | mlir-translate --mlir-to-llvmir > %t.ll
// RUN: %llvm_dist/bin/clang -O0 %t.ll -o %t.o0
// RUN: %t.o0
// RUN: %llvm_dist/bin/clang -O3 %t.ll -o %t.o3
// RUN: %t.o3
// Inlining exposes the first member's publication to the next member's local
// readiness read. Neither a premature flush nor an old physical leaf snapshot
// is needed. Both consumes and the intervening addition collapse to one store.
module {
  llvm.mlir.global internal @ready(1 : i64) : i64
  llvm.mlir.global internal @observed(0 : i32) : i32
  llvm.func @producer() attributes {schedule.eval.infallible} {
    %base = llvm.mlir.addressof @ready : !llvm.ptr
    %two = llvm.mlir.constant(2 : i64) : i64
    schedule.ready.update %base, %two {capacity = 64 : i64, word = 0 : i64, consume = false} : (!llvm.ptr, i64) -> ()
    llvm.return
  }
  llvm.func @consumer() attributes {schedule.eval.infallible} {
    %out = llvm.mlir.addressof @observed : !llvm.ptr
    %one = llvm.mlir.constant(1 : i32) : i32
    llvm.store %one, %out : i32, !llvm.ptr
    llvm.return
  }
  // CHECK-LABEL: llvm.func @group()
  // CHECK-NOT: llvm.alloca
  // CHECK-NOT: llvm.call
  // CHECK: llvm.cond_br
  // CHECK-COUNT-2: llvm.store
  // CHECK-NOT: llvm.store
  // CHECK: llvm.return
  llvm.func @group() attributes {schedule.eval.ranked_members = array<i32: 0, 1>} {
    %base = llvm.mlir.addressof @ready : !llvm.ptr
    %zero = llvm.mlir.constant(0 : i64) : i64
    %one = llvm.mlir.constant(1 : i64) : i64
    %two = llvm.mlir.constant(2 : i64) : i64
    %first = schedule.ready.read %base {capacity = 64 : i64, word = 0 : i64} : (!llvm.ptr) -> i64
    %bit0 = llvm.and %first, %one : i64
    %active0 = llvm.icmp "ne" %bit0, %zero : i64
    llvm.cond_br %active0, ^first, ^next
  ^first:
    schedule.ready.update %base, %one {capacity = 64 : i64, word = 0 : i64, consume = true} : (!llvm.ptr, i64) -> ()
    llvm.call @producer() {schedule.eval.group_member = 0 : i32} : () -> ()
    llvm.br ^next
  ^next:
    %second = schedule.ready.read %base {capacity = 64 : i64, word = 0 : i64} : (!llvm.ptr) -> i64
    %bit1 = llvm.and %second, %two : i64
    %active1 = llvm.icmp "ne" %bit1, %zero : i64
    llvm.cond_br %active1, ^second, ^done
  ^second:
    schedule.ready.update %base, %two {capacity = 64 : i64, word = 0 : i64, consume = true} : (!llvm.ptr, i64) -> ()
    llvm.call @consumer() {schedule.eval.group_member = 1 : i32} : () -> ()
    llvm.br ^done
  ^done:
    llvm.return
  }
  // CHECK-LABEL: llvm.func @main()
  llvm.func @main() -> i32 {
    llvm.call @group() : () -> ()
    %base = llvm.mlir.addressof @ready : !llvm.ptr
    %out = llvm.mlir.addressof @observed : !llvm.ptr
    %ready = llvm.load %base : !llvm.ptr -> i64
    %seen = llvm.load %out : !llvm.ptr -> i32
    %zero = llvm.mlir.constant(0 : i64) : i64
    %one = llvm.mlir.constant(1 : i32) : i32
    %empty = llvm.icmp "eq" %ready, %zero : i64
    %ran = llvm.icmp "eq" %seen, %one : i32
    %ok = llvm.and %empty, %ran : i1
    %pass = llvm.mlir.constant(0 : i32) : i32
    %result = llvm.select %ok, %pass, %one : i1, i32
    llvm.return %result : i32
  }
}
