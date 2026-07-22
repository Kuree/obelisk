// RUN: obelisk-opt %s --pass-pipeline='builtin.module(obelisk_sim.design(obelisk-sim-build-compute-graph,obelisk-sim-verify-compute-graph))' | FileCheck %s

!packed_record = !obelisk_sim.packed_struct<[
  #obelisk_sim.field<name = "payload", type = !obelisk_sim.packed_array<3 : 0 x i1>, ordinal = 0, packedOffset = 1>,
  #obelisk_sim.field<name = "valid", type = i1, ordinal = 1, packedOffset = 0>
]>
!unpacked_record = !obelisk_sim.unpacked_struct<[
  #obelisk_sim.field<name = "payload", type = i8, ordinal = 0, packedOffset = 0>,
  #obelisk_sim.field<name = "valid", type = i1, ordinal = 1, packedOffset = 0>
]>
!descending = !obelisk_sim.unpacked_array<3 : 1 x i8>
!ascending = !obelisk_sim.unpacked_array<-1 : 1 x i8>
!choice = !obelisk_sim.unpacked_union<fields = [
  #obelisk_sim.field<name = "byte", type = i8, ordinal = 0, packedOffset = 0>,
  #obelisk_sim.field<name = "integer", type = i32, ordinal = 1, packedOffset = 0>
], isTagged = false>

