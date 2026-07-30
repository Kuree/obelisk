// RUN: obelisk-opt %s --convert-obelisk-sim-processes-to-llvm-coroutines | FileCheck %s

!record = !obelisk_sim.unpacked_struct<[
  #obelisk_sim.field<name = "byte", type = i8, ordinal = 0, packedOffset = 0>,
  #obelisk_sim.field<name = "flag", type = i1, ordinal = 1, packedOffset = 0>
]>
!words = !obelisk_sim.unpacked_array<3 : 1 x i8>
!choice = !obelisk_sim.unpacked_union<fields = [
  #obelisk_sim.field<name = "byte", type = i8, ordinal = 0, packedOffset = 0>,
  #obelisk_sim.field<name = "word", type = i16, ordinal = 1, packedOffset = 0>
], isTagged = false>
!tagged = !obelisk_sim.packed_union<fields = [
  #obelisk_sim.field<name = "byte", type = i8, ordinal = 0, packedOffset = 0>,
  #obelisk_sim.field<name = "word", type = i16, ordinal = 1, packedOffset = 0>
], isTagged = true, tagBits = 1>

module attributes {
  llvm.data_layout = "e-p:64:64-i64:64-i32:32-i16:16-i8:8",
  llvm.target_triple = "x86_64-unknown-linux-gnu"
} {
  obelisk_sim.design @aggregates {
    obelisk_sim.scope.decl 0
    obelisk_sim.code_unit.decl 1 in 0 function hierarchy "aggregates.exercise"

    obelisk_sim.func @exercise(
        %ctx: !obelisk_sim.context
            {obelisk_sim.capture_kind = 0 : i32},
        %index: i32 {obelisk_sim.capture_kind = 2 : i32})
        attributes {code_unit_id = 1 : i64, entry_kind = 8 : i32} {
      %zero = arith.constant 0 : i8
      %one = arith.constant 1 : i8
      %true = arith.constant true
      %default = obelisk_sim.aggregate.default : !record
      %record = obelisk_sim.aggregate.construct %zero, %true :
          (i8, i1) -> !record
      %byte = obelisk_sim.aggregate.extract %record[0] : (!record) -> i8
      %updated = obelisk_sim.aggregate.insert %one into %default[0] :
          (!record, i8) -> !record
      %array = obelisk_sim.aggregate.construct %zero, %one, %byte :
          (i8, i8, i8) -> !words
      %dynamic = obelisk_sim.array.extract_dynamic %array[%index] :
          (!words, i32) -> i8
      %choice = obelisk_sim.union.construct %dynamic as 0 :
          (i8) -> !choice
      %extracted = obelisk_sim.union.extract %choice[0] : (!choice) -> i8
      %word = arith.constant 42 : i16
      %tagged = obelisk_sim.union.construct %word as 1 : (i16) -> !tagged
      %active = obelisk_sim.union.is_active %tagged[1] : !tagged
      obelisk_sim.return
    }
  }
}

// CHECK-LABEL: llvm.func @exercise(
// CHECK-DAG: llvm.or
// CHECK-DAG: llvm.and
// CHECK-DAG: llvm.select
// CHECK-DAG: llvm.icmp
// CHECK-NOT: obelisk_sim.aggregate
// CHECK-NOT: obelisk_sim.array.extract_dynamic
// CHECK-NOT: obelisk_sim.union
