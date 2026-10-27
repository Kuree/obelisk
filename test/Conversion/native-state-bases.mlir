// RUN: talos-opt %s --address-native-state-through-bases | FileCheck %s
// RUN: talos-opt %s --address-native-state-through-bases --address-native-state-through-bases | FileCheck %s
// RUN: talos-opt %s --address-native-state-through-bases | talos-translate --mlir-to-llvmir | %llvm_dist/bin/opt -S -passes='default<O3>' | %llvm_dist/bin/llc -O3 -mtriple=aarch64-unknown-linux-gnu | FileCheck %s --check-prefix=ASM

// Every access in a function reaches each state plane through one invariant,
// dereferenceable load of the plane address. The optimizer cannot fold that
// load to the plane global, so accesses in different blocks stay offsets from
// one base instead of constant expressions rematerialized per block. When all
// accesses share one block, code generation already reaches nearby offsets
// from one folded plane address, so such a function keeps the plane global.
// So does a coroutine, which would otherwise carry the base in its frame across
// suspensions. Running the pass again reuses the base globals.

// The loaded value base serves both offsets across a branch, with no runtime
// instruction for the assumption.
// ASM-LABEL: eval:
// ASM: ldr [[BASE:x[0-9]+]],
// ASM: ldrb {{w[0-9]+}}, {{\[}}[[BASE]], #40]
// ASM: tbz
// ASM: strb {{w[0-9]+}}, {{\[}}[[BASE]], #48]
// ASM: ret

// CHECK: llvm.mlir.global internal @__talos_state_value(
// CHECK-NEXT: llvm.mlir.global internal constant @__talos_state_value_base() {{.*}}externally_initialized} : !llvm.ptr {
// CHECK-NEXT: %[[PLANE:.*]] = llvm.mlir.addressof @__talos_state_value : !llvm.ptr
// CHECK-NEXT: llvm.return %[[PLANE]] : !llvm.ptr
// CHECK: llvm.mlir.global internal @__talos_state_unknown(
// CHECK-NEXT: llvm.mlir.global internal constant @__talos_state_unknown_base() {{.*}}externally_initialized}
module {
  llvm.mlir.global internal @__talos_state_value(dense<0> : tensor<64xi8>) {addr_space = 0 : i32} : !llvm.array<64 x i8>
  llvm.mlir.global internal @__talos_state_unknown(dense<0> : tensor<64xi8>) {addr_space = 0 : i32} : !llvm.array<64 x i8>

  // CHECK-LABEL: llvm.func @eval(
  // CHECK-NEXT: %[[VSLOT:.*]] = llvm.mlir.addressof @__talos_state_value_base : !llvm.ptr
  // CHECK-NEXT: %[[VALUE:.*]] = llvm.load %[[VSLOT]] invariant dereferenceable<bytes = 64> {alignment = 8 : i64} : !llvm.ptr -> !llvm.ptr
  // CHECK-NEXT: %[[USLOT:.*]] = llvm.mlir.addressof @__talos_state_unknown_base : !llvm.ptr
  // CHECK-NEXT: %[[UNKNOWN:.*]] = llvm.load %[[USLOT]] invariant dereferenceable<bytes = 64> {alignment = 8 : i64} : !llvm.ptr -> !llvm.ptr
  // CHECK-NEXT: %[[TRUE:.*]] = llvm.mlir.constant(true) : i1
  // CHECK-NEXT: %[[VBYTES:.*]] = llvm.mlir.constant(64 : i64) : i64
  // CHECK-NEXT: %[[XBYTES:.*]] = llvm.mlir.constant(64 : i64) : i64
  // CHECK-NEXT: llvm.intr.assume %[[TRUE]] ["separate_storage"(%[[VALUE]], %[[UNKNOWN]] : !llvm.ptr, !llvm.ptr), "dereferenceable"(%[[VALUE]], %[[VBYTES]] : !llvm.ptr, i64), "dereferenceable"(%[[UNKNOWN]], %[[XBYTES]] : !llvm.ptr, i64)] : i1
  // CHECK-NOT: llvm.mlir.addressof @__talos_state_
  // CHECK: llvm.getelementptr inbounds %[[VALUE]][40]
  // CHECK: llvm.getelementptr inbounds %[[UNKNOWN]][40]
  // CHECK: ^bb1:
  // CHECK: llvm.getelementptr inbounds %[[VALUE]][48]
  // CHECK: llvm.return
  llvm.func @eval(%c: i1) -> i8 {
    %v = llvm.mlir.addressof @__talos_state_value : !llvm.ptr
    %p = llvm.getelementptr inbounds %v[40] : (!llvm.ptr) -> !llvm.ptr, i8
    %a = llvm.load %p : !llvm.ptr -> i8
    %u = llvm.mlir.addressof @__talos_state_unknown : !llvm.ptr
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

  // A multi-block function using only one plane keeps the original setup.
  // CHECK-LABEL: llvm.func @one_plane(
  // CHECK-NEXT: %[[SLOT:.*]] = llvm.mlir.addressof @__talos_state_value_base
  // CHECK-NEXT: %[[BASE:.*]] = llvm.load %[[SLOT]] invariant dereferenceable<bytes = 64>
  // CHECK-NEXT: llvm.load %[[BASE]] : !llvm.ptr -> i8
  // CHECK-NEXT: llvm.cond_br
  // CHECK-NOT: llvm.intr.assume
  // CHECK-NOT: llvm.mlir.constant
  // CHECK: llvm.return
  llvm.func @one_plane(%c: i1, %x: i8) -> i8 {
    %vp = llvm.mlir.addressof @__talos_state_value : !llvm.ptr
    %a = llvm.load %vp : !llvm.ptr -> i8
    llvm.cond_br %c, ^update, ^done
  ^update:
    llvm.store %x, %vp : i8, !llvm.ptr
    llvm.br ^done
  ^done:
    llvm.return %a : i8
  }

  // CHECK-LABEL: llvm.func @process(
  // CHECK: llvm.mlir.addressof @__talos_state_value : !llvm.ptr
  // CHECK-NOT: _base
  // CHECK: llvm.return
  llvm.func @process() -> !llvm.ptr {
    %align = llvm.mlir.constant(8 : i32) : i32
    %null = llvm.mlir.zero : !llvm.ptr
    %id = llvm.intr.coro.id %align, %null, %null, %null : (i32, !llvm.ptr, !llvm.ptr, !llvm.ptr) -> !llvm.token
    %handle = llvm.intr.coro.begin %id, %null : (!llvm.token, !llvm.ptr) -> !llvm.ptr
    %v = llvm.mlir.addressof @__talos_state_value : !llvm.ptr
    %p = llvm.getelementptr inbounds %v[8] : (!llvm.ptr) -> !llvm.ptr, i8
    %one = llvm.mlir.constant(1 : i8) : i8
    llvm.store %one, %p : i8, !llvm.ptr
    llvm.return %handle : !llvm.ptr
  }

  // CHECK-LABEL: llvm.func @single(
  // CHECK-NEXT: llvm.mlir.addressof @__talos_state_value : !llvm.ptr
  // CHECK-NOT: _base
  // CHECK: llvm.return
  llvm.func @single() -> i8 {
    %v = llvm.mlir.addressof @__talos_state_value : !llvm.ptr
    %p = llvm.getelementptr inbounds %v[16] : (!llvm.ptr) -> !llvm.ptr, i8
    %a = llvm.load %p : !llvm.ptr -> i8
    %w = llvm.mlir.addressof @__talos_state_value : !llvm.ptr
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
