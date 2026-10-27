// RUN: talos-opt %s --address-native-state-through-bases -o %t.mlir
// RUN: FileCheck %s --check-prefix=IR < %t.mlir
// RUN: talos-opt %s --address-native-state-through-bases --address-native-state-through-bases | diff %t.mlir -
// RUN: talos-opt %s --mlir-disable-threading --address-native-state-through-bases | diff %t.mlir -
// RUN: talos-translate --mlir-to-llvmir %t.mlir | %llvm_dist/bin/opt -S -passes='default<O3>' | FileCheck %s --check-prefix=OPT

// Opaque bases must preserve separation of the physical value and X/Z arrays.
// An arbitrary pointer or overlapping bytes in the same plane can still alias.
// IEEE 1800-2023 4.3, 4.6 and 6.3.1 require
// the same ordered state updates. The runtime companion checks aliasing with
// a pointer argument and differently sized overlapping accesses at O0/O3.
// IR: llvm.mlir.global internal constant @__talos_state_value_base()
// IR: llvm.mlir.global internal constant @__talos_state_unknown_base()
// IR-LABEL: llvm.func @load_across_unknown_store(
// IR: %[[VSLOT:.*]] = llvm.mlir.addressof @__talos_state_value_base
// IR-NEXT: %[[V:.*]] = llvm.load %[[VSLOT]] invariant dereferenceable<bytes = 64>
// IR-NEXT: %[[XSLOT:.*]] = llvm.mlir.addressof @__talos_state_unknown_base
// IR-NEXT: %[[X:.*]] = llvm.load %[[XSLOT]] invariant dereferenceable<bytes = 64>
// IR: llvm.intr.assume {{.*}} ["separate_storage"(%[[V]], %[[X]] : !llvm.ptr, !llvm.ptr), "dereferenceable"(%[[V]], {{.*}} : !llvm.ptr, i64), "dereferenceable"(%[[X]], {{.*}} : !llvm.ptr, i64)] : i1
// IR-NOT: alias_scopes
// IR-NOT: noalias_scopes
// IR-NOT: tbaa
// IR-LABEL: llvm.func @store_across_unknown_load(
// The second value load is redundant even when the X/Z store executes.
// OPT-LABEL: define {{.*}}@load_across_unknown_store(
// OPT: %[[BEFORE:.*]] = load i8,
// OPT-NOT: load i8,
// OPT: shl i8 %[[BEFORE]], 1
// OPT-NOT: load i8,
// OPT: ret i8
// The value store before the conditional X/Z load is dead on every path.
// OPT-LABEL: define {{.*}}@store_across_unknown_load(
// OPT-NOT: store i8 %1,
// OPT: load i8,
// OPT-NOT: store i8 %1,
// OPT: store i8 %2,
// OPT-NOT: store i8 %1,
// OPT: ret i8
// OPT-LABEL: define {{.*}}@load_across_ready_store(
// OPT: %[[BEFORE:.*]] = load i8,
// OPT-NOT: load i8,
// OPT: shl i8 %[[BEFORE]], 1
// OPT: ret i8
// An arbitrary argument can point into the value plane: keep the reload.
// Both bases are used, so this exercises the separate_storage assumption.
// IR-LABEL: llvm.func @unknown_pointer(
// IR: llvm.intr.assume
// OPT-LABEL: define {{.*}}@unknown_pointer(
// OPT: load i8,
// OPT: store i8
// OPT: load i8,
// OPT: ret i8
// Differently sized accesses can overlap within the same physical plane.
// IR-LABEL: llvm.func @overlap(
// IR: llvm.intr.assume
// OPT-LABEL: define {{.*}}@overlap(
// OPT: load i16,
// OPT: store i8
// OPT: load i16,
// OPT: ret i16
// The original unproved pointer arithmetic gets no new inbounds promise.
// IR-LABEL: llvm.func @unproved_range(
// IR: llvm.getelementptr {{.*}}[%arg0]
// IR: llvm.getelementptr {{.*}}[-1]
// IR: llvm.getelementptr {{.*}}[64]
// IR-NOT: alias_scopes
// IR-NOT: noalias_scopes
// IR-NOT: tbaa
// IR: llvm.return
module attributes {llvm.data_layout = "e-m:e-p:64:64-i64:64-n32:64-S128", llvm.target_triple = "aarch64-unknown-linux-gnu"} {
  llvm.mlir.global internal @__talos_state_value(dense<0> : tensor<64xi8>) : !llvm.array<64 x i8>
  llvm.mlir.global internal @__talos_state_unknown(dense<0> : tensor<64xi8>) : !llvm.array<64 x i8>
  llvm.mlir.global internal @ready(0 : i8) : i8
  llvm.func @seed(%v: i8, %x: i8) {
    %vp = llvm.mlir.addressof @__talos_state_value : !llvm.ptr
    %xp = llvm.mlir.addressof @__talos_state_unknown : !llvm.ptr
    llvm.store %v, %vp : i8, !llvm.ptr
    llvm.store %x, %xp : i8, !llvm.ptr
    llvm.return
  }
  llvm.func @read_ready() -> i8 {
    %rp = llvm.mlir.addressof @ready : !llvm.ptr
    %r = llvm.load %rp : !llvm.ptr -> i8
    llvm.return %r : i8
  }
  llvm.func @load_across_unknown_store(%c: i1, %x: i8) -> i8 {
    %vp = llvm.mlir.addressof @__talos_state_value : !llvm.ptr
    %xp = llvm.mlir.addressof @__talos_state_unknown : !llvm.ptr
    %before = llvm.load %vp : !llvm.ptr -> i8
    llvm.cond_br %c, ^update, ^done
  ^update:
    llvm.store %x, %xp : i8, !llvm.ptr
    llvm.br ^done
  ^done:
    %after = llvm.load %vp : !llvm.ptr -> i8
    %result = llvm.add %before, %after : i8
    llvm.return %result : i8
  }
  llvm.func @store_across_unknown_load(%c: i1, %v: i8, %next: i8) -> i8 {
    %vp = llvm.mlir.addressof @__talos_state_value : !llvm.ptr
    %xp = llvm.mlir.addressof @__talos_state_unknown : !llvm.ptr
    llvm.store %v, %vp : i8, !llvm.ptr
    llvm.cond_br %c, ^inspect, ^skip
  ^inspect:
    %unknown = llvm.load %xp : !llvm.ptr -> i8
    llvm.br ^done(%unknown : i8)
  ^skip:
    llvm.br ^done(%v : i8)
  ^done(%result: i8):
    llvm.store %next, %vp : i8, !llvm.ptr
    llvm.return %result : i8
  }
  llvm.func @load_across_ready_store(%c: i1, %x: i8) -> i8 {
    %vp = llvm.mlir.addressof @__talos_state_value : !llvm.ptr
    %xp = llvm.mlir.addressof @ready : !llvm.ptr
    %before = llvm.load %vp : !llvm.ptr -> i8
    llvm.cond_br %c, ^update, ^done
  ^update:
    llvm.store %x, %xp : i8, !llvm.ptr
    llvm.br ^done
  ^done:
    %after = llvm.load %vp : !llvm.ptr -> i8
    %result = llvm.add %before, %after : i8
    llvm.return %result : i8
  }
  llvm.func @unknown_pointer(%p: !llvm.ptr, %c: i1, %x: i8) -> i8 {
    %vp = llvm.mlir.addressof @__talos_state_value : !llvm.ptr
    %xp = llvm.mlir.addressof @__talos_state_unknown : !llvm.ptr
    llvm.store %x, %xp : i8, !llvm.ptr
    %before = llvm.load %vp : !llvm.ptr -> i8
    llvm.cond_br %c, ^update, ^done
  ^update:
    llvm.store %x, %p : i8, !llvm.ptr
    llvm.br ^done
  ^done:
    %after = llvm.load %vp : !llvm.ptr -> i8
    %result = llvm.add %before, %after : i8
    llvm.return %result : i8
  }
  llvm.func @overlap(%c: i1, %byte: i8) -> i16 {
    %vp = llvm.mlir.addressof @__talos_state_value : !llvm.ptr
    %xp = llvm.mlir.addressof @__talos_state_unknown : !llvm.ptr
    llvm.store %byte, %xp : i8, !llvm.ptr
    %before = llvm.load %vp : !llvm.ptr -> i16
    llvm.cond_br %c, ^update, ^done
  ^update:
    llvm.store %byte, %vp : i8, !llvm.ptr
    llvm.br ^done
  ^done:
    %after = llvm.load %vp : !llvm.ptr -> i16
    %result = llvm.add %before, %after : i16
    llvm.return %result : i16
  }
  llvm.func @unproved_range(%index: i64, %c: i1) -> i8 {
    %vp = llvm.mlir.addressof @__talos_state_value : !llvm.ptr
    %dynamic = llvm.getelementptr %vp[%index] : (!llvm.ptr, i64) -> !llvm.ptr, i8
    %negative = llvm.getelementptr %vp[-1] : (!llvm.ptr) -> !llvm.ptr, i8
    %past = llvm.getelementptr %vp[64] : (!llvm.ptr) -> !llvm.ptr, i8
    %a = llvm.load %dynamic : !llvm.ptr -> i8
    %b = llvm.load %negative : !llvm.ptr -> i8
    llvm.store %b, %past : i8, !llvm.ptr
    llvm.cond_br %c, ^read, ^done
  ^read:
    %v = llvm.load %vp : !llvm.ptr -> i8
    llvm.return %v : i8
  ^done:
    llvm.return %a : i8
  }
  llvm.func @main() -> i32 {
    %yes = llvm.mlir.constant(true) : i1
    %no = llvm.mlir.constant(false) : i1
    %five = llvm.mlir.constant(5 : i8) : i8
    %seven = llvm.mlir.constant(7 : i8) : i8
    %nine = llvm.mlir.constant(9 : i8) : i8
    %ten = llvm.mlir.constant(10 : i8) : i8
    %three = llvm.mlir.constant(3 : i8) : i8
    %fiftyfive = llvm.mlir.constant(55 : i8) : i8
    %eleven = llvm.mlir.constant(11 : i8) : i8
    %fourteen = llvm.mlir.constant(14 : i8) : i8
    llvm.call @seed(%five, %seven) : (i8, i8) -> ()
    %reused = llvm.call @load_across_unknown_store(%yes, %nine) : (i1, i8) -> i8
    %a = llvm.icmp "eq" %reused, %ten : i8
    %x = llvm.call @store_across_unknown_load(%yes, %fiftyfive, %three) : (i1, i8, i8) -> i8
    %b = llvm.icmp "eq" %x, %nine : i8
    %vp = llvm.mlir.addressof @__talos_state_value : !llvm.ptr
    %aliased = llvm.call @unknown_pointer(%vp, %yes, %eleven) : (!llvm.ptr, i1, i8) -> i8
    %c = llvm.icmp "eq" %aliased, %fourteen : i8
    %word = llvm.mlir.constant(4660 : i16) : i16
    %byte = llvm.mlir.constant(86 : i8) : i8
    %sum = llvm.mlir.constant(9354 : i16) : i16
    llvm.store %word, %vp : i16, !llvm.ptr
    %updated = llvm.call @overlap(%yes, %byte) : (i1, i8) -> i16
    %d = llvm.icmp "eq" %updated, %sum : i16
    %held = llvm.call @load_across_unknown_store(%no, %seven) : (i1, i8) -> i8
    %expected = llvm.mlir.constant(172 : i8) : i8
    %e = llvm.icmp "eq" %held, %expected : i8
    %ab = llvm.and %a, %b : i1
    %cd = llvm.and %c, %d : i1
    %abcd = llvm.and %ab, %cd : i1
    %all = llvm.and %abcd, %e : i1
    %zero = llvm.mlir.constant(0 : i32) : i32
    %one = llvm.mlir.constant(1 : i32) : i32
    %result = llvm.select %all, %zero, %one : i1, i32
    llvm.return %result : i32
  }
}
