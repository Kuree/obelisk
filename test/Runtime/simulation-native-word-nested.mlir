// RUN: obelisk-opt %s --schedule-optimize-native-words -o %t.words
// RUN: obelisk-opt %s --mlir-disable-threading --schedule-optimize-native-words -o %t.serial
// RUN: diff %t.words %t.serial
// RUN: FileCheck %s --check-prefix=WORDS < %t.words
// RUN: obelisk-opt %t.words --convert-schedule-word-regions --convert-obelisk-sim-processes-to-llvm-coroutines | obelisk-translate --mlir-to-llvmir | %llvm_dist/bin/opt -passes='coro-early,coro-split<reuse-storage>,coro-cleanup,default<O0>' | %llc -filetype=obj -relocation-model=pic -o %t.o
// RUN: %target_cxx %t.o %runtime_link_flags -o %t.exe
// RUN: %t.exe | FileCheck %s

// LRM 4.6 and 11.4.8: each lane first receives zero, then two ordered XORs.
// Different outer iterations are disjoint even though each inner iteration
// reads and writes the same lane. CSE must preserve the nested loop proof.
// WORDS-LABEL: simulation.func @nested
// WORDS: schedule.word.region 8
// WORDS: simulation.logic.binary xor {{.*}}!simulation.logic<8>
// WORDS-LABEL: simulation.func @nested_conditional
// WORDS: schedule.word.region 8
// WORDS-LABEL: simulation.func @unsigned_nested
// WORDS: schedule.word.region 8
// WORDS: arith.cmpi ult, {{.*}} : i8
// CHECK: nested words 1 1 1
!word = !simulation.logic<8>
!array = !simulation.unpacked_array<0 : 1 x !simulation.logic<8>>
!bit = !simulation.logic<1>
module attributes {llvm.data_layout = "e-m:e-p:64:64-i64:64-n8:16:32:64-S128", llvm.target_triple = "x86_64-unknown-linux-gnu"} {
  simulation.design @nested attributes {
    schedule.nba.transient_observable = array<i64>,
    schedule.nba.private_influence = array<i64: 1>,
    compute_graph = #schedule.graph<version = 1, vpi = off, workers = 1,
      nodes = [], edges = [], regions = [
        #schedule.region<kind = active, groups = []>,
        #schedule.region<kind = nba, groups = []>,
        #schedule.region<kind = observed, groups = []>,
        #schedule.region<kind = reactive, groups = []>,
        #schedule.region<kind = postponed, groups = []>]>} {
    simulation.scope.decl 0
    simulation.code_unit.decl 1 in 0 root_initializer hierarchy "root"
    simulation.code_unit.decl 2 in 0 function hierarchy "nested"
    simulation.code_unit.decl 3 in 0 function hierarchy "nested_conditional"
    simulation.code_unit.decl 4 in 0 function hierarchy "unsigned_nested"
    simulation.storage.decl 0 in 0 : !array design
    simulation.storage.decl 1 in 0 : !word design
    simulation.func @root(%ctx: !simulation.context {simulation.capture_kind = 0 : i32}) attributes {entry_kind = 0 : i32, code_unit_id = 1 : i64} {
      %src = simulation.context.storage %ctx[0] : !simulation.ref<!array>
      %dst = simulation.context.storage %ctx[1] : !simulation.ref<!word>
      %a = simulation.logic.constant 85 : i8, 12 : i8 : !word
      %b = simulation.logic.constant 60 : i8, 48 : i8 : !word
      %values = simulation.aggregate.construct %a, %b : (!word, !word) -> !array
      simulation.ref.store %values to %src : !array, !simulation.ref<!array>
      simulation.call @nested(%ctx, %src, %dst) : (!simulation.context, !simulation.ref<!array>, !simulation.ref<!word>) -> ()
      %actual = simulation.ref.load %dst : !simulation.ref<!word> -> !word
      %expected = simulation.logic.constant 65 : i8, 60 : i8 : !word
      %ok = simulation.logic.compare case_eq %actual, %expected : (!word, !word) -> i1
      simulation.call @nested_conditional(%ctx, %src, %dst) : (!simulation.context, !simulation.ref<!array>, !simulation.ref<!word>) -> ()
      %conditional = simulation.ref.load %dst : !simulation.ref<!word> -> !word
      %conditional_expected = simulation.logic.constant 65 : i8, 16 : i8 : !word
      %conditional_ok = simulation.logic.compare case_eq %conditional, %conditional_expected : (!word, !word) -> i1
      simulation.call @unsigned_nested(%ctx, %src, %dst) : (!simulation.context, !simulation.ref<!array>, !simulation.ref<!word>) -> ()
      %unsigned_result = simulation.ref.load %dst : !simulation.ref<!word> -> !word
      %unsigned_expected = simulation.logic.constant 255 : i8, 0 : i8 : !word
      %unsigned_ok = simulation.logic.compare case_eq %unsigned_result, %unsigned_expected : (!word, !word) -> i1
      %fmt = simulation.bytes.constant "nested words %0d %0d %0d"
      %channel = arith.constant 1 : i32
      simulation.display %ctx to %channel(%fmt, %ok, %conditional_ok, %unsigned_ok) newline = true radix = <decimal> flags = [0, 0, 0, 0] : !simulation.bytes, i1, i1, i1
      simulation.return
    }
    simulation.func @nested(
      %ctx: !simulation.context {simulation.capture_kind = 0 : i32},
      %src: !simulation.ref<!array> {simulation.capture_kind = 3 : i32, simulation.descriptor_id = 0 : i64},
      %dst: !simulation.ref<!word> {simulation.capture_kind = 3 : i32, simulation.descriptor_id = 1 : i64}) attributes {entry_kind = 8 : i32, code_unit_id = 2 : i64, schedule.eval.raw_captures} {
      %zero = arith.constant 0 : i32
      cf.br ^head(%zero : i32)
    ^head(%i: i32):
      %eight = arith.constant 8 : i32
      %more = arith.cmpi ult, %i, %eight : i32
      cf.cond_br %more, ^body, ^exit
    ^body:
      %index = arith.extui %i : i32 to i65
      %initial_storage = simulation.context.storage %ctx[1] : !simulation.ref<!word>
      %initial = simulation.ref.dyn_extract %initial_storage from %index : (!simulation.ref<!word>, i65) -> !simulation.ref<!bit>
      %clear = simulation.logic.constant 0 : i1, 0 : i1 : !bit
      simulation.ref.store %clear to %initial : !bit, !simulation.ref<!bit>
      cf.br ^inner(%zero : i32)
    ^inner(%j: i32):
      %two = arith.constant 2 : i32
      %inner_more = arith.cmpi ult, %j, %two : i32
      cf.cond_br %inner_more, ^update, ^advance
    ^update:
      %way = arith.extui %j : i32 to i65
      %row = simulation.ref.array_element %src[%way] : (!simulation.ref<!array>, i65) -> !simulation.ref<!word>
      %bit = simulation.ref.dyn_extract %row from %index : (!simulation.ref<!word>, i65) -> !simulation.ref<!bit>
      %value = simulation.ref.load %bit : !simulation.ref<!bit> -> !bit
      %fixed = simulation.context.storage %ctx[1] : !simulation.ref<!word>
      %duplicate_index = arith.extui %i : i32 to i65
      %target = simulation.ref.dyn_extract %fixed from %duplicate_index : (!simulation.ref<!word>, i65) -> !simulation.ref<!bit>
      %old = simulation.ref.load %target : !simulation.ref<!bit> -> !bit
      %next_value = simulation.logic.binary xor %old, %value : !bit
      simulation.ref.store %next_value to %target : !bit, !simulation.ref<!bit>
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
    // The outer lane mask guards a finite inner loop containing another
    // lane-dependent branch. Structured predication must mask both levels.
    simulation.func @nested_conditional(
      %ctx: !simulation.context {simulation.capture_kind = 0 : i32},
      %src: !simulation.ref<!array> {simulation.capture_kind = 3 : i32, simulation.descriptor_id = 0 : i64},
      %dst: !simulation.ref<!word> {simulation.capture_kind = 3 : i32, simulation.descriptor_id = 1 : i64}) attributes {entry_kind = 8 : i32, code_unit_id = 3 : i64, schedule.eval.raw_captures} {
      %zero = arith.constant 0 : i32
      cf.br ^head(%zero : i32)
    ^head(%i: i32):
      %eight = arith.constant 8 : i32
      %more = arith.cmpi ult, %i, %eight : i32
      cf.cond_br %more, ^body, ^exit
    ^body:
      %index = arith.extui %i : i32 to i65
      %initial_storage = simulation.context.storage %ctx[1] : !simulation.ref<!word>
      %initial = simulation.ref.dyn_extract %initial_storage from %index : (!simulation.ref<!word>, i65) -> !simulation.ref<!bit>
      %clear = simulation.logic.constant 0 : i1, 0 : i1 : !bit
      simulation.ref.store %clear to %initial : !bit, !simulation.ref<!bit>
      %first = simulation.ref.subelement %src[[0]] : !simulation.ref<!array> -> !simulation.ref<!word>
      %enabled_ref = simulation.ref.dyn_extract %first from %index : (!simulation.ref<!word>, i65) -> !simulation.ref<!bit>
      %enabled_value = simulation.ref.load %enabled_ref : !simulation.ref<!bit> -> !bit
      %known_one = simulation.logic.constant 1 : i1, 0 : i1 : !bit
      %enabled = simulation.logic.compare case_eq %enabled_value, %known_one : (!bit, !bit) -> i1
      cf.cond_br %enabled, ^inner(%zero : i32), ^advance
    ^inner(%j: i32):
      %two = arith.constant 2 : i32
      %inner_more = arith.cmpi ult, %j, %two : i32
      cf.cond_br %inner_more, ^update, ^advance
    ^update:
      %way = arith.extui %j : i32 to i65
      %row = simulation.ref.array_element %src[%way] : (!simulation.ref<!array>, i65) -> !simulation.ref<!word>
      %bit = simulation.ref.dyn_extract %row from %index : (!simulation.ref<!word>, i65) -> !simulation.ref<!bit>
      %value = simulation.ref.load %bit : !simulation.ref<!bit> -> !bit
      %fixed = simulation.context.storage %ctx[1] : !simulation.ref<!word>
      %duplicate_index = arith.extui %i : i32 to i65
      %target = simulation.ref.dyn_extract %fixed from %duplicate_index : (!simulation.ref<!word>, i65) -> !simulation.ref<!bit>
      %skip = simulation.logic.compare case_eq %value, %clear : (!bit, !bit) -> i1
      cf.cond_br %skip, ^next_way, ^apply
    ^apply:
      %old = simulation.ref.load %target : !simulation.ref<!bit> -> !bit
      %next_value = simulation.logic.binary xor %old, %value : !bit
      simulation.ref.store %next_value to %target : !bit, !simulation.ref<!bit>
      cf.br ^next_way
    ^next_way:
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
    simulation.func @unsigned_nested(
      %ctx: !simulation.context {simulation.capture_kind = 0 : i32},
      %src: !simulation.ref<!array> {simulation.capture_kind = 3 : i32, simulation.descriptor_id = 0 : i64},
      %dst: !simulation.ref<!word> {simulation.capture_kind = 3 : i32, simulation.descriptor_id = 1 : i64}) attributes {entry_kind = 8 : i32, code_unit_id = 4 : i64, schedule.eval.raw_captures} {
      %zero = arith.constant 0 : i32
      cf.br ^head(%zero : i32)
    ^head(%i: i32):
      %eight = arith.constant 8 : i32
      %more = arith.cmpi ult, %i, %eight : i32
      cf.cond_br %more, ^body, ^exit
    ^body:
      %index = arith.extui %i : i32 to i65
      %initial_storage = simulation.context.storage %ctx[1] : !simulation.ref<!word>
      %initial = simulation.ref.dyn_extract %initial_storage from %index : (!simulation.ref<!word>, i65) -> !simulation.ref<!bit>
      %clear = simulation.logic.constant 0 : i1, 0 : i1 : !bit
      simulation.ref.store %clear to %initial : !bit, !simulation.ref<!bit>
      %inner_zero = arith.constant 0 : i8
      cf.br ^inner(%inner_zero : i8)
    ^inner(%j: i8):
      // Unsigned 0..130 executes 131 times; a signed comparison against
      // the same i8 bit pattern would incorrectly execute zero times.
      %limit = arith.constant 131 : i8
      %inner_more = arith.cmpi ult, %j, %limit : i8
      cf.cond_br %inner_more, ^update, ^advance
    ^update:
      %old = simulation.ref.load %initial : !simulation.ref<!bit> -> !bit
      %is_zero = simulation.logic.compare case_eq %old, %clear : (!bit, !bit) -> i1
      cf.cond_br %is_zero, ^set, ^clear_again
    ^set:
      %known_one = simulation.logic.constant 1 : i1, 0 : i1 : !bit
      simulation.ref.store %known_one to %initial : !bit, !simulation.ref<!bit>
      cf.br ^next_way
    ^clear_again:
      simulation.ref.store %clear to %initial : !bit, !simulation.ref<!bit>
      cf.br ^next_way
    ^next_way:
      %one = arith.constant 1 : i8
      %next_j = arith.addi %j, %one : i8
      cf.br ^inner(%next_j : i8)
    ^advance:
      %outer_one = arith.constant 1 : i32
      %next_i = arith.addi %i, %outer_one : i32
      cf.br ^head(%next_i : i32)
    ^exit:
      simulation.return
    }
  }
}
