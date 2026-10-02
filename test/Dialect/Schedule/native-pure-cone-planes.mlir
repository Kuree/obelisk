// RUN: obelisk-opt %s --schedule-cache-pure-cones='minimum-cost=2 maximum-cost=30' -o %t.cache
// RUN: obelisk-opt %t.cache | FileCheck %s
// RUN: %llvm_dist/bin/mlir-opt %t.cache --convert-arith-to-llvm --convert-cf-to-llvm | mlir-translate --mlir-to-llvmir | %llc -filetype=obj -relocation-model=pic -o %t.o
// RUN: %llvm_dist/bin/clang %t.o -o %t.exe
// RUN: %t.exe
!planes = !llvm.struct<(i64, i64)>
module {
  // CHECK-LABEL: llvm.func internal @planes
  // CHECK: schedule.pure_cache = array<i32: 0>
  // CHECK: arith.cmpi eq
  // CHECK: arith.cmpi eq
  // CHECK: cf.cond_br
  llvm.func internal @planes(%key: !planes) -> i64 {
    %v = llvm.extractvalue %key[0] : !planes
    %u = llvm.extractvalue %key[1] : !planes
    %a = arith.addi %v, %u : i64
    llvm.return %a : i64
  }
  llvm.func @main() -> i32 {
    %zero = llvm.mlir.zero : !planes
    %one = llvm.mlir.constant(1 : i64) : i64
    %key1 = llvm.insertvalue %one, %zero[0] : !planes
    %key2 = llvm.insertvalue %one, %key1[1] : !planes
    %a = llvm.call @planes(%key1) : (!planes) -> i64
    %b = llvm.call @planes(%key1) : (!planes) -> i64
    %c = llvm.call @planes(%key2) : (!planes) -> i64
    %two = arith.addi %one, %one : i64
    %ok1 = arith.cmpi eq, %a, %one : i64
    %ok2 = arith.cmpi eq, %b, %one : i64
    %ok3 = arith.cmpi eq, %c, %two : i64
    %x = arith.andi %ok1, %ok2 : i1
    %ok = arith.andi %x, %ok3 : i1
    %true = arith.constant true
    %fail = arith.xori %ok, %true : i1
    %status = arith.extui %fail : i1 to i32
    llvm.return %status : i32
  }
}