module {
  obelisk_sim.design @aggregate_provenance {
    obelisk_sim.scope.decl 0
    obelisk_sim.storage.decl 0 in 0 : !packed_record design
    obelisk_sim.storage.decl 1 in 0 : !unpacked_record design
    obelisk_sim.storage.decl 2 in 0 : !descending design
    obelisk_sim.storage.decl 3 in 0 : !ascending design
    obelisk_sim.storage.decl 4 in 0 : !choice design

    // Packed fields use their declared packed offsets.
    // CHECK-LABEL: obelisk_sim.func @packed_payload
    // CHECK-SAME: effect_summary = [#obelisk_sim.effect<effect = read, resource = storage, target = descriptor, descriptor = 0, formal = 0, low = 1, width = 4, dynamic = false
    obelisk_sim.func @packed_payload(
        %ctx: !obelisk_sim.context {obelisk_sim.capture_kind = 0 : i32},
        %record: !obelisk_sim.ref<!packed_record> {obelisk_sim.capture_kind = 3 : i32, obelisk_sim.descriptor_id = 0 : i64}) -> !obelisk_sim.packed_array<3 : 0 x i1>
        attributes {entry_kind = 8 : i32} {
      %field = obelisk_sim.ref.subelement %record[[0]] : !obelisk_sim.ref<!packed_record> -> !obelisk_sim.ref<!obelisk_sim.packed_array<3 : 0 x i1>>
      %value = obelisk_sim.ref.load %field : !obelisk_sim.ref<!obelisk_sim.packed_array<3 : 0 x i1>> -> !obelisk_sim.packed_array<3 : 0 x i1>
      obelisk_sim.return %value : !obelisk_sim.packed_array<3 : 0 x i1>
    }

    // Unpacked struct children occupy disjoint structural spans.
    // CHECK-LABEL: obelisk_sim.func @unpacked_valid
    // CHECK-SAME: effect_summary = [#obelisk_sim.effect<effect = read, resource = storage, target = descriptor, descriptor = 1, formal = 0, low = 8, width = 1, dynamic = false
    obelisk_sim.func @unpacked_valid(
        %ctx: !obelisk_sim.context {obelisk_sim.capture_kind = 0 : i32},
        %record: !obelisk_sim.ref<!unpacked_record> {obelisk_sim.capture_kind = 3 : i32, obelisk_sim.descriptor_id = 1 : i64}) -> i1
        attributes {entry_kind = 8 : i32} {
      %field = obelisk_sim.ref.subelement %record[[1]] : !obelisk_sim.ref<!unpacked_record> -> !obelisk_sim.ref<i1>
      %value = obelisk_sim.ref.load %field : !obelisk_sim.ref<i1> -> i1
      obelisk_sim.return %value : i1
    }

    // Declaration ordinals map to disjoint array intervals.
    // CHECK-LABEL: obelisk_sim.func @descending_first
    // CHECK-SAME: descriptor = 2, formal = 0, low = 0, width = 8, dynamic = false
    obelisk_sim.func @descending_first(
        %ctx: !obelisk_sim.context {obelisk_sim.capture_kind = 0 : i32},
        %array: !obelisk_sim.ref<!descending> {obelisk_sim.capture_kind = 3 : i32, obelisk_sim.descriptor_id = 2 : i64}) -> i8
        attributes {entry_kind = 8 : i32} {
      %field = obelisk_sim.ref.subelement %array[[0]] : !obelisk_sim.ref<!descending> -> !obelisk_sim.ref<i8>
      %value = obelisk_sim.ref.load %field : !obelisk_sim.ref<i8> -> i8
      obelisk_sim.return %value : i8
    }

    // CHECK-LABEL: obelisk_sim.func @descending_second
    // CHECK-SAME: descriptor = 2, formal = 0, low = 8, width = 8, dynamic = false
    obelisk_sim.func @descending_second(
        %ctx: !obelisk_sim.context {obelisk_sim.capture_kind = 0 : i32},
        %array: !obelisk_sim.ref<!descending> {obelisk_sim.capture_kind = 3 : i32, obelisk_sim.descriptor_id = 2 : i64}) -> i8
        attributes {entry_kind = 8 : i32} {
      %field = obelisk_sim.ref.subelement %array[[1]] : !obelisk_sim.ref<!descending> -> !obelisk_sim.ref<i8>
      %value = obelisk_sim.ref.load %field : !obelisk_sim.ref<i8> -> i8
      obelisk_sim.return %value : i8
    }

    // CHECK-LABEL: obelisk_sim.func @ascending_last
    // CHECK-SAME: descriptor = 3, formal = 0, low = 16, width = 8, dynamic = false
    obelisk_sim.func @ascending_last(
        %ctx: !obelisk_sim.context {obelisk_sim.capture_kind = 0 : i32},
        %array: !obelisk_sim.ref<!ascending> {obelisk_sim.capture_kind = 3 : i32, obelisk_sim.descriptor_id = 3 : i64}) -> i8
        attributes {entry_kind = 8 : i32} {
      %field = obelisk_sim.ref.subelement %array[[2]] : !obelisk_sim.ref<!ascending> -> !obelisk_sim.ref<i8>
      %value = obelisk_sim.ref.load %field : !obelisk_sim.ref<i8> -> i8
      obelisk_sim.return %value : i8
    }

    // A dynamic view conservatively covers the containing array.
    // CHECK-LABEL: obelisk_sim.func @dynamic_array
    // CHECK-SAME: descriptor = 2, formal = 0, low = 0, width = 24, dynamic = true
    obelisk_sim.func @dynamic_array(
        %ctx: !obelisk_sim.context {obelisk_sim.capture_kind = 0 : i32},
        %array: !obelisk_sim.ref<!descending> {obelisk_sim.capture_kind = 3 : i32, obelisk_sim.descriptor_id = 2 : i64},
        %index: i64 {obelisk_sim.capture_kind = 2 : i32}) -> i8
        attributes {entry_kind = 8 : i32} {
      %field = obelisk_sim.ref.array_element %array[%index] : (!obelisk_sim.ref<!descending>, i64) -> !obelisk_sim.ref<i8>
      %value = obelisk_sim.ref.load %field : !obelisk_sim.ref<i8> -> i8
      obelisk_sim.return %value : i8
    }

    // Union fields intentionally overlap at structural offset zero.
    // CHECK-LABEL: obelisk_sim.func @union_byte
    // CHECK-SAME: descriptor = 4, formal = 0, low = 0, width = 8, dynamic = false
    obelisk_sim.func @union_byte(
        %ctx: !obelisk_sim.context {obelisk_sim.capture_kind = 0 : i32},
        %choice: !obelisk_sim.ref<!choice> {obelisk_sim.capture_kind = 3 : i32, obelisk_sim.descriptor_id = 4 : i64}) -> i8
        attributes {entry_kind = 8 : i32} {
      %field = obelisk_sim.ref.subelement %choice[[0]] : !obelisk_sim.ref<!choice> -> !obelisk_sim.ref<i8>
      %value = obelisk_sim.ref.load %field : !obelisk_sim.ref<i8> -> i8
      obelisk_sim.return %value : i8
    }

    // CHECK-LABEL: obelisk_sim.func @union_integer
    // CHECK-SAME: descriptor = 4, formal = 0, low = 0, width = 32, dynamic = false
    obelisk_sim.func @union_integer(
        %ctx: !obelisk_sim.context {obelisk_sim.capture_kind = 0 : i32},
        %choice: !obelisk_sim.ref<!choice> {obelisk_sim.capture_kind = 3 : i32, obelisk_sim.descriptor_id = 4 : i64}) -> i32
        attributes {entry_kind = 8 : i32} {
      %field = obelisk_sim.ref.subelement %choice[[1]] : !obelisk_sim.ref<!choice> -> !obelisk_sim.ref<i32>
      %value = obelisk_sim.ref.load %field : !obelisk_sim.ref<i32> -> i32
      obelisk_sim.return %value : i32
    }
  }
}
