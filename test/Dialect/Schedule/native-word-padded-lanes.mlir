// RUN: obelisk-opt %s --schedule-optimize-native-words | FileCheck %s
// Eight three-bit values occupy eight LLVM byte slots. Word access and
// reversal use byte strides; stores clear padding without converting Z to X.
// CHECK: schedule.word.region 64
// CHECK: schedule.word.view {{.*}} -> !simulation.ref<!simulation.logic<64>>
// CHECK: schedule.word.extract {{.*}} -> !simulation.logic<64>
// CHECK: schedule.word.reverse {{.*}}lane_width = 8
// CHECK: simulation.logic.constant 0 : i5, 0 : i5
// CHECK: simulation.logic.insert {{.*}} at 3
// CHECK: simulation.logic.insert {{.*}} at 59
// CHECK: simulation.ref.store {{.*}} : !simulation.logic<64>
// CHECK-LABEL: simulation.func @mixed
// CHECK: schedule.word.region 64
// CHECK: simulation.logic.dyn_extract {{.*}} -> !simulation.logic<24>
// CHECK: schedule.word.reverse {{.*}}lane_width = 3
// CHECK: schedule.word.resize_lanes {{.*}}input_lane_width = 3{{.*}}result_lane_width = 8
// CHECK: simulation.ref.store {{.*}}!simulation.logic<64>
// CHECK-LABEL: simulation.func @nested
// CHECK: schedule.word.region 64
// CHECK: simulation.logic.binary xor {{.*}}!simulation.logic<64>

