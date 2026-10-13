// RUN: obelisk-opt %s --address-native-state-through-bases | FileCheck %s
// RUN: obelisk-opt %s --address-native-state-through-bases --address-native-state-through-bases | FileCheck %s

// Every access in a function reaches each state plane through one invariant,
// dereferenceable load of the plane address. The optimizer cannot fold that
// load to the plane global, so accesses in different blocks stay offsets from
// one base instead of constant expressions rematerialized per block. When all
// accesses share one block, code generation already reaches nearby offsets
// from one folded plane address, so such a function keeps the plane global. So does a
// coroutine, which would otherwise carry the base in its frame across
// suspensions. Running the pass again reuses the base globals.

// CHECK: llvm.mlir.global internal @__obelisk_state_value(
// CHECK-NEXT: llvm.mlir.global internal @__obelisk_state_value_base() {{.*}}externally_initialized} : !llvm.ptr {
// CHECK-NEXT: %[[PLANE:.*]] = llvm.mlir.addressof @__obelisk_state_value : !llvm.ptr
// CHECK-NEXT: llvm.return %[[PLANE]] : !llvm.ptr
// CHECK: llvm.mlir.global internal @__obelisk_state_unknown(
// CHECK-NEXT: llvm.mlir.global internal @__obelisk_state_unknown_base() {{.*}}externally_initialized}
module {
  llvm.mlir.global internal @__obelisk_state_value(dense<0> : tensor<64xi8>) {addr_space = 0 : i32} : !llvm.array<64 x i8>
  llvm.mlir.global internal @__obelisk_state_unknown(dense<0> : tensor<64xi8>) {addr_space = 0 : i32} : !llvm.array<64 x i8>

  // CHECK-LABEL: llvm.func @eval(
  // CHECK-NEXT: %[[VSLOT:.*]] = llvm.mlir.addressof @__obelisk_state_value_base : !llvm.ptr
  // CHECK-NEXT: %[[VALUE:.*]] = llvm.load %[[VSLOT]] invariant dereferenceable<bytes = 64> {alignment = 8 : i64} : !llvm.ptr -> !llvm.ptr
  // CHECK-NEXT: %[[USLOT:.*]] = llvm.mlir.addressof @__obelisk_state_unknown_base : !llvm.ptr
  // CHECK-NEXT: %[[UNKNOWN:.*]] = llvm.load %[[USLOT]] invariant dereferenceable<bytes = 64> {alignment = 8 : i64} : !llvm.ptr -> !llvm.ptr
  // CHECK-NOT: llvm.mlir.addressof @__obelisk_state_
  // CHECK: llvm.getelementptr inbounds %[[VALUE]][40]
  // CHECK: llvm.getelementptr inbounds %[[UNKNOWN]][40]
  // CHECK: ^bb1:
  // CHECK: llvm.getelementptr inbounds %[[VALUE]][48]
  // CHECK: llvm.return
  llvm.func @eval(%c: i1) -> i8 {
    %v = llvm.mlir.addressof @__obelisk_state_value : !llvm.ptr
    %p = llvm.getelementptr inbounds %v[40] : (!llvm.ptr) -> !llvm.ptr, i8
    %a = llvm.load %p : !llvm.ptr -> i8
    %u = llvm.mlir.addressof @__obelisk_state_unknown : !llvm.ptr
    %q = llvm.getelementptr inbounds %u[40] : (!llvm.ptr) -> !llvm.ptr, i8
    %x = llvm.load %q : !llvm.ptr -> i8
    llvm.cond_br %c, ^bb1, ^bb2
  ^bb1:
    %r = llvm.getelementptr inbounds %v[48] : (!llvm.ptr) -> !llvm.ptr, i8
    llvm.store %x, %r : i8, !llvm.ptr
    llvm.br ^bb2
  ^bb2:
    llvm.return %a : i8
  }

  // CHECK-LABEL: llvm.func @process(
  // CHECK: llvm.mlir.addressof @__obelisk_state_value : !llvm.ptr
  // CHECK-NOT: _base
  // CHECK: llvm.return
  llvm.func @process() -> !llvm.ptr {
    %align = llvm.mlir.constant(8 : i32) : i32
    %null = llvm.mlir.zero : !llvm.ptr
    %id = llvm.intr.coro.id %align, %null, %null, %null : (i32, !llvm.ptr, !llvm.ptr, !llvm.ptr) -> !llvm.token
    %handle = llvm.intr.coro.begin %id, %null : (!llvm.token, !llvm.ptr) -> !llvm.ptr
    %v = llvm.mlir.addressof @__obelisk_state_value : !llvm.ptr
    %p = llvm.getelementptr inbounds %v[8] : (!llvm.ptr) -> !llvm.ptr, i8
    %one = llvm.mlir.constant(1 : i8) : i8
    llvm.store %one, %p : i8, !llvm.ptr
    llvm.return %handle : !llvm.ptr
  }

  // CHECK-LABEL: llvm.func @single(
  // CHECK-NEXT: llvm.mlir.addressof @__obelisk_state_value : !llvm.ptr
  // CHECK-NOT: _base
  // CHECK: llvm.return
  llvm.func @single() -> i8 {
    %v = llvm.mlir.addressof @__obelisk_state_value : !llvm.ptr
    %p = llvm.getelementptr inbounds %v[16] : (!llvm.ptr) -> !llvm.ptr, i8
    %a = llvm.load %p : !llvm.ptr -> i8
    %w = llvm.mlir.addressof @__obelisk_state_value : !llvm.ptr
    %q = llvm.getelementptr inbounds %w[24] : (!llvm.ptr) -> !llvm.ptr, i8
    llvm.store %a, %q : i8, !llvm.ptr
    llvm.return %a : i8
  }

  // A function that never touches state is unchanged.
  // CHECK-LABEL: llvm.func @pure(
  // CHECK-NEXT: llvm.return
  llvm.func @pure() {
    llvm.return
  }
}
