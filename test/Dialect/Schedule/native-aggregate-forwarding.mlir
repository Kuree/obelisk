// RUN: talos-opt %s --pass-pipeline='builtin.module(simulation.func(schedule-optimize-native-aggregates))' | FileCheck %s
// RUN: talos-opt %s --pass-pipeline='builtin.module(simulation.func(schedule-optimize-native-aggregates{eval-only=true}))' | FileCheck %s --check-prefix=FRAME
!packed = !simulation.packed_array<0 : 1 x i8>
module {
  // Both values of an unsigned one-bit index are in this ascending array.
  // The late eval-only mode preserves original bodies with frozen frames.
  // FRAME-LABEL: simulation.func @bounded
  // FRAME: simulation.array.insert_dynamic
  // FRAME: simulation.array.extract_dynamic
  // FRAME: simulation.return
  // CHECK-LABEL: simulation.func @bounded
  // CHECK-SAME: %[[VALUE:[^: ,]+]]: i8
  // CHECK-NOT: simulation.array
  // CHECK: simulation.return %[[VALUE]] : i8
  simulation.func @bounded(
      %ctx: !simulation.context {simulation.capture_kind = 0 : i32},
      %array: !packed {simulation.capture_kind = 2 : i32},
      %index: i1 {simulation.capture_kind = 2 : i32},
      %value: i8 {simulation.capture_kind = 2 : i32}) -> i8 attributes {entry_kind = 8 : i32} {
    %extended = arith.extui %index : i1 to i65
    %updated = simulation.array.insert_dynamic %value into %array[%extended] : (!packed, i8, i65) -> !packed
    %read = simulation.array.extract_dynamic %updated[%extended] : (!packed, i65) -> i8
    simulation.return %read : i8
  }
  // A known but unconstrained selector cannot bypass invalid-index defaults.
  // CHECK-LABEL: simulation.func @unbounded
  // CHECK: simulation.array.insert_dynamic
  // CHECK: simulation.array.extract_dynamic
  // CHECK: simulation.return
  simulation.func @unbounded(
      %ctx: !simulation.context {simulation.capture_kind = 0 : i32},
      %array: !packed {simulation.capture_kind = 2 : i32},
      %index: i65 {simulation.capture_kind = 2 : i32},
      %value: i8 {simulation.capture_kind = 2 : i32}) -> i8 attributes {entry_kind = 8 : i32} {
    %updated = simulation.array.insert_dynamic %value into %array[%index] : (!packed, i8, i65) -> !packed
    %read = simulation.array.extract_dynamic %updated[%index] : (!packed, i65) -> i8
    simulation.return %read : i8
  }
  // Inserting a read back into its original array is an identity even when
  // the index contains X/Z: both invalid writes and valid self-writes do nothing.
  // CHECK-LABEL: simulation.func @identity
  // CHECK-SAME: %[[ARRAY:[^: ,]+]]: !simulation.packed_array
  // CHECK-NOT: simulation.array
  // CHECK: simulation.return %[[ARRAY]]
  simulation.func @identity(
      %ctx: !simulation.context {simulation.capture_kind = 0 : i32},
      %array: !packed {simulation.capture_kind = 2 : i32},
      %index: !simulation.logic<65> {simulation.capture_kind = 2 : i32}) -> !packed attributes {entry_kind = 8 : i32} {
    %read = simulation.array.extract_dynamic %array[%index] : (!packed, !simulation.logic<65>) -> i8
    %updated = simulation.array.insert_dynamic %read into %array[%index] : (!packed, i8, !simulation.logic<65>) -> !packed
    simulation.return %updated : !packed
  }
}
