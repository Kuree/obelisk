// RUN: obelisk-opt %s --pass-pipeline='builtin.module(simulation.design(simulation.func(canonicalize,mem2reg,canonicalize)))' | FileCheck %s --check-prefix=REF
// RUN: obelisk-opt %s --convert-obelisk-sim-processes-to-llvm-coroutines | FileCheck %s --check-prefix=LLVM
// RUN: obelisk-opt %s --convert-obelisk-sim-processes-to-llvm-coroutines | mlir-translate --mlir-to-llvmir | opt -passes='default<O3>,verify' -S | FileCheck %s --check-prefix=OPT

// LLVM keeps runtime-indexed storage in memory and promotes fixed fields.
// OPT-LABEL: define i426 @dynamic_update(
// OPT: alloca { [2 x i213], i8 }
// OPT-NOT: shl i426 %{{[^,]+}}, %
// OPT-NOT: lshr i426 %{{[^,]+}}, %
// OPT: ret i426
// OPT-LABEL: define i213 @static_update(
// OPT-NOT: alloca
// OPT: ret i213

// A seven-bit indexed field update must remain a narrow memory operation.
// Whole input/output snapshots still have the language's value semantics.
// REF-LABEL: simulation.func @dynamic_update
// REF: simulation.ref.alloc
// REF: simulation.ref.array_element
// REF: simulation.ref.store
// REF-NOT: simulation.array.insert_dynamic
// REF: simulation.return
// LLVM-LABEL: llvm.func @dynamic_update
// LLVM: llvm.alloca
// LLVM-NOT: llvm.call @obelisk_rt_v1_native_state_alloc
// LLVM: llvm.getelementptr
// LLVM: llvm.load {{.*}} -> i16
// LLVM: llvm.store {{.*}} : i16, !llvm.ptr
// LLVM-NOT: llvm.call
// LLVM: llvm.return
// REF-LABEL: simulation.func @static_update
// REF: simulation.ref.alloc
// REF: simulation.ref.store
// REF: simulation.return
// LLVM-LABEL: llvm.func @static_update
// LLVM: llvm.alloca
// LLVM-NOT: llvm.call
// LLVM: llvm.store {{.*}} : i8, !llvm.ptr
// LLVM-NOT: llvm.call
// LLVM: llvm.return

