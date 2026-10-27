// RUN: talos-opt %s --talos-sim-build-compute-graph='vpi=off' --talos-sim-specialize-static-state-nba --convert-talos-sim-processes-to-llvm-coroutines | FileCheck %s --implicit-check-not=i426 --implicit-check-not='llvm.call @talos_rt_v1_native_state'
// RUN: talos-opt %s --talos-sim-build-compute-graph='vpi=off' --talos-sim-specialize-static-state-nba --convert-talos-sim-processes-to-llvm-coroutines | talos-translate --mlir-to-llvmir | opt -passes='default<O3>,verify' -disable-output

// Direct state/private and state/state copies stay in memory. A 426-bit store
// copies 53 bytes and masks the last two bits, preserving neighboring state.
// CHECK-LABEL: llvm.func @snapshot
// CHECK: llvm.intr.memcpy
// CHECK: llvm.getelementptr
// CHECK: llvm.store {{.*}} : i16, !llvm.ptr
// CHECK: llvm.intr.memcpy
// CHECK: llvm.mlir.constant(3 : i8)
// CHECK: llvm.store {{.*}} : i8, !llvm.ptr
// CHECK: llvm.call @talos_rt_v1_scheduler_signal_transition
// CHECK: llvm.return
// CHECK-LABEL: llvm.func @copy_state
// CHECK-COUNT-5: llvm.intr.memcpy
// CHECK: llvm.mlir.constant(3 : i8)
// CHECK: llvm.store {{.*}} : i8, !llvm.ptr
// CHECK: llvm.call @talos_rt_v1_scheduler_signal_transition
// CHECK: llvm.return

!record = !simulation.packed_struct<[
  #simulation.field<name = "payload", type = !simulation.logic<206>, ordinal = 0, packedOffset = 7>,
  #simulation.field<name = "tag", type = !simulation.logic<7>, ordinal = 1, packedOffset = 0>
]>
!array = !simulation.unpacked_array<-2 : -3 x !record>

module attributes {llvm.data_layout = "e-m:e-p:64:64-i64:64-n8:16:32:64-S128", llvm.target_triple = "aarch64-unknown-linux-gnu"} {
  simulation.design @memory_copy {
    simulation.scope.decl 0
    simulation.storage.decl 0 in 0 : !array design
    simulation.storage.decl 1 in 0 : !array design
    simulation.storage.decl 2 in 0 : !array design
    simulation.code_unit.decl 1 in 0 function hierarchy "memory_copy.snapshot"
    simulation.code_unit.decl 2 in 0 function hierarchy "memory_copy.copy_state"

    simulation.func @snapshot(
        %ctx: !simulation.context {simulation.capture_kind = 0 : i32},
        %index: i64 {simulation.capture_kind = 2 : i32},
        %value: !simulation.logic<7> {simulation.capture_kind = 2 : i32})
        attributes {entry_kind = 8 : i32, code_unit_id = 1 : i64} {
      %source = simulation.context.storage %ctx[0] : !simulation.ref<!array>
      %destination = simulation.context.storage %ctx[1] : !simulation.ref<!array>
      %other = simulation.context.storage %ctx[2] : !simulation.ref<!array>
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

    simulation.func @copy_state(
        %ctx: !simulation.context {simulation.capture_kind = 0 : i32})
        attributes {entry_kind = 8 : i32, code_unit_id = 2 : i64} {
      %source = simulation.context.storage %ctx[0] : !simulation.ref<!array>
      %destination = simulation.context.storage %ctx[1] : !simulation.ref<!array>
      %value = simulation.ref.load %source : !simulation.ref<!array> -> !array
      simulation.ref.store %value to %destination : !array, !simulation.ref<!array>
      simulation.return
    }
  }
}
