// RUN: obelisk-opt %s --schedule-cache-pure-cones='minimum-cost=2 maximum-cost=20' -o %t.cache
// RUN: obelisk-opt %t.cache --schedule-cache-pure-cones='minimum-cost=2 maximum-cost=20' | FileCheck %s
// RUN: %llvm_dist/bin/mlir-opt %t.cache --convert-arith-to-llvm --convert-cf-to-llvm | mlir-translate --mlir-to-llvmir | %llc -filetype=obj -relocation-model=pic -o %t.o
// RUN: %llvm_dist/bin/clang %t.o -o %t.exe
// RUN: %t.exe
module {
  // CHECK: llvm.mlir.global internal thread_local @__obelisk_pure_cache.pure
  llvm.mlir.global internal @state(0 : i64) : i64
  // CHECK-LABEL: llvm.func internal @pure
  // CHECK: schedule.pure_cache = array<i32: 0, 1>
  // CHECK: cf.cond_br
  // CHECK: llvm.store
  llvm.func internal @pure(%x: i64, %u: i64) -> i64 {
    %sum = arith.addi %x, %u : i64
    %answer = arith.xori %sum, %u : i64
    llvm.return %answer : i64
  }
  // A hidden load prevents caching even when the final return is unchanged.
  // CHECK-LABEL: llvm.func internal @reader(
  // CHECK-SAME: -> i64 {
  // CHECK: llvm.load
  // CHECK: llvm.call @reader.__obelisk_pure_cone
  // CHECK: llvm.return
  llvm.func internal @reader(%x: i64) -> i64 {
    %p = llvm.mlir.addressof @state : !llvm.ptr
    %s = llvm.load %p : !llvm.ptr -> i64
    %a = arith.addi %x, %s : i64
    %b = arith.xori %a, %s : i64
    llvm.return %b : i64
  }
  // CHECK-LABEL: llvm.func internal @reader.__obelisk_pure_cone
  // CHECK-SAME: schedule.pure_cache
  // CHECK-LABEL: llvm.func @main
  llvm.func @main() -> i32 {
    %x = llvm.mlir.constant(3 : i64) : i64
    %u = llvm.mlir.constant(5 : i64) : i64
    %changed = llvm.mlir.constant(4 : i64) : i64
    %a = llvm.call @pure(%x, %u) : (i64, i64) -> i64
    %b = llvm.call @pure(%x, %u) : (i64, i64) -> i64
    %c = llvm.call @pure(%changed, %u) : (i64, i64) -> i64
    %d = llvm.call @pure(%x, %changed) : (i64, i64) -> i64
    %state = llvm.mlir.addressof @state : !llvm.ptr
    llvm.store %u, %state : i64, !llvm.ptr
    %e = llvm.call @reader(%x) : (i64) -> i64
    %f = llvm.call @reader(%x) : (i64) -> i64
    llvm.store %changed, %state : i64, !llvm.ptr
    %g = llvm.call @reader(%x) : (i64) -> i64
    %thirteen = llvm.mlir.constant(13 : i64) : i64
    %twelve = llvm.mlir.constant(12 : i64) : i64
    %three = llvm.mlir.constant(3 : i64) : i64
    %ok0 = llvm.icmp "eq" %a, %thirteen : i64
    %ok1 = llvm.icmp "eq" %b, %thirteen : i64
    %ok2 = llvm.icmp "eq" %c, %twelve : i64
    %ok3 = llvm.icmp "eq" %d, %three : i64
    %ok4 = llvm.icmp "eq" %e, %thirteen : i64
    %ok5 = llvm.icmp "eq" %f, %thirteen : i64
    %ok6 = llvm.icmp "eq" %g, %three : i64
    %and0 = llvm.and %ok0, %ok1 : i1
    %and1 = llvm.and %ok2, %ok3 : i1
    %pureok = llvm.and %and0, %and1 : i1
    %reader0 = llvm.and %ok4, %ok5 : i1
    %readerok = llvm.and %reader0, %ok6 : i1
    %ok = llvm.and %pureok, %readerok : i1
    %zero = llvm.mlir.constant(0 : i32) : i32
    %one = llvm.mlir.constant(1 : i32) : i32
    %status = llvm.select %ok, %zero, %one : i1, i32
    llvm.return %status : i32
  }
}
