// RUN: talos-opt %s --convert-native-schedule-actions-to-llvm -o %t.lowered
// RUN: FileCheck %s < %t.lowered
// RUN: %llvm_dist/bin/mlir-opt %t.lowered --convert-arith-to-llvm --convert-cf-to-llvm | talos-translate --mlir-to-llvmir > %t.ll
// RUN: %llvm_dist/bin/clang -O0 %t.ll -o %t.o0
// RUN: %t.o0
// RUN: %llvm_dist/bin/clang -O3 %t.ll -o %t.o3
// RUN: %t.o3
// Local reads see ordered consume/add actions. Opaque calls see coherent leaves
// and summaries, and later local reads include the callback's modifications.
// CHECK-NOT: schedule.ready
// CHECK-LABEL: llvm.func @exercise
// CHECK-NOT: llvm.alloca
// CHECK: llvm.call @observe
// CHECK-NOT: schedule.ready
module {
  llvm.mlir.global internal @ready(dense<0> : tensor<66xi64>) : !llvm.array<66 x i64>
  llvm.mlir.global internal @error(0 : i32) : i32
  llvm.func @observe() -> i1 attributes {passthrough = ["noinline"]} {
    %base = llvm.mlir.addressof @ready : !llvm.ptr
    %last = llvm.getelementptr %base[63] : (!llvm.ptr) -> !llvm.ptr, i64
    %summary = llvm.getelementptr %base[65] : (!llvm.ptr) -> !llvm.ptr, i64
    %zero = llvm.mlir.constant(0 : i64) : i64
    %one = llvm.mlir.constant(1 : i64) : i64
    %eight = llvm.mlir.constant(8 : i64) : i64
    %high = llvm.mlir.constant(-9223372036854775808 : i64) : i64
    %first = llvm.load %base : !llvm.ptr -> i64
    %end = llvm.load %last : !llvm.ptr -> i64
    %bits = llvm.load %summary : !llvm.ptr -> i64
    %a = llvm.icmp "eq" %first, %zero : i64
    %b = llvm.icmp "eq" %end, %high : i64
    %c = llvm.icmp "eq" %bits, %high : i64
    %ab = llvm.and %a, %b : i1
    %ok = llvm.and %ab, %c : i1
    llvm.store %eight, %base : i64, !llvm.ptr
    llvm.store %zero, %last : i64, !llvm.ptr
    llvm.store %one, %summary : i64, !llvm.ptr
    llvm.return %ok : i1
  }
  llvm.func @exercise() {
    %base = llvm.mlir.addressof @ready : !llvm.ptr
    %error = llvm.mlir.addressof @error : !llvm.ptr
    %zero = arith.constant 0 : i64
    %one = arith.constant 1 : i64
    %two = arith.constant 2 : i64
    %eight = arith.constant 8 : i64
    %ten = arith.constant 10 : i64
    %high = arith.constant -9223372036854775808 : i64
    %pass = arith.constant 0 : i32
    %fail = arith.constant 1 : i32
    schedule.ready.region {
      schedule.ready.update %base, %one {capacity = 4096 : i64, word = 0 : i64, consume = false} : (!llvm.ptr, i64) -> ()
      %first = schedule.ready.read %base {capacity = 4096 : i64, word = 0 : i64} : (!llvm.ptr) -> i64
      schedule.ready.update %base, %one {capacity = 4096 : i64, word = 0 : i64, consume = true} : (!llvm.ptr, i64) -> ()
      schedule.ready.update %base, %high {capacity = 4096 : i64, word = 63 : i64, consume = false} : (!llvm.ptr, i64) -> ()
      %empty = schedule.ready.read %base {capacity = 4096 : i64, word = 0 : i64} : (!llvm.ptr) -> i64
      %last = schedule.ready.read %base {capacity = 4096 : i64, word = 63 : i64} : (!llvm.ptr) -> i64
      %a = arith.cmpi eq, %first, %one : i64
      %b = arith.cmpi eq, %empty, %zero : i64
      %c = arith.cmpi eq, %last, %high : i64
      %observed = llvm.call @observe() : () -> i1
      %after = schedule.ready.read %base {capacity = 4096 : i64, word = 0 : i64} : (!llvm.ptr) -> i64
      schedule.ready.update %base, %two {capacity = 4096 : i64, word = 0 : i64, consume = false} : (!llvm.ptr, i64) -> ()
      %final = schedule.ready.read %base {capacity = 4096 : i64, word = 0 : i64} : (!llvm.ptr) -> i64
      %d = arith.cmpi eq, %after, %eight : i64
      %e = arith.cmpi eq, %final, %ten : i64
      %ab = arith.andi %a, %b : i1
      %abc = arith.andi %ab, %c : i1
      %de = arith.andi %d, %e : i1
      %readok = arith.andi %abc, %de : i1
      %ok = arith.andi %readok, %observed : i1
      %code = arith.select %ok, %pass, %fail : i32
      llvm.store %code, %error : i32, !llvm.ptr
      schedule.ready.yield
    }
    llvm.return
  }
  llvm.func @main() -> i32 {
    llvm.call @exercise() : () -> ()
    %base = llvm.mlir.addressof @ready : !llvm.ptr
    %error = llvm.mlir.addressof @error : !llvm.ptr
    %ten = llvm.mlir.constant(10 : i64) : i64
    %value = llvm.load %base : !llvm.ptr -> i64
    %code = llvm.load %error : !llvm.ptr -> i32
    %ok = llvm.icmp "eq" %value, %ten : i64
    %fail = llvm.mlir.constant(1 : i32) : i32
    %result = llvm.select %ok, %code, %fail : i1, i32
    llvm.return %result : i32
  }
}