!record = !simulation.packed_struct<[
  #simulation.field<name = "payload", type = i206, ordinal = 0, packedOffset = 7>,
  #simulation.field<name = "reg", type = i7, ordinal = 1, packedOffset = 0>
]>
!array = !simulation.unpacked_array<0 : 1 x !record>
module attributes {llvm.data_layout = "e-m:e-p:64:64-i64:64-n8:16:32:64-S128", llvm.target_triple = "aarch64-unknown-linux-gnu"} {
  simulation.design @sroa_shape {
    simulation.scope.decl 0
    simulation.code_unit.decl 9000991 in 0 function hierarchy "sroa_shape.dynamic"
    simulation.code_unit.decl 9000992 in 0 function hierarchy "sroa_shape.static"
    simulation.code_unit.decl 9000993 in 0 function hierarchy "sroa_shape.forwarded"
    simulation.code_unit.decl 9000994 in 0 function hierarchy "sroa_shape.copy"
    simulation.code_unit.decl 9000995 in 0 function hierarchy "sroa_shape.copy_out"
    simulation.func @dynamic_update(
      %ctx: !simulation.context {simulation.capture_kind = 0 : i32},
      %input: !array {simulation.capture_kind = 2 : i32},
      %index: i64 {simulation.capture_kind = 2 : i32},
      %value: i7 {simulation.capture_kind = 2 : i32}) -> !array
      attributes {entry_kind = 8 : i32, code_unit_id = 9000991 : i64} {
      %local = simulation.ref.alloc %input : !array -> !simulation.ref<!array>
      %element = simulation.ref.array_element %local[%index] : (!simulation.ref<!array>, i64) -> !simulation.ref<!record>
      %field = simulation.ref.subelement %element[[1]] : !simulation.ref<!record> -> !simulation.ref<i7>
      simulation.ref.store %value to %field : i7, !simulation.ref<i7>
      %result = simulation.ref.load %local : !simulation.ref<!array> -> !array
      simulation.return %result : !array
    }
    simulation.func @static_update(
      %ctx: !simulation.context {simulation.capture_kind = 0 : i32},
      %input: !record {simulation.capture_kind = 2 : i32},
      %value: i7 {simulation.capture_kind = 2 : i32}) -> !record
      attributes {entry_kind = 8 : i32, code_unit_id = 9000992 : i64} {
      %local = simulation.ref.alloc %input : !record -> !simulation.ref<!record>
      %field = simulation.ref.subelement %local[[1]] : !simulation.ref<!record> -> !simulation.ref<i7>
      simulation.ref.store %value to %field : i7, !simulation.ref<i7>
      %result = simulation.ref.load %local : !simulation.ref<!record> -> !record
      simulation.return %result : !record
    }
    simulation.func @forwarded_update(
      %ctx: !simulation.context {simulation.capture_kind = 0 : i32},
      %input: !array {simulation.capture_kind = 2 : i32},
      %value: i7 {simulation.capture_kind = 2 : i32}) -> !array
      attributes {entry_kind = 8 : i32, code_unit_id = 9000993 : i64} {
      %local = simulation.ref.alloc %input : !array -> !simulation.ref<!array>
      %zero = arith.constant 0 : i64
      %one = arith.constant 1 : i64
      %two = arith.constant 2 : i64
      cf.br ^loop(%local, %zero : !simulation.ref<!array>, i64)
    ^loop(%alias: !simulation.ref<!array>, %index: i64):
      %element = simulation.ref.array_element %alias[%index] : (!simulation.ref<!array>, i64) -> !simulation.ref<!record>
      %field = simulation.ref.subelement %element[[1]] : !simulation.ref<!record> -> !simulation.ref<i7>
      simulation.ref.store %value to %field : i7, !simulation.ref<i7>
      %next = arith.addi %index, %one : i64
      %again = arith.cmpi ult, %next, %two : i64
      cf.cond_br %again, ^loop(%alias, %next : !simulation.ref<!array>, i64), ^done(%alias : !simulation.ref<!array>)
    ^done(%result_ref: !simulation.ref<!array>):
      %result = simulation.ref.load %result_ref : !simulation.ref<!array> -> !array
      simulation.return %result : !array
    }

    simulation.func @copy_slot(
      %ctx: !simulation.context {simulation.capture_kind = 0 : i32},
      %input: !array {simulation.capture_kind = 2 : i32}) -> !array
        attributes {entry_kind = 8 : i32, code_unit_id = 9000994 : i64} {
      %source = simulation.ref.alloc %input : !array -> !simulation.ref<!array>
      %snapshot = simulation.ref.load %source : !simulation.ref<!array> -> !array
      %destination = simulation.ref.alloc %snapshot : !array -> !simulation.ref<!array>
      %value = simulation.ref.load %destination : !simulation.ref<!array> -> !array
      simulation.return %value : !array
    }

    simulation.func @forwarded_copy_out(
      %ctx: !simulation.context {simulation.capture_kind = 0 : i32},
      %input: !array {simulation.capture_kind = 2 : i32},
      %output: !simulation.ref<!array> {simulation.capture_kind = 1 : i32})
        attributes {entry_kind = 8 : i32, code_unit_id = 9000995 : i64} {
      %local = simulation.ref.alloc %input : !array -> !simulation.ref<!array>
      cf.br ^copy(%local : !simulation.ref<!array>)
    ^copy(%alias: !simulation.ref<!array>):
      %snapshot = simulation.ref.load %alias : !simulation.ref<!array> -> !array
      simulation.ref.store %snapshot to %output : !array, !simulation.ref<!array>
      simulation.return
    }
  }
}

// LLVM-LABEL: llvm.func @forwarded_update
// LLVM: llvm.alloca
// LLVM-NOT: llvm.call
// LLVM: llvm.store {{.*}} : i16, !llvm.ptr
// LLVM-NOT: llvm.call
// LLVM: llvm.return

// LLVM-LABEL: llvm.func @copy_slot
// LLVM: llvm.intr.memcpy
// LLVM-NOT: llvm.call
// LLVM: llvm.return
// OPT-LABEL: define i426 @copy_slot(
// OPT-NOT: alloca
// OPT: ret i426

// Private-reference classification must survive CFG forwarding before bulk
// copy formation; a local bit offset can never become a runtime handle.
// LLVM-LABEL: llvm.func @forwarded_copy_out
// LLVM-NOT: llvm.call @obelisk_rt_v1_native_state_alloc
// LLVM-NOT: llvm.call @obelisk_rt_v1_native_ref_copy
// LLVM: llvm.return
