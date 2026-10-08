// RUN: obelisk-opt %s --convert-obelisk-sim-processes-to-llvm-coroutines | FileCheck %s

// Unpacked objects have an ABI layout per plane. Packed leaves preserve their
// bit layout. Whole-object loads gather both planes from the same field offsets.
!record = !simulation.unpacked_struct<[
  #simulation.field<name = "prefix", type = i3, ordinal = 0, packedOffset = 0>,
  #simulation.field<name = "payload", type = !simulation.logic<67>, ordinal = 1, packedOffset = 0>,
  #simulation.field<name = "suffix", type = i9, ordinal = 2, packedOffset = 0>
]>
!array = !simulation.unpacked_array<-1 : 0 x !record>
!flat = !simulation.unpacked_array<0 : 1 x i213>

module attributes {llvm.data_layout = "e-m:e-p:64:64-i64:64-n8:16:32:64-S128", llvm.target_triple = "aarch64-unknown-linux-gnu"} {
  simulation.design @local_layout {
    simulation.scope.decl 0
    simulation.code_unit.decl 1 in 0 function hierarchy "local_layout.nested"
    simulation.code_unit.decl 2 in 0 function hierarchy "local_layout.flat"
    // CHECK-LABEL: llvm.func @nested
    // CHECK-COUNT-2: llvm.alloca {{.*}} x !llvm.struct<(array<2 x struct<(i3, i67, i9)>>, i8)>
    // CHECK: llvm.mlir.constant(256 : i64)
    // CHECK: llvm.mlir.constant(64 : i64)
    // CHECK-COUNT-2: llvm.intr.memcpy
    // CHECK-NOT: llvm.load {{.*}} -> i160
    // CHECK: llvm.load {{.*}} -> i72
    // CHECK: llvm.load {{.*}} -> i72
    // CHECK: llvm.load {{.*}} -> i72
    // CHECK: llvm.load {{.*}} -> i72
    // CHECK-NOT: llvm.call
    // CHECK: llvm.return
    simulation.func @nested(
      %ctx: !simulation.context {simulation.capture_kind = 0 : i32},
      %input: !array {simulation.capture_kind = 2 : i32},
      %index: i64 {simulation.capture_kind = 2 : i32},
      %value: !simulation.logic<67> {simulation.capture_kind = 2 : i32}) -> !array
      attributes {entry_kind = 8 : i32, code_unit_id = 1 : i64} {
      %a = simulation.ref.alloc %input : !array -> !simulation.ref<!array>
      %element = simulation.ref.array_element %a[%index] : (!simulation.ref<!array>, i64) -> !simulation.ref<!record>
      %payload = simulation.ref.subelement %element[[1]] : !simulation.ref<!record> -> !simulation.ref<!simulation.logic<67>>
      simulation.ref.store %value to %payload : !simulation.logic<67>, !simulation.ref<!simulation.logic<67>>
      %snapshot = simulation.ref.load %a : !simulation.ref<!array> -> !array
      %copy = simulation.ref.alloc %snapshot : !array -> !simulation.ref<!array>
      %result = simulation.ref.load %copy : !simulation.ref<!array> -> !array
      simulation.return %result : !array
    }

    // A flat view crossing unpacked elements requires the canonical bitstream
    // layout. The seven-bit word crosses the boundary at bit 213.
    // CHECK-LABEL: llvm.func @flat_view
    // CHECK: llvm.mlir.constant(55 : i64)
    // CHECK: llvm.alloca {{.*}} x i8
    // CHECK: llvm.mlir.constant(210 : i64)
    // CHECK: llvm.load {{.*}} -> i16
    // CHECK: llvm.store {{.*}} : i16, !llvm.ptr
    // CHECK-NOT: llvm.call
    // CHECK: llvm.return
    simulation.func @flat_view(
      %ctx: !simulation.context {simulation.capture_kind = 0 : i32},
      %input: !flat {simulation.capture_kind = 2 : i32},
      %value: i7 {simulation.capture_kind = 2 : i32}) -> !flat
      attributes {entry_kind = 8 : i32, code_unit_id = 2 : i64} {
      %a = simulation.ref.alloc %input : !flat -> !simulation.ref<!flat>
      %low = arith.constant 210 : i64
      %field = schedule.word.view %a, %low : (!simulation.ref<!flat>, i64) -> !simulation.ref<i7>
      simulation.ref.store %value to %field : i7, !simulation.ref<i7>
      %result = simulation.ref.load %a : !simulation.ref<!flat> -> !flat
      simulation.return %result : !flat
    }
  }
}
