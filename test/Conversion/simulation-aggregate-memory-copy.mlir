// RUN: obelisk-opt %s --convert-obelisk-sim-processes-to-llvm-coroutines | FileCheck %s
// RUN: obelisk-opt %s --convert-obelisk-sim-processes-to-llvm-coroutines | obelisk-translate --mlir-to-llvmir | opt -passes='default<O3>,verify' -disable-output

!record = !simulation.packed_struct<[
  #simulation.field<name = "payload", type = !simulation.logic<206>, ordinal = 0, packedOffset = 7>,
  #simulation.field<name = "tag", type = !simulation.logic<7>, ordinal = 1, packedOffset = 0>
]>
!array = !simulation.unpacked_array<-2 : -3 x !record>

module attributes {llvm.data_layout = "e-m:e-p:64:64-i64:64-n8:16:32:64-S128", llvm.target_triple = "aarch64-unknown-linux-gnu"} {
  simulation.design @memory_copy {
    simulation.scope.decl 0
    simulation.code_unit.decl 1 in 0 function hierarchy "memory_copy.snapshot"

    // The initial aggregate is a buffer, and each subsequent field access
    // addresses its bytes. A snapshot used after a write remains a distinct
    // copy. Both stores consume that snapshot without loading an i426 value.
    // CHECK-LABEL: llvm.func @snapshot
    // CHECK-NOT: i426
    // CHECK-COUNT-2: llvm.call @obelisk_rt_v1_native_state_load_plane
    // CHECK-NOT: i426
    // CHECK-COUNT-2: llvm.intr.memcpy
    // CHECK-NOT: i426
    // CHECK: llvm.getelementptr
    // CHECK: llvm.store {{.*}} : i16, !llvm.ptr
    // CHECK-NOT: i426
    // CHECK-COUNT-2: llvm.intr.memcpy
    // CHECK-NOT: i426
    // CHECK: llvm.store {{.*}} : i16, !llvm.ptr
    // CHECK-NOT: i426
    // CHECK: llvm.call @obelisk_rt_v1_scheduler_signal_transition
    // CHECK-NOT: i426
    // CHECK: llvm.call @obelisk_rt_v1_scheduler_signal_transition
    // CHECK-NOT: i426
    // CHECK: llvm.return
    simulation.func @snapshot(
        %ctx: !simulation.context {simulation.capture_kind = 0 : i32},
        %source: !simulation.ref<!array> {simulation.capture_kind = 1 : i32},
        %destination: !simulation.ref<!array> {simulation.capture_kind = 1 : i32},
        %other: !simulation.ref<!array> {simulation.capture_kind = 1 : i32},
        %index: i64 {simulation.capture_kind = 2 : i32},
        %value: !simulation.logic<7> {simulation.capture_kind = 2 : i32})
        attributes {entry_kind = 8 : i32, code_unit_id = 1 : i64} {
      %input = simulation.ref.load %source : !simulation.ref<!array> -> !array
      %local = simulation.ref.alloc %input : !array -> !simulation.ref<!array>
      %element = simulation.ref.array_element %local[%index] : (!simulation.ref<!array>, i64) -> !simulation.ref<!record>
      %field = simulation.ref.subelement %element[[1]] : !simulation.ref<!record> -> !simulation.ref<!simulation.logic<7>>
      simulation.ref.store %value to %field : !simulation.logic<7>, !simulation.ref<!simulation.logic<7>>
      %snapshot = simulation.ref.load %local : !simulation.ref<!array> -> !array
      %zero = simulation.logic.constant 0 : i7, 0 : i7 : !simulation.logic<7>
      simulation.ref.store %zero to %field : !simulation.logic<7>, !simulation.ref<!simulation.logic<7>>
      simulation.ref.store %snapshot to %destination : !array, !simulation.ref<!array>
      simulation.ref.store %snapshot to %other : !array, !simulation.ref<!array>
      simulation.return
    }
  }
}
