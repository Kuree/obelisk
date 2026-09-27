// RUN: obelisk-opt %s --pass-pipeline='builtin.module(simulation.design(obelisk-sim-build-compute-graph,obelisk-sim-verify-compute-graph))' | FileCheck %s

!packed_record = !simulation.packed_struct<[
  #simulation.field<name = "payload", type = !simulation.packed_array<3 : 0 x i1>, ordinal = 0, packedOffset = 1>,
  #simulation.field<name = "valid", type = i1, ordinal = 1, packedOffset = 0>
]>
!unpacked_record = !simulation.unpacked_struct<[
  #simulation.field<name = "payload", type = i8, ordinal = 0, packedOffset = 0>,
  #simulation.field<name = "valid", type = i1, ordinal = 1, packedOffset = 0>
]>
!descending = !simulation.unpacked_array<3 : 1 x i8>
!ascending = !simulation.unpacked_array<-1 : 1 x i8>
!choice = !simulation.unpacked_union<fields = [
  #simulation.field<name = "byte", type = i8, ordinal = 0, packedOffset = 0>,
  #simulation.field<name = "integer", type = i32, ordinal = 1, packedOffset = 0>
], isTagged = false>

module {
  simulation.design @aggregate_provenance {
    simulation.code_unit.decl 9000001 in 0 function hierarchy "test.aggregate_provenance.packed_payload.9000001"
    simulation.code_unit.decl 9000002 in 0 function hierarchy "test.aggregate_provenance.unpacked_valid.9000002"
    simulation.code_unit.decl 9000003 in 0 function hierarchy "test.aggregate_provenance.descending_first.9000003"
    simulation.code_unit.decl 9000004 in 0 function hierarchy "test.aggregate_provenance.descending_second.9000004"
    simulation.code_unit.decl 9000005 in 0 function hierarchy "test.aggregate_provenance.ascending_last.9000005"
    simulation.code_unit.decl 9000006 in 0 function hierarchy "test.aggregate_provenance.dynamic_array.9000006"
    simulation.code_unit.decl 9000007 in 0 function hierarchy "test.aggregate_provenance.union_byte.9000007"
    simulation.code_unit.decl 9000008 in 0 function hierarchy "test.aggregate_provenance.union_integer.9000008"
    simulation.scope.decl 0
    simulation.storage.decl 0 in 0 : !packed_record design
    simulation.storage.decl 1 in 0 : !unpacked_record design
    simulation.storage.decl 2 in 0 : !descending design
    simulation.storage.decl 3 in 0 : !ascending design
    simulation.storage.decl 4 in 0 : !choice design

    // Packed fields use their declared packed offsets.
    // CHECK-LABEL: simulation.func @packed_payload
    // CHECK-SAME: effect_summary = [#schedule.effect<effect = read, resource = storage, target = descriptor, descriptor = 0, formal = 0, low = 1, width = 4, dynamic = false
    simulation.func @packed_payload(
        %ctx: !simulation.context {simulation.capture_kind = 0 : i32},
        %record: !simulation.ref<!packed_record> {simulation.capture_kind = 3 : i32, simulation.descriptor_id = 0 : i64}) -> !simulation.packed_array<3 : 0 x i1>
        attributes {entry_kind = 8 : i32, code_unit_id = 9000001 : i64} {
      %field = simulation.ref.subelement %record[[0]] : !simulation.ref<!packed_record> -> !simulation.ref<!simulation.packed_array<3 : 0 x i1>>
      %value = simulation.ref.load %field : !simulation.ref<!simulation.packed_array<3 : 0 x i1>> -> !simulation.packed_array<3 : 0 x i1>
      simulation.return %value : !simulation.packed_array<3 : 0 x i1>
    }

    // Unpacked struct children occupy disjoint structural spans.
    // CHECK-LABEL: simulation.func @unpacked_valid
    // CHECK-SAME: effect_summary = [#schedule.effect<effect = read, resource = storage, target = descriptor, descriptor = 1, formal = 0, low = 8, width = 1, dynamic = false
    simulation.func @unpacked_valid(
        %ctx: !simulation.context {simulation.capture_kind = 0 : i32},
        %record: !simulation.ref<!unpacked_record> {simulation.capture_kind = 3 : i32, simulation.descriptor_id = 1 : i64}) -> i1
        attributes {entry_kind = 8 : i32, code_unit_id = 9000002 : i64} {
      %field = simulation.ref.subelement %record[[1]] : !simulation.ref<!unpacked_record> -> !simulation.ref<i1>
      %value = simulation.ref.load %field : !simulation.ref<i1> -> i1
      simulation.return %value : i1
    }

    // Declaration ordinals map to disjoint array intervals.
    // CHECK-LABEL: simulation.func @descending_first
    // CHECK-SAME: descriptor = 2, formal = 0, low = 0, width = 8, dynamic = false
    simulation.func @descending_first(
        %ctx: !simulation.context {simulation.capture_kind = 0 : i32},
        %array: !simulation.ref<!descending> {simulation.capture_kind = 3 : i32, simulation.descriptor_id = 2 : i64}) -> i8
        attributes {entry_kind = 8 : i32, code_unit_id = 9000003 : i64} {
      %field = simulation.ref.subelement %array[[0]] : !simulation.ref<!descending> -> !simulation.ref<i8>
      %value = simulation.ref.load %field : !simulation.ref<i8> -> i8
      simulation.return %value : i8
    }

    // CHECK-LABEL: simulation.func @descending_second
    // CHECK-SAME: descriptor = 2, formal = 0, low = 8, width = 8, dynamic = false
    simulation.func @descending_second(
        %ctx: !simulation.context {simulation.capture_kind = 0 : i32},
        %array: !simulation.ref<!descending> {simulation.capture_kind = 3 : i32, simulation.descriptor_id = 2 : i64}) -> i8
        attributes {entry_kind = 8 : i32, code_unit_id = 9000004 : i64} {
      %field = simulation.ref.subelement %array[[1]] : !simulation.ref<!descending> -> !simulation.ref<i8>
      %value = simulation.ref.load %field : !simulation.ref<i8> -> i8
      simulation.return %value : i8
    }

    // CHECK-LABEL: simulation.func @ascending_last
    // CHECK-SAME: descriptor = 3, formal = 0, low = 16, width = 8, dynamic = false
    simulation.func @ascending_last(
        %ctx: !simulation.context {simulation.capture_kind = 0 : i32},
        %array: !simulation.ref<!ascending> {simulation.capture_kind = 3 : i32, simulation.descriptor_id = 3 : i64}) -> i8
        attributes {entry_kind = 8 : i32, code_unit_id = 9000005 : i64} {
      %field = simulation.ref.subelement %array[[2]] : !simulation.ref<!ascending> -> !simulation.ref<i8>
      %value = simulation.ref.load %field : !simulation.ref<i8> -> i8
      simulation.return %value : i8
    }

    // A dynamic view conservatively covers the containing array.
    // CHECK-LABEL: simulation.func @dynamic_array
    // CHECK-SAME: descriptor = 2, formal = 0, low = 0, width = 24, dynamic = true
    simulation.func @dynamic_array(
        %ctx: !simulation.context {simulation.capture_kind = 0 : i32},
        %array: !simulation.ref<!descending> {simulation.capture_kind = 3 : i32, simulation.descriptor_id = 2 : i64},
        %index: i64 {simulation.capture_kind = 2 : i32}) -> i8
        attributes {entry_kind = 8 : i32, code_unit_id = 9000006 : i64} {
      %field = simulation.ref.array_element %array[%index] : (!simulation.ref<!descending>, i64) -> !simulation.ref<i8>
      %value = simulation.ref.load %field : !simulation.ref<i8> -> i8
      simulation.return %value : i8
    }

    // Union fields intentionally overlap at structural offset zero.
    // CHECK-LABEL: simulation.func @union_byte
    // CHECK-SAME: descriptor = 4, formal = 0, low = 0, width = 8, dynamic = false
    simulation.func @union_byte(
        %ctx: !simulation.context {simulation.capture_kind = 0 : i32},
        %choice: !simulation.ref<!choice> {simulation.capture_kind = 3 : i32, simulation.descriptor_id = 4 : i64}) -> i8
        attributes {entry_kind = 8 : i32, code_unit_id = 9000007 : i64} {
      %field = simulation.ref.subelement %choice[[0]] : !simulation.ref<!choice> -> !simulation.ref<i8>
      %value = simulation.ref.load %field : !simulation.ref<i8> -> i8
      simulation.return %value : i8
    }

    // CHECK-LABEL: simulation.func @union_integer
    // CHECK-SAME: descriptor = 4, formal = 0, low = 0, width = 32, dynamic = false
    simulation.func @union_integer(
        %ctx: !simulation.context {simulation.capture_kind = 0 : i32},
        %choice: !simulation.ref<!choice> {simulation.capture_kind = 3 : i32, simulation.descriptor_id = 4 : i64}) -> i32
        attributes {entry_kind = 8 : i32, code_unit_id = 9000008 : i64} {
      %field = simulation.ref.subelement %choice[[1]] : !simulation.ref<!choice> -> !simulation.ref<i32>
      %value = simulation.ref.load %field : !simulation.ref<i32> -> i32
      simulation.return %value : i32
    }
  }
}
