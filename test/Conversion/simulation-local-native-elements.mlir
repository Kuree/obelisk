// RUN: talos-opt %s --convert-talos-sim-processes-to-llvm-coroutines | talos-translate --mlir-to-llvmir | opt -passes='default<O3>,verify' -S | FileCheck %s

// Each unpacked i3 owns a byte. Updating an in-bounds element must not load
// its old byte to preserve padding or access the adjacent element as i16.
// Packed subfields and invalid indices are covered by local-stack-storage
// and native-private-activation-arrays respectively.
!array = !simulation.unpacked_array<0 : 31 x i3>
module attributes {llvm.data_layout = "e-m:e-p:64:64-i64:64-n8:16:32:64-S128", llvm.target_triple = "aarch64-unknown-linux-gnu"} {
  simulation.design @native_elements {
    simulation.scope.decl 0
    simulation.code_unit.decl 1 in 0 function hierarchy "native_elements.update"
    simulation.func @update(
        %ctx: !simulation.context {simulation.capture_kind = 0 : i32},
        %input: !array {simulation.capture_kind = 2 : i32},
        %writeIndex: i64 {simulation.capture_kind = 2 : i32},
        %readIndex: i64 {simulation.capture_kind = 2 : i32},
        %value: i3 {simulation.capture_kind = 2 : i32}) -> i3
        attributes {entry_kind = 8 : i32, code_unit_id = 1 : i64} {
      %local = simulation.ref.alloc %input : !array -> !simulation.ref<!array>
      %mask = arith.constant 31 : i64
      %w = arith.andi %writeIndex, %mask : i64
      %r = arith.andi %readIndex, %mask : i64
      %destination = simulation.ref.array_element %local[%w] : (!simulation.ref<!array>, i64) -> !simulation.ref<i3>
      simulation.ref.store %value to %destination : i3, !simulation.ref<i3>
      %source = simulation.ref.array_element %local[%r] : (!simulation.ref<!array>, i64) -> !simulation.ref<i3>
      %result = simulation.ref.load %source : !simulation.ref<i3> -> i3
      simulation.return %result : i3
    }
  }
}
// CHECK-LABEL: define i3 @update(
// CHECK-NOT: load i16
// CHECK-NOT: and i8 {{.*}}, -8
// CHECK: store i8
// CHECK-NOT: load i16
// CHECK-NOT: and i8 {{.*}}, -8
// CHECK: ret i3
