// RUN: talos-opt %s --convert-talos-sim-processes-to-llvm-coroutines | FileCheck %s
// RUN: talos-opt %s --convert-talos-sim-processes-to-llvm-coroutines | talos-translate --mlir-to-llvmir | opt -passes='default<O3>,verify' -disable-output

!record = !simulation.packed_struct<[
  #simulation.field<name = "payload", type = !simulation.logic<206>, ordinal = 0, packedOffset = 7>,
  #simulation.field<name = "tag", type = !simulation.logic<7>, ordinal = 1, packedOffset = 0>
]>
!array = !simulation.unpacked_array<0 : 1 x !record>
module attributes {llvm.data_layout = "e-m:e-p:64:64-i64:64-n8:16:32:64-S128", llvm.target_triple = "aarch64-unknown-linux-gnu"} {
  simulation.design @versions {
    simulation.scope.decl 0
    simulation.code_unit.decl 1 in 0 function hierarchy "versions.borrow"
    simulation.code_unit.decl 2 in 0 function hierarchy "versions.conflict"

    // A whole native aggregate keeps its layout and only the destination
    // copies remain. The source write after consumption needs no snapshot.
    // CHECK-LABEL: llvm.func @borrow
    // CHECK-COUNT-4: llvm.alloca
    // CHECK-NOT: llvm.alloca
    // CHECK: llvm.cond_br
    // CHECK-NOT: llvm.intr.memcpy
    // CHECK: llvm.br
    // CHECK-COUNT-2: llvm.intr.memcpy
    // CHECK-NOT: llvm.intr.memcpy
    // CHECK: llvm.return
    simulation.func @borrow(
        %ctx: !simulation.context {simulation.capture_kind = 0 : i32},
        %input: !array {simulation.capture_kind = 2 : i32},
        %condition: i1 {simulation.capture_kind = 2 : i32}) -> !array
        attributes {entry_kind = 8 : i32, code_unit_id = 1 : i64} {
      %source = simulation.ref.alloc %input : !array -> !simulation.ref<!array>
      %old = simulation.ref.load %source : !simulation.ref<!array> -> !array
      cf.cond_br %condition, ^left, ^join
    ^left:
      cf.br ^join
    ^join:
      %destination = simulation.ref.alloc %old : !array -> !simulation.ref<!array>
      simulation.ref.store %input to %source : !array, !simulation.ref<!array>
      %result = simulation.ref.load %destination : !simulation.ref<!array> -> !array
      simulation.return %result : !array
    }

    // A write on just one path is enough to require a snapshot. Both planes
    // are copied before the branch and remain valid after the source changes.
    // CHECK-LABEL: llvm.func @conflict
    // CHECK-COUNT-6: llvm.alloca
    // CHECK-COUNT-2: llvm.intr.memcpy
    // CHECK: llvm.cond_br
    // CHECK-COUNT-2: llvm.intr.memcpy
    // CHECK-NOT: llvm.intr.memcpy
    // CHECK: llvm.return
    simulation.func @conflict(
        %ctx: !simulation.context {simulation.capture_kind = 0 : i32},
        %input: !array {simulation.capture_kind = 2 : i32},
        %replacement: !array {simulation.capture_kind = 2 : i32},
        %condition: i1 {simulation.capture_kind = 2 : i32}) -> !array
        attributes {entry_kind = 8 : i32, code_unit_id = 2 : i64} {
      %source = simulation.ref.alloc %input : !array -> !simulation.ref<!array>
      %old = simulation.ref.load %source : !simulation.ref<!array> -> !array
      cf.cond_br %condition, ^write, ^join
    ^write:
      simulation.ref.store %replacement to %source : !array, !simulation.ref<!array>
      cf.br ^join
    ^join:
      %destination = simulation.ref.alloc %old : !array -> !simulation.ref<!array>
      %result = simulation.ref.load %destination : !simulation.ref<!array> -> !array
      simulation.return %result : !array
    }
  }
}