!bit = !simulation.logic<3>
!up = !simulation.unpacked_array<-4 : 3 x !simulation.logic<3>, #simulation.storage_layout<size = 8, alignment = 1, stride = 1, offsets = []>>
!wrapped = !simulation.packed_struct<[#simulation.field<name = "bit", type = !simulation.logic<3>, ordinal = 0, packedOffset = 0>]>
!down = !simulation.unpacked_array<3 : -4 x !simulation.packed_struct<[#simulation.field<name = "bit", type = !simulation.logic<3>, ordinal = 0, packedOffset = 0>]>, #simulation.storage_layout<size = 8, alignment = 1, stride = 1, offsets = []>>
!upref = !simulation.ref<!up>
!downref = !simulation.ref<!down>
!bitref = !simulation.ref<!bit>
!packed = !simulation.packed_array<-4 : 3 x !simulation.logic<3>>
!packedref = !simulation.ref<!packed>
!rows = !simulation.unpacked_array<0 : 1 x !up, #simulation.storage_layout<size = 16, alignment = 1, stride = 8, offsets = []>>
module attributes {llvm.data_layout = "e-m:e-p:64:64-i64:64-n8:16:32:64-S128", llvm.target_triple = "x86_64-unknown-linux-gnu"} {
  simulation.design @unpacked attributes {
    schedule.nba.transient_observable = array<i64>,
    schedule.nba.private_influence = array<i64: 1, 3, 5>,
    compute_graph = #schedule.graph<version = 1, vpi = off, workers = 1,
      nodes = [], edges = [], regions = [
        #schedule.region<kind = active, groups = []>,
        #schedule.region<kind = nba, groups = []>,
        #schedule.region<kind = observed, groups = []>,
        #schedule.region<kind = reactive, groups = []>,
        #schedule.region<kind = postponed, groups = []>]>} {
    simulation.scope.decl 0
    simulation.code_unit.decl 1 in 0 root_initializer hierarchy "root"
    simulation.code_unit.decl 2 in 0 function hierarchy "copy"
    simulation.storage.decl 0 in 0 : !up design
    simulation.storage.decl 1 in 0 : !down design
    simulation.storage.decl 2 in 0 : !packed design
    simulation.storage.decl 3 in 0 : !down design
    simulation.code_unit.decl 3 in 0 function hierarchy "mixed"
    simulation.storage.decl 4 in 0 : !rows design
    simulation.storage.decl 5 in 0 : !up design
    simulation.code_unit.decl 4 in 0 function hierarchy "nested"
    simulation.func @copy(
      %ctx: !simulation.context {simulation.capture_kind = 0 : i32},
      %src: !upref {simulation.capture_kind = 3 : i32, simulation.descriptor_id = 0 : i64},
      %dst: !downref {simulation.capture_kind = 3 : i32, simulation.descriptor_id = 1 : i64}) attributes {entry_kind = 8 : i32, code_unit_id = 2 : i64, schedule.eval.raw_captures} {
      %start = arith.constant -4 : i32
      cf.br ^head(%start : i32)
    ^head(%i: i32):
      %limit = arith.constant 4 : i32
      %more = arith.cmpi slt, %i, %limit : i32
      cf.cond_br %more, ^body, ^exit
    ^body:
      %index = arith.extsi %i : i32 to i65
      %snapshot = simulation.ref.load %src : !upref -> !up
      %wrapped = simulation.ref.array_element %dst[%index] : (!downref, i65) -> !simulation.ref<!wrapped>
      %out = simulation.ref.subelement %wrapped[[0]] : !simulation.ref<!wrapped> -> !bitref
      %value = simulation.array.extract_dynamic %snapshot[%index] : (!up, i65) -> !bit
      simulation.ref.store %value to %out : !bit, !bitref
      %one = arith.constant 1 : i32
      %next = arith.addi %i, %one : i32
      cf.br ^head(%next : i32)
    ^exit:
      simulation.return
    }
    // Pack and unpack logical lanes at their physical storage boundaries.
    simulation.func @mixed(
      %ctx: !simulation.context {simulation.capture_kind = 0 : i32},
      %src: !packedref {simulation.capture_kind = 3 : i32, simulation.descriptor_id = 2 : i64},
      %dst: !downref {simulation.capture_kind = 3 : i32, simulation.descriptor_id = 3 : i64}) attributes {entry_kind = 8 : i32, code_unit_id = 3 : i64, schedule.eval.raw_captures} {
      %start = arith.constant -4 : i32
      cf.br ^head(%start : i32)
    ^head(%i: i32):
      %limit = arith.constant 4 : i32
      %more = arith.cmpi slt, %i, %limit : i32
      cf.cond_br %more, ^body, ^exit
    ^body:
      %index = arith.extsi %i : i32 to i65
      %snapshot = simulation.ref.load %src : !packedref -> !packed
      %wrapped = simulation.ref.array_element %dst[%index] : (!downref, i65) -> !simulation.ref<!wrapped>
      %out = simulation.ref.subelement %wrapped[[0]] : !simulation.ref<!wrapped> -> !bitref
      %value = simulation.array.extract_dynamic %snapshot[%index] : (!packed, i65) -> !bit
      simulation.ref.store %value to %out : !bit, !bitref
      %one = arith.constant 1 : i32
      %next = arith.addi %i, %one : i32
      cf.br ^head(%next : i32)
    ^exit:
      simulation.return
    }
    // Outer iterations touch disjoint byte slots despite logical width three.
    // Inner iterations intentionally read and write the same selected slot.
    simulation.func @nested(
      %ctx: !simulation.context {simulation.capture_kind = 0 : i32}) attributes {entry_kind = 8 : i32, code_unit_id = 4 : i64, schedule.eval.raw_captures} {
      %start = arith.constant -4 : i32
      %zero = arith.constant 0 : i32
      cf.br ^head(%start : i32)
    ^head(%i: i32):
      %limit = arith.constant 4 : i32
      %more = arith.cmpi slt, %i, %limit : i32
      cf.cond_br %more, ^body, ^exit
    ^body:
      %index = arith.extsi %i : i32 to i65
      %initial_storage = simulation.context.storage %ctx[5] : !upref
      %initial = simulation.ref.array_element %initial_storage[%index] : (!upref, i65) -> !bitref
      %clear = simulation.logic.constant 0 : i3, 0 : i3 : !bit
      simulation.ref.store %clear to %initial : !bit, !bitref
      cf.br ^inner(%zero : i32)
    ^inner(%j: i32):
      %two = arith.constant 2 : i32
      %inner_more = arith.cmpi slt, %j, %two : i32
      cf.cond_br %inner_more, ^update, ^advance
    ^update:
      %way = arith.extsi %j : i32 to i65
      %src = simulation.context.storage %ctx[4] : !simulation.ref<!rows>
      %row = simulation.ref.array_element %src[%way] : (!simulation.ref<!rows>, i65) -> !upref
      %bit = simulation.ref.array_element %row[%index] : (!upref, i65) -> !bitref
      %value = simulation.ref.load %bit : !bitref -> !bit
      %fixed = simulation.context.storage %ctx[5] : !upref
      %duplicate_index = arith.extsi %i : i32 to i65
      %target = simulation.ref.array_element %fixed[%duplicate_index] : (!upref, i65) -> !bitref
      %old = simulation.ref.load %target : !bitref -> !bit
      %next_value = simulation.logic.binary xor %old, %value : !bit
      simulation.ref.store %next_value to %target : !bit, !bitref
      %one = arith.constant 1 : i32
      %next_j = arith.addi %j, %one : i32
      cf.br ^inner(%next_j : i32)
    ^advance:
      %outer_one = arith.constant 1 : i32
      %next_i = arith.addi %i, %outer_one : i32
      cf.br ^head(%next_i : i32)
    ^exit:
      simulation.return
    }
  }
}
