// RUN: obelisk-opt %s --convert-obelisk-sim-processes-to-llvm-coroutines | FileCheck %s

!record = !simulation.unpacked_struct<[
  #simulation.field<name = "byte", type = i8, ordinal = 0, packedOffset = 0>,
  #simulation.field<name = "flag", type = i1, ordinal = 1, packedOffset = 0>
]>
!words = !simulation.unpacked_array<3 : 1 x i8>
!choice = !simulation.unpacked_union<fields = [
  #simulation.field<name = "byte", type = i8, ordinal = 0, packedOffset = 0>,
  #simulation.field<name = "word", type = i16, ordinal = 1, packedOffset = 0>
], isTagged = false>
!tagged = !simulation.packed_union<fields = [
  #simulation.field<name = "byte", type = i8, ordinal = 0, packedOffset = 0>,
  #simulation.field<name = "word", type = i16, ordinal = 1, packedOffset = 0>
], isTagged = true, tagBits = 1>

module attributes {
  llvm.data_layout = "e-p:64:64-i64:64-i32:32-i16:16-i8:8",
  llvm.target_triple = "x86_64-unknown-linux-gnu"
} {
  simulation.design @aggregates {
    simulation.scope.decl 0
    simulation.code_unit.decl 1 in 0 function hierarchy "aggregates.exercise"

    simulation.func @exercise(
        %ctx: !simulation.context
            {simulation.capture_kind = 0 : i32},
        %index: i32 {simulation.capture_kind = 2 : i32})
        attributes {code_unit_id = 1 : i64, entry_kind = 8 : i32} {
      %zero = arith.constant 0 : i8
      %one = arith.constant 1 : i8
      %true = arith.constant true
      %default = simulation.aggregate.default : !record
      %record = simulation.aggregate.construct %zero, %true :
          (i8, i1) -> !record
      %byte = simulation.aggregate.extract %record[0] : (!record) -> i8
      %updated = simulation.aggregate.insert %one into %default[0] :
          (!record, i8) -> !record
      %array = simulation.aggregate.construct %zero, %one, %byte :
          (i8, i8, i8) -> !words
      %dynamic = simulation.array.extract_dynamic %array[%index] :
          (!words, i32) -> i8
      %choice = simulation.union.construct %dynamic as 0 :
          (i8) -> !choice
      %extracted = simulation.union.extract %choice[0] : (!choice) -> i8
      %word = arith.constant 42 : i16
      %tagged = simulation.union.construct %word as 1 : (i16) -> !tagged
      %active = simulation.union.is_active %tagged[1] : !tagged
      simulation.return
    }
  }
}

// CHECK-LABEL: llvm.func @exercise(
// CHECK-DAG: llvm.or
// CHECK-DAG: llvm.and
// CHECK-DAG: llvm.select
// CHECK-DAG: llvm.icmp
// CHECK-NOT: simulation.aggregate
// CHECK-NOT: simulation.array.extract_dynamic
// CHECK-NOT: simulation.union
