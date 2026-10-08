// RUN: obelisk-opt %s --obelisk-sim-build-compute-graph='vpi=off' --obelisk-sim-specialize-static-state-nba --convert-obelisk-sim-processes-to-llvm-coroutines | FileCheck %s
// RUN: obelisk-opt %s --obelisk-sim-build-compute-graph='vpi=off' --obelisk-sim-specialize-static-state-nba --convert-obelisk-sim-processes-to-llvm-coroutines | mlir-translate --mlir-to-llvmir | opt -passes='default<O3>,verify' -S | FileCheck %s --check-prefix=OPT

// Clang's packed bit-field access units: these fields share bytes all the
// way across a 95-bit record, so each uses the same i96 unit. The untouched
// low bit must survive, including for a dynamic local array element.
!record = !simulation.packed_struct<[
  #simulation.field<name = "e", type = i32, ordinal = 0, packedOffset = 63>,
  #simulation.field<name = "d", type = i4, ordinal = 1, packedOffset = 59>,
  #simulation.field<name = "c", type = i7, ordinal = 2, packedOffset = 52>,
  #simulation.field<name = "b", type = i19, ordinal = 3, packedOffset = 33>,
  #simulation.field<name = "a", type = i32, ordinal = 4, packedOffset = 1>,
  #simulation.field<name = "pad", type = i1, ordinal = 5, packedOffset = 0>
]>
!array = !simulation.unpacked_array<0 : 1 x !record>
!aligned_record = !simulation.packed_struct<[
  #simulation.field<name = "e", type = i33, ordinal = 0, packedOffset = 63>,
  #simulation.field<name = "d", type = i4, ordinal = 1, packedOffset = 59>,
  #simulation.field<name = "c", type = i7, ordinal = 2, packedOffset = 52>,
  #simulation.field<name = "b", type = i19, ordinal = 3, packedOffset = 33>,
  #simulation.field<name = "a", type = i32, ordinal = 4, packedOffset = 1>,
  #simulation.field<name = "pad", type = i1, ordinal = 5, packedOffset = 0>
]>
!aligned_array = !simulation.unpacked_array<0 : 1 x !aligned_record>
!words = !simulation.unpacked_array<0 : 31 x i32>
module attributes {llvm.data_layout = "e-m:e-p:64:64-i64:64-n8:16:32:64-S128", llvm.target_triple = "aarch64-unknown-linux-gnu"} {
  simulation.design @packed_units {
    simulation.scope.decl 0
    simulation.storage.decl 0 in 0 : !record design
    simulation.storage.decl 1 in 0 : !aligned_array design
    simulation.storage.decl 2 in 0 : !words design
    simulation.code_unit.decl 1 in 0 function hierarchy "packed_units.state"
    simulation.code_unit.decl 2 in 0 function hierarchy "packed_units.local"
    simulation.code_unit.decl 3 in 0 function hierarchy "packed_units.indexed"
    simulation.code_unit.decl 4 in 0 function hierarchy "packed_units.words"
    simulation.func @state_fields(
        %ctx: !simulation.context {simulation.capture_kind = 0 : i32},
        %a: i32 {simulation.capture_kind = 2 : i32},
        %b: i19 {simulation.capture_kind = 2 : i32}) -> i32
        attributes {entry_kind = 8 : i32, code_unit_id = 1 : i64} {
      %root = simulation.context.storage %ctx[0] : !simulation.ref<!record>
      %ra = simulation.ref.subelement %root[[4]] : !simulation.ref<!record> -> !simulation.ref<i32>
      %rb = simulation.ref.subelement %root[[3]] : !simulation.ref<!record> -> !simulation.ref<i19>
      simulation.ref.store %a to %ra : i32, !simulation.ref<i32>
      simulation.ref.store %b to %rb : i19, !simulation.ref<i19>
      %out = simulation.ref.load %ra : !simulation.ref<i32> -> i32
      simulation.return %out : i32
    }
    simulation.func @word_elements(
        %ctx: !simulation.context {simulation.capture_kind = 0 : i32},
        %index: i64 {simulation.capture_kind = 2 : i32},
        %value: i32 {simulation.capture_kind = 2 : i32}) -> i32
        attributes {entry_kind = 8 : i32, code_unit_id = 4 : i64} {
      %root = simulation.context.storage %ctx[2] : !simulation.ref<!words>
      %element = simulation.ref.array_element %root[%index] : (!simulation.ref<!words>, i64) -> !simulation.ref<i32>
      simulation.ref.store %value to %element : i32, !simulation.ref<i32>
      %out = simulation.ref.load %element : !simulation.ref<i32> -> i32
      simulation.return %out : i32
    }
    simulation.func @local_fields(
        %ctx: !simulation.context {simulation.capture_kind = 0 : i32},
        %input: !array {simulation.capture_kind = 2 : i32},
        %index: i64 {simulation.capture_kind = 2 : i32},
        %a: i32 {simulation.capture_kind = 2 : i32},
        %b: i19 {simulation.capture_kind = 2 : i32}) -> !array
        attributes {entry_kind = 8 : i32, code_unit_id = 2 : i64} {
      %local = simulation.ref.alloc %input : !array -> !simulation.ref<!array>
      %element = simulation.ref.array_element %local[%index] : (!simulation.ref<!array>, i64) -> !simulation.ref<!record>
      %ra = simulation.ref.subelement %element[[4]] : !simulation.ref<!record> -> !simulation.ref<i32>
      %rb = simulation.ref.subelement %element[[3]] : !simulation.ref<!record> -> !simulation.ref<i19>
      simulation.ref.store %a to %ra : i32, !simulation.ref<i32>
      simulation.ref.store %b to %rb : i19, !simulation.ref<i19>
      %out = simulation.ref.load %local : !simulation.ref<!array> -> !array
      simulation.return %out : !array
    }
    simulation.func @indexed_state_fields(
        %ctx: !simulation.context {simulation.capture_kind = 0 : i32},
        %index: i64 {simulation.capture_kind = 2 : i32},
        %a: i32 {simulation.capture_kind = 2 : i32}) -> i32
        attributes {entry_kind = 8 : i32, code_unit_id = 3 : i64} {
      %root = simulation.context.storage %ctx[1] : !simulation.ref<!aligned_array>
      %mask = arith.constant 1 : i64
      %i = arith.andi %index, %mask : i64
      %element = simulation.ref.array_element %root[%i] : (!simulation.ref<!aligned_array>, i64) -> !simulation.ref<!aligned_record>
      %ra = simulation.ref.subelement %element[[4]] : !simulation.ref<!aligned_record> -> !simulation.ref<i32>
      simulation.ref.store %a to %ra : i32, !simulation.ref<i32>
      %out = simulation.ref.load %ra : !simulation.ref<i32> -> i32
      simulation.return %out : i32
    }
  }
}
// CHECK-LABEL: llvm.func @state_fields(
// CHECK: llvm.load {{.*}} -> i96
// CHECK: llvm.store {{.*}} : i96, !llvm.ptr
// CHECK: llvm.load {{.*}} -> i96
// CHECK: llvm.store {{.*}} : i96, !llvm.ptr
// CHECK: llvm.load {{.*}} -> i96
// CHECK: llvm.return
// Whole words use i32 accesses even when the index needs a validity guard.
// CHECK-LABEL: llvm.func @word_elements(
// CHECK-NOT: !llvm.ptr -> i64
// CHECK: llvm.load {{.*}} -> i32
// CHECK-NOT: !llvm.ptr -> i64
// CHECK: llvm.return
// The two dynamic updates use one load/store after the ordinary LLVM pipeline.
// OPT-LABEL: define i190 @local_fields(
// OPT: [[ADDR:%.*]] = getelementptr i8, ptr {{.*}}, i64 %
// OPT: [[OLD:%.*]] = load i96, ptr [[ADDR]]
// OPT-NOT: load
// OPT: store i96 {{.*}}, ptr [[ADDR]]
// OPT-NOT: store
// OPT: ret i190
// CHECK-LABEL: llvm.func @local_fields(
// CHECK: llvm.load {{.*}} -> i96
// CHECK: llvm.store {{.*}} : i96, !llvm.ptr
// CHECK: llvm.load {{.*}} -> i96
// CHECK: llvm.store {{.*}} : i96, !llvm.ptr
// CHECK: llvm.return
// CHECK-LABEL: llvm.func @indexed_state_fields(
// CHECK: llvm.load {{.*}} -> i96
// CHECK-NOT: -> i104
// CHECK: llvm.return
