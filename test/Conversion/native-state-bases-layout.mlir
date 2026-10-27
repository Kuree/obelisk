// RUN: %split-file %s %t
// RUN: talos-opt %t/32.mlir --address-native-state-through-bases | FileCheck %s --check-prefix=INDEX32
// RUN: talos-opt %t/32.mlir --address-native-state-through-bases | talos-translate --mlir-to-llvmir | %llvm_dist/bin/opt -S -passes=verify | FileCheck %s --check-prefix=LLVM32
// RUN: talos-opt %t/wide.mlir --address-native-state-through-bases | FileCheck %s --check-prefix=INDEX32
// RUN: talos-opt %t/wide.mlir --address-native-state-through-bases | talos-translate --mlir-to-llvmir | %llvm_dist/bin/opt -S -passes=verify | FileCheck %s --check-prefix=LLVM32
// RUN: talos-opt %t/empty.mlir --address-native-state-through-bases | FileCheck %s --check-prefix=EMPTY
// RUN: talos-opt %t/empty.mlir --address-native-state-through-bases | talos-translate --mlir-to-llvmir | %llvm_dist/bin/opt -S -passes=verify | FileCheck %s --check-prefix=LLVMEMPTY

// Pointer loads retain the target pointer type. Dereferenceable bundle byte
// counts are i64 even with 32-bit pointers or a narrower pointer index.
// INDEX32: llvm.mlir.global internal constant @__talos_state_value_base()
// INDEX32: llvm.mlir.global internal constant @__talos_state_unknown_base()
// INDEX32-LABEL: llvm.func @eval(
// INDEX32: %[[V:.*]] = llvm.load {{.*}} invariant dereferenceable<bytes = 64>
// INDEX32: %[[X:.*]] = llvm.load {{.*}} invariant dereferenceable<bytes = 32>
// INDEX32: %[[VSIZE:.*]] = llvm.mlir.constant(64 : i64) : i64
// INDEX32: %[[XSIZE:.*]] = llvm.mlir.constant(32 : i64) : i64
// INDEX32: llvm.intr.assume {{.*}} ["separate_storage"(%[[V]], %[[X]] : !llvm.ptr, !llvm.ptr), "dereferenceable"(%[[V]], %[[VSIZE]] : !llvm.ptr, i64), "dereferenceable"(%[[X]], %[[XSIZE]] : !llvm.ptr, i64)] : i1
// LLVM32: @__talos_state_value_base = internal externally_initialized constant ptr @__talos_state_value, align 8
// LLVM32-LABEL: define i8 @eval(
// LLVM32: %[[V:.*]] = load ptr, ptr @__talos_state_value_base
// LLVM32: %[[X:.*]] = load ptr, ptr @__talos_state_unknown_base
// LLVM32: call void @llvm.assume(i1 true) [ "separate_storage"(ptr %[[V]], ptr %[[X]]), "dereferenceable"(ptr %[[V]], i64 64), "dereferenceable"(ptr %[[X]], i64 32) ]

// Empty planes add no positive byte-count or explicit nonnull promise.
// EMPTY-LABEL: llvm.func @empty(
// EMPTY: llvm.load {{.*}} invariant dereferenceable<bytes = 0>
// EMPTY: llvm.load {{.*}} invariant dereferenceable<bytes = 0>
// EMPTY: %[[VSIZE:.*]] = llvm.mlir.constant(0 : i64) : i64
// EMPTY: %[[XSIZE:.*]] = llvm.mlir.constant(0 : i64) : i64
// EMPTY: llvm.intr.assume {{.*}} "dereferenceable"({{.*}}, %[[VSIZE]] : !llvm.ptr, i64), "dereferenceable"({{.*}}, %[[XSIZE]] : !llvm.ptr, i64)] : i1
// LLVMEMPTY-LABEL: define ptr @empty(
// LLVMEMPTY: call void @llvm.assume(i1 true) [ "separate_storage"(ptr {{.*}}, ptr {{.*}}), "dereferenceable"(ptr {{.*}}, i64 0), "dereferenceable"(ptr {{.*}}, i64 0) ]

//--- 32.mlir
module attributes {llvm.data_layout = "e-p:32:32"} {
  llvm.mlir.global internal @__talos_state_value(dense<0> : tensor<64xi8>) : !llvm.array<64 x i8>
  llvm.mlir.global internal @__talos_state_unknown(dense<0> : tensor<32xi8>) : !llvm.array<32 x i8>
  llvm.func @eval(%c: i1) -> i8 {
    %vp = llvm.mlir.addressof @__talos_state_value : !llvm.ptr
    %xp = llvm.mlir.addressof @__talos_state_unknown : !llvm.ptr
    %a = llvm.load %vp : !llvm.ptr -> i8
    %b = llvm.load %xp : !llvm.ptr -> i8
    llvm.cond_br %c, ^update, ^done
  ^update:
    llvm.store %b, %vp : i8, !llvm.ptr
    llvm.br ^done
  ^done:
    llvm.return %a : i8
  }
}

//--- wide.mlir
module attributes {llvm.data_layout = "e-p:64:64:64:32"} {
  llvm.mlir.global internal @__talos_state_value(dense<0> : tensor<64xi8>) : !llvm.array<64 x i8>
  llvm.mlir.global internal @__talos_state_unknown(dense<0> : tensor<32xi8>) : !llvm.array<32 x i8>
  llvm.func @eval(%c: i1) -> i8 {
    %vp = llvm.mlir.addressof @__talos_state_value : !llvm.ptr
    %xp = llvm.mlir.addressof @__talos_state_unknown : !llvm.ptr
    %a = llvm.load %vp : !llvm.ptr -> i8
    %b = llvm.load %xp : !llvm.ptr -> i8
    llvm.cond_br %c, ^update, ^done
  ^update:
    llvm.store %b, %vp : i8, !llvm.ptr
    llvm.br ^done
  ^done:
    llvm.return %a : i8
  }
}

//--- empty.mlir
module {
  llvm.mlir.global internal @__talos_state_value(dense<> : tensor<0xi8>) : !llvm.array<0 x i8>
  llvm.mlir.global internal @__talos_state_unknown(dense<> : tensor<0xi8>) : !llvm.array<0 x i8>
  llvm.func @empty(%c: i1) -> !llvm.ptr {
    %vp = llvm.mlir.addressof @__talos_state_value : !llvm.ptr
    %xp = llvm.mlir.addressof @__talos_state_unknown : !llvm.ptr
    llvm.cond_br %c, ^value, ^unknown
  ^value:
    llvm.return %vp : !llvm.ptr
  ^unknown:
    llvm.return %xp : !llvm.ptr
  }
}
