// RUN: obelisk-opt %s --pass-pipeline='builtin.module(simulation.design(simulation.func(schedule-optimize-native-aggregates)))' -o %t.forwarded
// RUN: FileCheck %s --check-prefix=IR < %t.forwarded
// RUN: obelisk-opt %t.forwarded --convert-obelisk-sim-processes-to-llvm-coroutines="native-aggregates=false" -o /dev/null
// RUN: obelisk-opt %s --convert-obelisk-sim-processes-to-llvm-coroutines="native-words=false native-aggregates=false" -o /dev/null

// LRM 7.4.5 and 11.5: eliminate repeated reconstruction of a 288-bit array,
// retaining every four-state field and the defaults for invalid reads.
// IR-LABEL: simulation.func @update
// IR: simulation.array.extract_dynamic
// IR-NOT: simulation.array.extract_dynamic
// IR: simulation.array.insert_dynamic
// IR-NOT: simulation.array.insert_dynamic
// IR-NOT: simulation.array.extract_dynamic
// IR: simulation.return
// IR-LABEL: simulation.func @read_unproved
// IR: simulation.array.insert_dynamic
// IR: simulation.array.extract_dynamic
// IR: simulation.return
// IR-LABEL: simulation.func @overwrite
// IR: simulation.array.insert_dynamic
// IR-NOT: simulation.array.insert_dynamic
// IR: simulation.return
!word = !simulation.logic<32>
!record = !simulation.packed_struct<[#simulation.field<name = "a", type = !simulation.logic<32>, ordinal = 0, packedOffset = 64>, #simulation.field<name = "b", type = !simulation.logic<32>, ordinal = 1, packedOffset = 32>, #simulation.field<name = "c", type = !simulation.logic<32>, ordinal = 2, packedOffset = 0>]>
!array = !simulation.unpacked_array<-2 : 0 x !record>
module attributes {llvm.data_layout = "e-m:e-p:64:64-i64:64-n8:16:32:64-S128", llvm.target_triple = "x86_64-unknown-linux-gnu"} {
  simulation.design @forwarding {
    simulation.scope.decl 0
    simulation.code_unit.decl 1 in 0 root_initializer hierarchy "root"
    simulation.code_unit.decl 2 in 0 function hierarchy "update"
    simulation.code_unit.decl 3 in 0 function hierarchy "read_unproved"
    simulation.code_unit.decl 4 in 0 function hierarchy "overwrite"
    simulation.func @root(%ctx: !simulation.context {simulation.capture_kind = 0 : i32}) attributes {entry_kind = 0 : i32, code_unit_id = 1 : i64} {
      %a = simulation.logic.constant 305419896 : i32, 65280 : i32 : !word
      %b = simulation.logic.constant 2309737967 : i32, 4026531840 : i32 : !word
      %c = simulation.logic.constant 4277009102 : i32, 11162880 : i32 : !word
      %z = simulation.logic.constant -1 : i96, -1 : i96 : !simulation.logic<96>
      %initial_record = simulation.packed.unflatten %z : (!simulation.logic<96>) -> !record
      %initial = simulation.aggregate.splat %initial_record : (!record) -> !array
      %result = simulation.call @update(%ctx, %initial, %a, %b, %c) : (!simulation.context, !array, !word, !word, !word) -> !array
      %r0 = simulation.aggregate.extract %result[0] : (!array) -> !record
      %r1 = simulation.aggregate.extract %result[1] : (!array) -> !record
      %r2 = simulation.aggregate.extract %result[2] : (!array) -> !record
      %flat0 = simulation.packed.flatten %r0 : (!record) -> !simulation.logic<96>
      %flat1 = simulation.packed.flatten %r1 : (!record) -> !simulation.logic<96>
      %flat2 = simulation.packed.flatten %r2 : (!record) -> !simulation.logic<96>
      %expected = simulation.logic.concat %a, %b, %c : (!word, !word, !word) -> !simulation.logic<96>
      %ok0 = simulation.logic.compare case_eq %flat0, %expected : (!simulation.logic<96>, !simulation.logic<96>) -> i1
      %ok1 = simulation.logic.compare case_eq %flat1, %expected : (!simulation.logic<96>, !simulation.logic<96>) -> i1
      %ok2 = simulation.logic.compare case_eq %flat2, %expected : (!simulation.logic<96>, !simulation.logic<96>) -> i1
      %outside = simulation.logic.constant 1 : i65, 0 : i65 : !simulation.logic<65>
      %unknown = simulation.logic.constant 0 : i65, 1 : i65 : !simulation.logic<65>
      %invalid = simulation.call @read_unproved(%ctx, %initial, %outside, %r0) : (!simulation.context, !array, !simulation.logic<65>, !record) -> !record
      %ambiguous = simulation.call @read_unproved(%ctx, %initial, %unknown, %r0) : (!simulation.context, !array, !simulation.logic<65>, !record) -> !record
      %invalid_flat = simulation.packed.flatten %invalid : (!record) -> !simulation.logic<96>
      %ambiguous_flat = simulation.packed.flatten %ambiguous : (!record) -> !simulation.logic<96>
      %allx = simulation.logic.constant 0 : i96, -1 : i96 : !simulation.logic<96>
      %invalid_ok = simulation.logic.compare case_eq %invalid_flat, %allx : (!simulation.logic<96>, !simulation.logic<96>) -> i1
      %ambiguous_ok = simulation.logic.compare case_eq %ambiguous_flat, %allx : (!simulation.logic<96>, !simulation.logic<96>) -> i1
      %kept = simulation.call @overwrite(%ctx, %initial, %unknown, %r0, %r1) : (!simulation.context, !array, !simulation.logic<65>, !record, !record) -> !array
      %kept_record = simulation.aggregate.extract %kept[0] : (!array) -> !record
      %kept_flat = simulation.packed.flatten %kept_record : (!record) -> !simulation.logic<96>
      %kept_ok = simulation.logic.compare case_eq %kept_flat, %z : (!simulation.logic<96>, !simulation.logic<96>) -> i1
      %fmt = simulation.bytes.constant "aggregate forwarding %0d %0d %0d %0d %0d %0d"
      %channel = arith.constant 1 : i32
      simulation.display %ctx to %channel(%fmt, %ok0, %ok1, %ok2, %invalid_ok, %ambiguous_ok, %kept_ok) newline = true radix = <decimal> flags = [0, 0, 0, 0, 0, 0, 0] : !simulation.bytes, i1, i1, i1, i1, i1, i1
      simulation.return
    }
    simulation.func @update(
      %ctx: !simulation.context {simulation.capture_kind = 0 : i32},
      %input: !array {simulation.capture_kind = 2 : i32},
      %a: !word {simulation.capture_kind = 2 : i32},
      %b: !word {simulation.capture_kind = 2 : i32},
      %c: !word {simulation.capture_kind = 2 : i32}) -> !array attributes {entry_kind = 8 : i32, code_unit_id = 2 : i64} {
      %start = arith.constant -2 : i32
      cf.br ^head(%start, %input : i32, !array)
    ^head(%i: i32, %state: !array):
      %limit = arith.constant 1 : i32
      %more = arith.cmpi slt, %i, %limit : i32
      cf.cond_br %more, ^body, ^exit
    ^body:
      %index = arith.extsi %i : i32 to i65
      %old = simulation.array.extract_dynamic %state[%index] : (!array, i65) -> !record
      %field_a = simulation.aggregate.insert %a into %old[0] : (!record, !word) -> !record
      %first = simulation.array.insert_dynamic %field_a into %state[%index] : (!array, !record, i65) -> !array
      %read_first = simulation.array.extract_dynamic %first[%index] : (!array, i65) -> !record
      %field_b = simulation.aggregate.insert %b into %read_first[1] : (!record, !word) -> !record
      %second = simulation.array.insert_dynamic %field_b into %first[%index] : (!array, !record, i65) -> !array
      %read_second = simulation.array.extract_dynamic %second[%index] : (!array, i65) -> !record
      %field_c = simulation.aggregate.insert %c into %read_second[2] : (!record, !word) -> !record
      %next_state = simulation.array.insert_dynamic %field_c into %second[%index] : (!array, !record, i65) -> !array
      %one = arith.constant 1 : i32
      %next = arith.addi %i, %one : i32
      cf.br ^head(%next, %next_state : i32, !array)
    ^exit:
      simulation.return %state : !array
    }
    simulation.func @read_unproved(
      %ctx: !simulation.context {simulation.capture_kind = 0 : i32},
      %input: !array {simulation.capture_kind = 2 : i32},
      %index: !simulation.logic<65> {simulation.capture_kind = 2 : i32},
      %replacement: !record {simulation.capture_kind = 2 : i32}) -> !record attributes {entry_kind = 8 : i32, code_unit_id = 3 : i64} {
      %updated = simulation.array.insert_dynamic %replacement into %input[%index] : (!array, !record, !simulation.logic<65>) -> !array
      %result = simulation.array.extract_dynamic %updated[%index] : (!array, !simulation.logic<65>) -> !record
      simulation.return %result : !record
    }
    simulation.func @overwrite(
      %ctx: !simulation.context {simulation.capture_kind = 0 : i32},
      %input: !array {simulation.capture_kind = 2 : i32},
      %index: !simulation.logic<65> {simulation.capture_kind = 2 : i32},
      %first: !record {simulation.capture_kind = 2 : i32},
      %last: !record {simulation.capture_kind = 2 : i32}) -> !array attributes {entry_kind = 8 : i32, code_unit_id = 4 : i64} {
      %temporary = simulation.array.insert_dynamic %first into %input[%index] : (!array, !record, !simulation.logic<65>) -> !array
      %result = simulation.array.insert_dynamic %last into %temporary[%index] : (!array, !record, !simulation.logic<65>) -> !array
      simulation.return %result : !array
    }
  }
}
