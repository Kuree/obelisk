// RUN: obelisk-opt %s --convert-obelisk-sim-processes-to-llvm-coroutines | FileCheck %s
// Exercise the final native legalization boundary directly. All vector
// memory accesses keep the original byte alignment and footprint.
module attributes {llvm.data_layout = "e-m:e-p:64:64-i64:64-n8:16:32:64-S128", llvm.target_triple = "x86_64-unknown-linux-gnu"} {
  // CHECK-LABEL: llvm.func @bitwise
  // CHECK: llvm.load {{.*}} {alignment = 1 : i64} : !llvm.ptr -> vector<1024xi64>
  // CHECK: llvm.mlir.constant(dense<-1> : vector<1024xi64>)
  // CHECK: llvm.and {{.*}} : vector<1024xi64>
  // CHECK: llvm.or {{.*}} : vector<1024xi64>
  // CHECK: llvm.xor {{.*}} : vector<1024xi64>
  // CHECK: llvm.select {{.*}} : i1, vector<1024xi64>
  // CHECK: llvm.store {{.*}} : vector<1024xi64>, !llvm.ptr
  // CHECK: llvm.icmp "eq" {{.*}} : vector<1024xi64>
  // CHECK: "llvm.intr.vector.reduce.and"{{.*}} : (vector<1024xi1>) -> i1
  llvm.func @bitwise(%p: !llvm.ptr, %q: !llvm.ptr, %choose: i1) -> i1 {
    %a = llvm.load %p {alignment = 1 : i64} : !llvm.ptr -> i65536
    %ones = llvm.mlir.constant(-1 : i65536) : i65536
    %and = llvm.and %a, %ones : i65536
    %or = llvm.or %a, %ones : i65536
    %xor = llvm.xor %and, %or : i65536
    %select = llvm.select %choose, %xor, %a : i1, i65536
    llvm.store %select, %q {alignment = 1 : i64} : i65536, !llvm.ptr
    %eq = llvm.icmp "eq" %a, %select : i65536
    llvm.return %eq : i1
  }
  // A non-power-of-two word count is legal, without rounding storage size.
  // CHECK-LABEL: llvm.func @unequal
  // CHECK: llvm.load {{.*}} : !llvm.ptr -> vector<65xi64>
  // CHECK: llvm.icmp "ne" {{.*}} : vector<65xi64>
  // CHECK: "llvm.intr.vector.reduce.or"{{.*}} : (vector<65xi1>) -> i1
  llvm.func @unequal(%p: !llvm.ptr) -> i1 {
    %a = llvm.load %p {alignment = 1 : i64} : !llvm.ptr -> i4160
    %zero = llvm.mlir.constant(0 : i4160) : i4160
    %ne = llvm.icmp "ne" %a, %zero : i4160
    llvm.return %ne : i1
  }
  // CHECK-LABEL: llvm.func @carry
  // CHECK: llvm.load {{.*}} -> i65536
  // CHECK: llvm.add {{.*}} : i65536
  // CHECK: llvm.xor {{.*}} : i65536
  llvm.func @carry(%p: !llvm.ptr, %q: !llvm.ptr) {
    %a = llvm.load %p : !llvm.ptr -> i65536
    %one = llvm.mlir.constant(1 : i65536) : i65536
    %sum = llvm.add %a, %one : i65536
    %xor = llvm.xor %sum, %a : i65536
    llvm.store %xor, %q : i65536, !llvm.ptr
    llvm.return
  }
  // CHECK-LABEL: llvm.func @partial_word
  // CHECK: llvm.load {{.*}} -> i4097
  llvm.func @partial_word(%p: !llvm.ptr, %q: !llvm.ptr) {
    %a = llvm.load %p : !llvm.ptr -> i4097
    llvm.store %a, %q : i4097, !llvm.ptr
    llvm.return
  }
  // CHECK-LABEL: llvm.func @narrow
  // CHECK: llvm.load {{.*}} -> i64
  // CHECK: llvm.xor {{.*}} : i64
  llvm.func @narrow(%p: !llvm.ptr, %q: !llvm.ptr) {
    %a = llvm.load %p : !llvm.ptr -> i64
    %ones = llvm.mlir.constant(-1 : i64) : i64
    %b = llvm.xor %a, %ones : i64
    llvm.store %b, %q : i64, !llvm.ptr
    llvm.return
  }
  // CHECK-LABEL: llvm.func @volatile
  // CHECK: llvm.load volatile {{.*}} -> i65536
  llvm.func @volatile(%p: !llvm.ptr, %q: !llvm.ptr) {
    %a = llvm.load volatile %p : !llvm.ptr -> i65536
    llvm.store %a, %q : i65536, !llvm.ptr
    llvm.return
  }
  // CHECK-LABEL: llvm.func @abi(%{{.*}}: i65536,
  // CHECK: llvm.store {{.*}} : i65536, !llvm.ptr
  llvm.func @abi(%a: i65536, %p: !llvm.ptr) {
    llvm.store %a, %p : i65536, !llvm.ptr
    llvm.return
  }
  // CHECK-LABEL: llvm.func @implicit_alignment
  // CHECK: llvm.load {{.*}} {alignment = 8 : i64} : !llvm.ptr -> vector<65xi64>
  // CHECK: llvm.store {{.*}} {alignment = 8 : i64} : vector<65xi64>, !llvm.ptr
  llvm.func @implicit_alignment(%p: !llvm.ptr, %q: !llvm.ptr) {
    %a = llvm.load %p : !llvm.ptr -> i4160
    llvm.store %a, %q : i4160, !llvm.ptr
    llvm.return
  }
}
