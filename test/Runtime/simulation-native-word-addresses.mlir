// RUN: obelisk-opt %s --schedule-optimize-native-words -o %t.words
// RUN: FileCheck %s --check-prefix=WORDS < %t.words
// RUN: obelisk-opt %t.words --convert-schedule-word-regions --convert-obelisk-sim-processes-to-llvm-coroutines | obelisk-translate --mlir-to-llvmir | %llvm_dist/bin/opt -passes='coro-early,coro-split<reuse-storage>,coro-cleanup,default<O0>' | %llc -filetype=obj -relocation-model=pic -o %t.o
// RUN: %target_cxx %t.o %runtime_link_flags -o %t.exe
// RUN: %t.exe | FileCheck %s --check-prefix=RESULT --implicit-check-not=' 0'

// Direct MLIR inputs. Native -O0 keeps constant folding from hiding codegen errors.
// Expected value/unknown planes encode the LRM truth tables explicitly.
// Address cases cover signed bounds, either range direction, reversals,
// packed value sources, and ordered same-lane aliases.
// WORDS-COUNT-11: schedule.word.region 16
// RESULT: ascending_source 1
// RESULT: ascending_destination 1
// RESULT: negative_bounds 1
// RESULT: descending_loop 1
// RESULT: reverse_source 1
// RESULT: reverse_destination 1
// RESULT: packed_value 1
// RESULT: reversed_value 1
// RESULT: integer_value 1
// RESULT: in_place 1
// RESULT: ordered_writes 1

!array0 = !simulation.packed_array<-1 : -16 x !simulation.logic<1>>
!array1 = !simulation.packed_array<15 : 0 x !simulation.logic<1>>
!array2 = !simulation.packed_array<23 : 8 x !simulation.logic<1>>
!array3 = !simulation.packed_array<8 : 23 x !simulation.logic<1>>
!l1 = !simulation.logic<1>
!l16 = !simulation.logic<16>
module attributes {llvm.data_layout = "e-m:e-p:64:64-i64:64-n8:16:32:64-S128", llvm.target_triple = "x86_64-unknown-linux-gnu"} {
simulation.design @addresses attributes {schedule.nba.transient_observable = array<i64>, schedule.nba.private_influence = array<i64: 1, 2, 4, 5, 7, 8, 10, 11, 13, 14, 16, 17, 19, 20, 22, 23, 25, 26, 28, 29, 31, 32>, compute_graph = #schedule.graph<version = 1, vpi = off, workers = 1, nodes = [], edges = [], regions = [#schedule.region<kind = active, groups = []>, #schedule.region<kind = nba, groups = []>, #schedule.region<kind = observed, groups = []>, #schedule.region<kind = reactive, groups = []>, #schedule.region<kind = postponed, groups = []>]>} {
simulation.scope.decl 0
simulation.code_unit.decl 1 in 0 root_initializer hierarchy "root"
simulation.storage.decl 0 in 0 : !array3 design
simulation.storage.decl 1 in 0 : !array2 design
simulation.storage.decl 2 in 0 : !array2 design
simulation.code_unit.decl 3 in 0 function hierarchy "word_ascending_source"
simulation.storage.decl 3 in 0 : !array2 design
simulation.storage.decl 4 in 0 : !array3 design
simulation.storage.decl 5 in 0 : !array3 design
simulation.code_unit.decl 5 in 0 function hierarchy "word_ascending_destination"
simulation.storage.decl 6 in 0 : !array0 design
simulation.storage.decl 7 in 0 : !array0 design
simulation.storage.decl 8 in 0 : !array0 design
simulation.code_unit.decl 7 in 0 function hierarchy "word_negative_bounds"
simulation.storage.decl 9 in 0 : !array1 design
simulation.storage.decl 10 in 0 : !array1 design
simulation.storage.decl 11 in 0 : !array1 design
simulation.code_unit.decl 9 in 0 function hierarchy "word_descending_loop"
simulation.storage.decl 12 in 0 : !array1 design
simulation.storage.decl 13 in 0 : !array1 design
simulation.storage.decl 14 in 0 : !array1 design
simulation.code_unit.decl 11 in 0 function hierarchy "word_reverse_source"
simulation.storage.decl 15 in 0 : !array1 design
simulation.storage.decl 16 in 0 : !array1 design
simulation.storage.decl 17 in 0 : !array1 design
simulation.code_unit.decl 13 in 0 function hierarchy "word_reverse_destination"
simulation.storage.decl 18 in 0 : !l16 design
simulation.storage.decl 19 in 0 : !l16 design
simulation.storage.decl 20 in 0 : !l16 design
simulation.code_unit.decl 15 in 0 function hierarchy "word_packed_value"
simulation.storage.decl 21 in 0 : !l16 design
simulation.storage.decl 22 in 0 : !l16 design
simulation.storage.decl 23 in 0 : !l16 design
simulation.code_unit.decl 17 in 0 function hierarchy "word_reversed_value"
simulation.storage.decl 24 in 0 : i16 design
simulation.storage.decl 25 in 0 : i16 design
simulation.storage.decl 26 in 0 : i16 design
simulation.code_unit.decl 19 in 0 function hierarchy "word_integer_value"
simulation.storage.decl 27 in 0 : !l16 design
simulation.storage.decl 28 in 0 : !l16 design
simulation.storage.decl 29 in 0 : !l16 design
simulation.code_unit.decl 21 in 0 function hierarchy "word_in_place"
simulation.storage.decl 30 in 0 : !l16 design
simulation.storage.decl 31 in 0 : !l16 design
simulation.storage.decl 32 in 0 : !l16 design
simulation.code_unit.decl 23 in 0 function hierarchy "word_ordered_writes"
simulation.func @root(%ctx: !simulation.context {simulation.capture_kind = 0 : i32}) attributes {entry_kind = 0 : i32, code_unit_id = 1 : i64} {
  %r0_0 = simulation.context.storage %ctx[0] : !simulation.ref<!array3>
  %c0_0 = simulation.logic.constant 38563 : i16, 28869 : i16 : !l16
  %v0_0 = simulation.packed.unflatten %c0_0 : (!l16) -> !array3
  simulation.ref.store %v0_0 to %r0_0 : !array3, !simulation.ref<!array3>
  %r0_1 = simulation.context.storage %ctx[1] : !simulation.ref<!array2>
  %c0_1 = simulation.logic.constant 43690 : i16, 0 : i16 : !l16
  %v0_1 = simulation.packed.unflatten %c0_1 : (!l16) -> !array2
  simulation.ref.store %v0_1 to %r0_1 : !array2, !simulation.ref<!array2>
  %r0_2 = simulation.context.storage %ctx[2] : !simulation.ref<!array2>
  %c0_2 = simulation.logic.constant 43690 : i16, 0 : i16 : !l16
  %v0_2 = simulation.packed.unflatten %c0_2 : (!l16) -> !array2
  simulation.ref.store %v0_2 to %r0_2 : !array2, !simulation.ref<!array2>
  %expected0 = simulation.logic.constant 50537 : i16, 41742 : i16 : !l16
  simulation.call @word_ascending_source(%ctx, %r0_0, %r0_2) : (!simulation.context, !simulation.ref<!array3>, !simulation.ref<!array2>) -> ()
  %out0_1 = simulation.ref.load %r0_2 : !simulation.ref<!array2> -> !array2
  %flat0_1 = simulation.packed.flatten %out0_1 : (!array2) -> !l16
  %ok0_1 = simulation.logic.compare case_eq %flat0_1, %expected0 : (!l16, !l16) -> i1
  %fmt0 = simulation.bytes.constant "ascending_source %0d"
  %ch0 = arith.constant 1 : i32
  simulation.display %ctx to %ch0(%fmt0, %ok0_1) newline = true radix = <decimal> flags = [0,0] : !simulation.bytes, i1
  %r1_0 = simulation.context.storage %ctx[3] : !simulation.ref<!array2>
  %c1_0 = simulation.logic.constant 38563 : i16, 28869 : i16 : !l16
  %v1_0 = simulation.packed.unflatten %c1_0 : (!l16) -> !array2
  simulation.ref.store %v1_0 to %r1_0 : !array2, !simulation.ref<!array2>
  %r1_1 = simulation.context.storage %ctx[4] : !simulation.ref<!array3>
  %c1_1 = simulation.logic.constant 43690 : i16, 0 : i16 : !l16
  %v1_1 = simulation.packed.unflatten %c1_1 : (!l16) -> !array3
  simulation.ref.store %v1_1 to %r1_1 : !array3, !simulation.ref<!array3>
  %r1_2 = simulation.context.storage %ctx[5] : !simulation.ref<!array3>
  %c1_2 = simulation.logic.constant 43690 : i16, 0 : i16 : !l16
  %v1_2 = simulation.packed.unflatten %c1_2 : (!l16) -> !array3
  simulation.ref.store %v1_2 to %r1_2 : !array3, !simulation.ref<!array3>
  %expected1 = simulation.logic.constant 50537 : i16, 41742 : i16 : !l16
  simulation.call @word_ascending_destination(%ctx, %r1_0, %r1_2) : (!simulation.context, !simulation.ref<!array2>, !simulation.ref<!array3>) -> ()
  %out1_1 = simulation.ref.load %r1_2 : !simulation.ref<!array3> -> !array3
  %flat1_1 = simulation.packed.flatten %out1_1 : (!array3) -> !l16
  %ok1_1 = simulation.logic.compare case_eq %flat1_1, %expected1 : (!l16, !l16) -> i1
  %fmt1 = simulation.bytes.constant "ascending_destination %0d"
  %ch1 = arith.constant 1 : i32
  simulation.display %ctx to %ch1(%fmt1, %ok1_1) newline = true radix = <decimal> flags = [0,0] : !simulation.bytes, i1
  %r2_0 = simulation.context.storage %ctx[6] : !simulation.ref<!array0>
  %c2_0 = simulation.logic.constant 38563 : i16, 28869 : i16 : !l16
  %v2_0 = simulation.packed.unflatten %c2_0 : (!l16) -> !array0
  simulation.ref.store %v2_0 to %r2_0 : !array0, !simulation.ref<!array0>
  %r2_1 = simulation.context.storage %ctx[7] : !simulation.ref<!array0>
  %c2_1 = simulation.logic.constant 43690 : i16, 0 : i16 : !l16
  %v2_1 = simulation.packed.unflatten %c2_1 : (!l16) -> !array0
  simulation.ref.store %v2_1 to %r2_1 : !array0, !simulation.ref<!array0>
  %r2_2 = simulation.context.storage %ctx[8] : !simulation.ref<!array0>
  %c2_2 = simulation.logic.constant 43690 : i16, 0 : i16 : !l16
  %v2_2 = simulation.packed.unflatten %c2_2 : (!l16) -> !array0
  simulation.ref.store %v2_2 to %r2_2 : !array0, !simulation.ref<!array0>
  %expected2 = simulation.logic.constant 38563 : i16, 28869 : i16 : !l16
  simulation.call @word_negative_bounds(%ctx, %r2_0, %r2_2) : (!simulation.context, !simulation.ref<!array0>, !simulation.ref<!array0>) -> ()
  %out2_1 = simulation.ref.load %r2_2 : !simulation.ref<!array0> -> !array0
  %flat2_1 = simulation.packed.flatten %out2_1 : (!array0) -> !l16
  %ok2_1 = simulation.logic.compare case_eq %flat2_1, %expected2 : (!l16, !l16) -> i1
  %fmt2 = simulation.bytes.constant "negative_bounds %0d"
  %ch2 = arith.constant 1 : i32
  simulation.display %ctx to %ch2(%fmt2, %ok2_1) newline = true radix = <decimal> flags = [0,0] : !simulation.bytes, i1
  %r3_0 = simulation.context.storage %ctx[9] : !simulation.ref<!array1>
  %c3_0 = simulation.logic.constant 38563 : i16, 28869 : i16 : !l16
  %v3_0 = simulation.packed.unflatten %c3_0 : (!l16) -> !array1
  simulation.ref.store %v3_0 to %r3_0 : !array1, !simulation.ref<!array1>
  %r3_1 = simulation.context.storage %ctx[10] : !simulation.ref<!array1>
  %c3_1 = simulation.logic.constant 43690 : i16, 0 : i16 : !l16
  %v3_1 = simulation.packed.unflatten %c3_1 : (!l16) -> !array1
  simulation.ref.store %v3_1 to %r3_1 : !array1, !simulation.ref<!array1>
  %r3_2 = simulation.context.storage %ctx[11] : !simulation.ref<!array1>
  %c3_2 = simulation.logic.constant 43690 : i16, 0 : i16 : !l16
  %v3_2 = simulation.packed.unflatten %c3_2 : (!l16) -> !array1
  simulation.ref.store %v3_2 to %r3_2 : !array1, !simulation.ref<!array1>
  %expected3 = simulation.logic.constant 38563 : i16, 28869 : i16 : !l16
  simulation.call @word_descending_loop(%ctx, %r3_0, %r3_2) : (!simulation.context, !simulation.ref<!array1>, !simulation.ref<!array1>) -> ()
  %out3_1 = simulation.ref.load %r3_2 : !simulation.ref<!array1> -> !array1
  %flat3_1 = simulation.packed.flatten %out3_1 : (!array1) -> !l16
  %ok3_1 = simulation.logic.compare case_eq %flat3_1, %expected3 : (!l16, !l16) -> i1
  %fmt3 = simulation.bytes.constant "descending_loop %0d"
  %ch3 = arith.constant 1 : i32
  simulation.display %ctx to %ch3(%fmt3, %ok3_1) newline = true radix = <decimal> flags = [0,0] : !simulation.bytes, i1
  %r4_0 = simulation.context.storage %ctx[12] : !simulation.ref<!array1>
  %c4_0 = simulation.logic.constant 38563 : i16, 28869 : i16 : !l16
  %v4_0 = simulation.packed.unflatten %c4_0 : (!l16) -> !array1
  simulation.ref.store %v4_0 to %r4_0 : !array1, !simulation.ref<!array1>
  %r4_1 = simulation.context.storage %ctx[13] : !simulation.ref<!array1>
  %c4_1 = simulation.logic.constant 43690 : i16, 0 : i16 : !l16
  %v4_1 = simulation.packed.unflatten %c4_1 : (!l16) -> !array1
  simulation.ref.store %v4_1 to %r4_1 : !array1, !simulation.ref<!array1>
  %r4_2 = simulation.context.storage %ctx[14] : !simulation.ref<!array1>
  %c4_2 = simulation.logic.constant 43690 : i16, 0 : i16 : !l16
  %v4_2 = simulation.packed.unflatten %c4_2 : (!l16) -> !array1
  simulation.ref.store %v4_2 to %r4_2 : !array1, !simulation.ref<!array1>
  %expected4 = simulation.logic.constant 50537 : i16, 41742 : i16 : !l16
  simulation.call @word_reverse_source(%ctx, %r4_0, %r4_2) : (!simulation.context, !simulation.ref<!array1>, !simulation.ref<!array1>) -> ()
  %out4_1 = simulation.ref.load %r4_2 : !simulation.ref<!array1> -> !array1
  %flat4_1 = simulation.packed.flatten %out4_1 : (!array1) -> !l16
  %ok4_1 = simulation.logic.compare case_eq %flat4_1, %expected4 : (!l16, !l16) -> i1
  %fmt4 = simulation.bytes.constant "reverse_source %0d"
  %ch4 = arith.constant 1 : i32
  simulation.display %ctx to %ch4(%fmt4, %ok4_1) newline = true radix = <decimal> flags = [0,0] : !simulation.bytes, i1
  %r5_0 = simulation.context.storage %ctx[15] : !simulation.ref<!array1>
  %c5_0 = simulation.logic.constant 38563 : i16, 28869 : i16 : !l16
  %v5_0 = simulation.packed.unflatten %c5_0 : (!l16) -> !array1
  simulation.ref.store %v5_0 to %r5_0 : !array1, !simulation.ref<!array1>
  %r5_1 = simulation.context.storage %ctx[16] : !simulation.ref<!array1>
  %c5_1 = simulation.logic.constant 43690 : i16, 0 : i16 : !l16
  %v5_1 = simulation.packed.unflatten %c5_1 : (!l16) -> !array1
  simulation.ref.store %v5_1 to %r5_1 : !array1, !simulation.ref<!array1>
  %r5_2 = simulation.context.storage %ctx[17] : !simulation.ref<!array1>
  %c5_2 = simulation.logic.constant 43690 : i16, 0 : i16 : !l16
  %v5_2 = simulation.packed.unflatten %c5_2 : (!l16) -> !array1
  simulation.ref.store %v5_2 to %r5_2 : !array1, !simulation.ref<!array1>
  %expected5 = simulation.logic.constant 50537 : i16, 41742 : i16 : !l16
  simulation.call @word_reverse_destination(%ctx, %r5_0, %r5_2) : (!simulation.context, !simulation.ref<!array1>, !simulation.ref<!array1>) -> ()
  %out5_1 = simulation.ref.load %r5_2 : !simulation.ref<!array1> -> !array1
  %flat5_1 = simulation.packed.flatten %out5_1 : (!array1) -> !l16
  %ok5_1 = simulation.logic.compare case_eq %flat5_1, %expected5 : (!l16, !l16) -> i1
  %fmt5 = simulation.bytes.constant "reverse_destination %0d"
  %ch5 = arith.constant 1 : i32
  simulation.display %ctx to %ch5(%fmt5, %ok5_1) newline = true radix = <decimal> flags = [0,0] : !simulation.bytes, i1
  %r6_0 = simulation.context.storage %ctx[18] : !simulation.ref<!l16>
  %c6_0 = simulation.logic.constant 38563 : i16, 28869 : i16 : !l16
  %v6_0 = simulation.logic.resize %c6_0 signed = false : !l16 -> !l16
  simulation.ref.store %v6_0 to %r6_0 : !l16, !simulation.ref<!l16>
  %r6_1 = simulation.context.storage %ctx[19] : !simulation.ref<!l16>
  %c6_1 = simulation.logic.constant 43690 : i16, 0 : i16 : !l16
  %v6_1 = simulation.logic.resize %c6_1 signed = false : !l16 -> !l16
  simulation.ref.store %v6_1 to %r6_1 : !l16, !simulation.ref<!l16>
  %r6_2 = simulation.context.storage %ctx[20] : !simulation.ref<!l16>
  %c6_2 = simulation.logic.constant 43690 : i16, 0 : i16 : !l16
  %v6_2 = simulation.logic.resize %c6_2 signed = false : !l16 -> !l16
  simulation.ref.store %v6_2 to %r6_2 : !l16, !simulation.ref<!l16>
  %expected6 = simulation.logic.constant 38563 : i16, 28869 : i16 : !l16
  simulation.call @word_packed_value(%ctx, %r6_0, %r6_2) : (!simulation.context, !simulation.ref<!l16>, !simulation.ref<!l16>) -> ()
  %out6_1 = simulation.ref.load %r6_2 : !simulation.ref<!l16> -> !l16
  %ok6_1 = simulation.logic.compare case_eq %out6_1, %expected6 : (!l16, !l16) -> i1
  %fmt6 = simulation.bytes.constant "packed_value %0d"
  %ch6 = arith.constant 1 : i32
  simulation.display %ctx to %ch6(%fmt6, %ok6_1) newline = true radix = <decimal> flags = [0,0] : !simulation.bytes, i1
  %r7_0 = simulation.context.storage %ctx[21] : !simulation.ref<!l16>
  %c7_0 = simulation.logic.constant 38563 : i16, 28869 : i16 : !l16
  %v7_0 = simulation.logic.resize %c7_0 signed = false : !l16 -> !l16
  simulation.ref.store %v7_0 to %r7_0 : !l16, !simulation.ref<!l16>
  %r7_1 = simulation.context.storage %ctx[22] : !simulation.ref<!l16>
  %c7_1 = simulation.logic.constant 43690 : i16, 0 : i16 : !l16
  %v7_1 = simulation.logic.resize %c7_1 signed = false : !l16 -> !l16
  simulation.ref.store %v7_1 to %r7_1 : !l16, !simulation.ref<!l16>
  %r7_2 = simulation.context.storage %ctx[23] : !simulation.ref<!l16>
  %c7_2 = simulation.logic.constant 43690 : i16, 0 : i16 : !l16
  %v7_2 = simulation.logic.resize %c7_2 signed = false : !l16 -> !l16
  simulation.ref.store %v7_2 to %r7_2 : !l16, !simulation.ref<!l16>
  %expected7 = simulation.logic.constant 50537 : i16, 41742 : i16 : !l16
  simulation.call @word_reversed_value(%ctx, %r7_0, %r7_2) : (!simulation.context, !simulation.ref<!l16>, !simulation.ref<!l16>) -> ()
  %out7_1 = simulation.ref.load %r7_2 : !simulation.ref<!l16> -> !l16
  %ok7_1 = simulation.logic.compare case_eq %out7_1, %expected7 : (!l16, !l16) -> i1
  %fmt7 = simulation.bytes.constant "reversed_value %0d"
  %ch7 = arith.constant 1 : i32
  simulation.display %ctx to %ch7(%fmt7, %ok7_1) newline = true radix = <decimal> flags = [0,0] : !simulation.bytes, i1
  %r8_0 = simulation.context.storage %ctx[24] : !simulation.ref<i16>
  %v8_0 = arith.constant 34338 : i16
  simulation.ref.store %v8_0 to %r8_0 : i16, !simulation.ref<i16>
  %r8_1 = simulation.context.storage %ctx[25] : !simulation.ref<i16>
  %v8_1 = arith.constant 43690 : i16
  simulation.ref.store %v8_1 to %r8_1 : i16, !simulation.ref<i16>
  %r8_2 = simulation.context.storage %ctx[26] : !simulation.ref<i16>
  %v8_2 = arith.constant 43690 : i16
  simulation.ref.store %v8_2 to %r8_2 : i16, !simulation.ref<i16>
  %expected8 = arith.constant 17505 : i16
  simulation.call @word_integer_value(%ctx, %r8_0, %r8_2) : (!simulation.context, !simulation.ref<i16>, !simulation.ref<i16>) -> ()
  %out8_1 = simulation.ref.load %r8_2 : !simulation.ref<i16> -> i16
  %ok8_1 = arith.cmpi eq, %out8_1, %expected8 : i16
  %fmt8 = simulation.bytes.constant "integer_value %0d"
  %ch8 = arith.constant 1 : i32
  simulation.display %ctx to %ch8(%fmt8, %ok8_1) newline = true radix = <decimal> flags = [0,0] : !simulation.bytes, i1
  %r9_0 = simulation.context.storage %ctx[27] : !simulation.ref<!l16>
  %c9_0 = simulation.logic.constant 38563 : i16, 28869 : i16 : !l16
  %v9_0 = simulation.logic.resize %c9_0 signed = false : !l16 -> !l16
  simulation.ref.store %v9_0 to %r9_0 : !l16, !simulation.ref<!l16>
  %r9_1 = simulation.context.storage %ctx[28] : !simulation.ref<!l16>
  %c9_1 = simulation.logic.constant 43690 : i16, 0 : i16 : !l16
  %v9_1 = simulation.logic.resize %c9_1 signed = false : !l16 -> !l16
  simulation.ref.store %v9_1 to %r9_1 : !l16, !simulation.ref<!l16>
  %r9_2 = simulation.context.storage %ctx[29] : !simulation.ref<!l16>
  %c9_2 = simulation.logic.constant 43690 : i16, 0 : i16 : !l16
  %v9_2 = simulation.logic.resize %c9_2 signed = false : !l16 -> !l16
  simulation.ref.store %v9_2 to %r9_2 : !l16, !simulation.ref<!l16>
  %expected9 = simulation.logic.constant 21845 : i16, 0 : i16 : !l16
  simulation.call @word_in_place(%ctx, %r9_0, %r9_2) : (!simulation.context, !simulation.ref<!l16>, !simulation.ref<!l16>) -> ()
  %out9_1 = simulation.ref.load %r9_2 : !simulation.ref<!l16> -> !l16
  %ok9_1 = simulation.logic.compare case_eq %out9_1, %expected9 : (!l16, !l16) -> i1
  %fmt9 = simulation.bytes.constant "in_place %0d"
  %ch9 = arith.constant 1 : i32
  simulation.display %ctx to %ch9(%fmt9, %ok9_1) newline = true radix = <decimal> flags = [0,0] : !simulation.bytes, i1
  %r10_0 = simulation.context.storage %ctx[30] : !simulation.ref<!l16>
  %c10_0 = simulation.logic.constant 38563 : i16, 28869 : i16 : !l16
  %v10_0 = simulation.logic.resize %c10_0 signed = false : !l16 -> !l16
  simulation.ref.store %v10_0 to %r10_0 : !l16, !simulation.ref<!l16>
  %r10_1 = simulation.context.storage %ctx[31] : !simulation.ref<!l16>
  %c10_1 = simulation.logic.constant 43690 : i16, 0 : i16 : !l16
  %v10_1 = simulation.logic.resize %c10_1 signed = false : !l16 -> !l16
  simulation.ref.store %v10_1 to %r10_1 : !l16, !simulation.ref<!l16>
  %r10_2 = simulation.context.storage %ctx[32] : !simulation.ref<!l16>
  %c10_2 = simulation.logic.constant 43690 : i16, 0 : i16 : !l16
  %v10_2 = simulation.logic.resize %c10_2 signed = false : !l16 -> !l16
  simulation.ref.store %v10_2 to %r10_2 : !l16, !simulation.ref<!l16>
  %expected10 = simulation.logic.constant 2328 : i16, 28869 : i16 : !l16
  simulation.call @word_ordered_writes(%ctx, %r10_0, %r10_2) : (!simulation.context, !simulation.ref<!l16>, !simulation.ref<!l16>) -> ()
  %out10_1 = simulation.ref.load %r10_2 : !simulation.ref<!l16> -> !l16
  %ok10_1 = simulation.logic.compare case_eq %out10_1, %expected10 : (!l16, !l16) -> i1
  %fmt10 = simulation.bytes.constant "ordered_writes %0d"
  %ch10 = arith.constant 1 : i32
  simulation.display %ctx to %ch10(%fmt10, %ok10_1) newline = true radix = <decimal> flags = [0,0] : !simulation.bytes, i1
  simulation.return
}
simulation.func @word_ascending_source(%ctx: !simulation.context {simulation.capture_kind = 0 : i32}, %src: !simulation.ref<!array3> {simulation.capture_kind = 3 : i32, simulation.descriptor_id = 0 : i64}, %dst: !simulation.ref<!array2> {simulation.capture_kind = 3 : i32, simulation.descriptor_id = 2 : i64}) attributes {entry_kind = 8 : i32, code_unit_id = 3 : i64, schedule.eval.raw_captures} {
  %start = arith.constant 8 : i32
  cf.br ^head(%start : i32)
^head(%i: i32):
  %limit = arith.constant 24 : i32
  %more = arith.cmpi slt, %i, %limit : i32
  cf.cond_br %more, ^body, ^exit
^body:
  %index = arith.extsi %i : i32 to i65
  %out = simulation.ref.array_element %dst[%index] : (!simulation.ref<!array2>, i65) -> !simulation.ref<!l1>
  %snapshot = simulation.ref.load %src : !simulation.ref<!array3> -> !array3
  %value = simulation.array.extract_dynamic %snapshot[%index] : (!array3, i65) -> !l1
  simulation.ref.store %value to %out : !l1, !simulation.ref<!l1>
  %one = arith.constant 1 : i32
  %next = arith.addi %i, %one : i32
  cf.br ^head(%next : i32)
^exit:
  simulation.return
}
simulation.func @word_ascending_destination(%ctx: !simulation.context {simulation.capture_kind = 0 : i32}, %src: !simulation.ref<!array2> {simulation.capture_kind = 3 : i32, simulation.descriptor_id = 3 : i64}, %dst: !simulation.ref<!array3> {simulation.capture_kind = 3 : i32, simulation.descriptor_id = 5 : i64}) attributes {entry_kind = 8 : i32, code_unit_id = 5 : i64, schedule.eval.raw_captures} {
  %start = arith.constant 8 : i32
  cf.br ^head(%start : i32)
^head(%i: i32):
  %limit = arith.constant 24 : i32
  %more = arith.cmpi slt, %i, %limit : i32
  cf.cond_br %more, ^body, ^exit
^body:
  %index = arith.extsi %i : i32 to i65
  %out = simulation.ref.array_element %dst[%index] : (!simulation.ref<!array3>, i65) -> !simulation.ref<!l1>
  %in = simulation.ref.array_element %src[%index] : (!simulation.ref<!array2>, i65) -> !simulation.ref<!l1>
  %value = simulation.ref.load %in : !simulation.ref<!l1> -> !l1
  simulation.ref.store %value to %out : !l1, !simulation.ref<!l1>
  %one = arith.constant 1 : i32
  %next = arith.addi %i, %one : i32
  cf.br ^head(%next : i32)
^exit:
  simulation.return
}
simulation.func @word_negative_bounds(%ctx: !simulation.context {simulation.capture_kind = 0 : i32}, %src: !simulation.ref<!array0> {simulation.capture_kind = 3 : i32, simulation.descriptor_id = 6 : i64}, %dst: !simulation.ref<!array0> {simulation.capture_kind = 3 : i32, simulation.descriptor_id = 8 : i64}) attributes {entry_kind = 8 : i32, code_unit_id = 7 : i64, schedule.eval.raw_captures} {
  %start = arith.constant -16 : i32
  cf.br ^head(%start : i32)
^head(%i: i32):
  %limit = arith.constant 0 : i32
  %more = arith.cmpi slt, %i, %limit : i32
  cf.cond_br %more, ^body, ^exit
^body:
  %index = arith.extsi %i : i32 to i65
  %out = simulation.ref.array_element %dst[%index] : (!simulation.ref<!array0>, i65) -> !simulation.ref<!l1>
  %snapshot = simulation.ref.load %src : !simulation.ref<!array0> -> !array0
  %value = simulation.array.extract_dynamic %snapshot[%index] : (!array0, i65) -> !l1
  simulation.ref.store %value to %out : !l1, !simulation.ref<!l1>
  %one = arith.constant 1 : i32
  %next = arith.addi %i, %one : i32
  cf.br ^head(%next : i32)
^exit:
  simulation.return
}
simulation.func @word_descending_loop(%ctx: !simulation.context {simulation.capture_kind = 0 : i32}, %src: !simulation.ref<!array1> {simulation.capture_kind = 3 : i32, simulation.descriptor_id = 9 : i64}, %dst: !simulation.ref<!array1> {simulation.capture_kind = 3 : i32, simulation.descriptor_id = 11 : i64}) attributes {entry_kind = 8 : i32, code_unit_id = 9 : i64, schedule.eval.raw_captures} {
  %start = arith.constant 15 : i32
  cf.br ^head(%start : i32)
^head(%i: i32):
  %limit = arith.constant -1 : i32
  %more = arith.cmpi sgt, %i, %limit : i32
  cf.cond_br %more, ^body, ^exit
^body:
  %index = arith.extsi %i : i32 to i65
  %out = simulation.ref.array_element %dst[%index] : (!simulation.ref<!array1>, i65) -> !simulation.ref<!l1>
  %in = simulation.ref.array_element %src[%index] : (!simulation.ref<!array1>, i65) -> !simulation.ref<!l1>
  %value = simulation.ref.load %in : !simulation.ref<!l1> -> !l1
  simulation.ref.store %value to %out : !l1, !simulation.ref<!l1>
  %one = arith.constant 1 : i32
  %next = arith.subi %i, %one : i32
  cf.br ^head(%next : i32)
^exit:
  simulation.return
}
simulation.func @word_reverse_source(%ctx: !simulation.context {simulation.capture_kind = 0 : i32}, %src: !simulation.ref<!array1> {simulation.capture_kind = 3 : i32, simulation.descriptor_id = 12 : i64}, %dst: !simulation.ref<!array1> {simulation.capture_kind = 3 : i32, simulation.descriptor_id = 14 : i64}) attributes {entry_kind = 8 : i32, code_unit_id = 11 : i64, schedule.eval.raw_captures} {
  %start = arith.constant 0 : i32
  cf.br ^head(%start : i32)
^head(%i: i32):
  %limit = arith.constant 16 : i32
  %more = arith.cmpi slt, %i, %limit : i32
  cf.cond_br %more, ^body, ^exit
^body:
  %index = arith.extsi %i : i32 to i65
  %last = arith.constant 15 : i65
  %reverse = arith.subi %last, %index : i65
  %out = simulation.ref.array_element %dst[%index] : (!simulation.ref<!array1>, i65) -> !simulation.ref<!l1>
  %in = simulation.ref.array_element %src[%reverse] : (!simulation.ref<!array1>, i65) -> !simulation.ref<!l1>
  %value = simulation.ref.load %in : !simulation.ref<!l1> -> !l1
  simulation.ref.store %value to %out : !l1, !simulation.ref<!l1>
  %one = arith.constant 1 : i32
  %next = arith.addi %i, %one : i32
  cf.br ^head(%next : i32)
^exit:
  simulation.return
}
simulation.func @word_reverse_destination(%ctx: !simulation.context {simulation.capture_kind = 0 : i32}, %src: !simulation.ref<!array1> {simulation.capture_kind = 3 : i32, simulation.descriptor_id = 15 : i64}, %dst: !simulation.ref<!array1> {simulation.capture_kind = 3 : i32, simulation.descriptor_id = 17 : i64}) attributes {entry_kind = 8 : i32, code_unit_id = 13 : i64, schedule.eval.raw_captures} {
  %start = arith.constant 0 : i32
  cf.br ^head(%start : i32)
^head(%i: i32):
  %limit = arith.constant 16 : i32
  %more = arith.cmpi slt, %i, %limit : i32
  cf.cond_br %more, ^body, ^exit
^body:
  %index = arith.extsi %i : i32 to i65
  %last = arith.constant 15 : i65
  %reverse = arith.subi %last, %index : i65
  %out = simulation.ref.array_element %dst[%reverse] : (!simulation.ref<!array1>, i65) -> !simulation.ref<!l1>
  %in = simulation.ref.array_element %src[%index] : (!simulation.ref<!array1>, i65) -> !simulation.ref<!l1>
  %value = simulation.ref.load %in : !simulation.ref<!l1> -> !l1
  simulation.ref.store %value to %out : !l1, !simulation.ref<!l1>
  %one = arith.constant 1 : i32
  %next = arith.addi %i, %one : i32
  cf.br ^head(%next : i32)
^exit:
  simulation.return
}
simulation.func @word_packed_value(%ctx: !simulation.context {simulation.capture_kind = 0 : i32}, %src: !simulation.ref<!l16> {simulation.capture_kind = 3 : i32, simulation.descriptor_id = 18 : i64}, %dst: !simulation.ref<!l16> {simulation.capture_kind = 3 : i32, simulation.descriptor_id = 20 : i64}) attributes {entry_kind = 8 : i32, code_unit_id = 15 : i64, schedule.eval.raw_captures} {
  %snapshot = simulation.ref.load %src : !simulation.ref<!l16> -> !l16
  %start = arith.constant 0 : i32
  cf.br ^head(%start : i32)
^head(%i: i32):
  %limit = arith.constant 16 : i32
  %more = arith.cmpi slt, %i, %limit : i32
  cf.cond_br %more, ^body, ^exit
^body:
  %index = arith.extsi %i : i32 to i65
  %out = simulation.ref.dyn_extract %dst from %index : (!simulation.ref<!l16>, i65) -> !simulation.ref<!l1>
  %value = simulation.logic.dyn_extract %snapshot from %index : (!l16, i65) -> !l1
  simulation.ref.store %value to %out : !l1, !simulation.ref<!l1>
  %one = arith.constant 1 : i32
  %next = arith.addi %i, %one : i32
  cf.br ^head(%next : i32)
^exit:
  simulation.return
}
simulation.func @word_reversed_value(%ctx: !simulation.context {simulation.capture_kind = 0 : i32}, %src: !simulation.ref<!l16> {simulation.capture_kind = 3 : i32, simulation.descriptor_id = 21 : i64}, %dst: !simulation.ref<!l16> {simulation.capture_kind = 3 : i32, simulation.descriptor_id = 23 : i64}) attributes {entry_kind = 8 : i32, code_unit_id = 17 : i64, schedule.eval.raw_captures} {
  %snapshot = simulation.ref.load %src : !simulation.ref<!l16> -> !l16
  %start = arith.constant 0 : i32
  cf.br ^head(%start : i32)
^head(%i: i32):
  %limit = arith.constant 16 : i32
  %more = arith.cmpi slt, %i, %limit : i32
  cf.cond_br %more, ^body, ^exit
^body:
  %index = arith.extsi %i : i32 to i65
  %last = arith.constant 15 : i65
  %reverse = arith.subi %last, %index : i65
  %out = simulation.ref.dyn_extract %dst from %index : (!simulation.ref<!l16>, i65) -> !simulation.ref<!l1>
  %value = simulation.logic.dyn_extract %snapshot from %reverse : (!l16, i65) -> !l1
  simulation.ref.store %value to %out : !l1, !simulation.ref<!l1>
  %one = arith.constant 1 : i32
  %next = arith.addi %i, %one : i32
  cf.br ^head(%next : i32)
^exit:
  simulation.return
}
simulation.func @word_integer_value(%ctx: !simulation.context {simulation.capture_kind = 0 : i32}, %src: !simulation.ref<i16> {simulation.capture_kind = 3 : i32, simulation.descriptor_id = 24 : i64}, %dst: !simulation.ref<i16> {simulation.capture_kind = 3 : i32, simulation.descriptor_id = 26 : i64}) attributes {entry_kind = 8 : i32, code_unit_id = 19 : i64, schedule.eval.raw_captures} {
  %snapshot = simulation.ref.load %src : !simulation.ref<i16> -> i16
  %array = simulation.packed.unflatten %snapshot : (i16) -> !simulation.packed_array<15 : 0 x i1>
  %start = arith.constant 0 : i32
  cf.br ^head(%start : i32)
^head(%i: i32):
  %limit = arith.constant 16 : i32
  %more = arith.cmpi slt, %i, %limit : i32
  cf.cond_br %more, ^body, ^exit
^body:
  %index = arith.extsi %i : i32 to i65
  %last = arith.constant 15 : i65
  %reverse = arith.subi %last, %index : i65
  %out = simulation.ref.dyn_extract %dst from %index : (!simulation.ref<i16>, i65) -> !simulation.ref<i1>
  %value = simulation.array.extract_dynamic %array[%reverse] : (!simulation.packed_array<15 : 0 x i1>, i65) -> i1
  simulation.ref.store %value to %out : i1, !simulation.ref<i1>
  %one = arith.constant 1 : i32
  %next = arith.addi %i, %one : i32
  cf.br ^head(%next : i32)
^exit:
  simulation.return
}
simulation.func @word_in_place(%ctx: !simulation.context {simulation.capture_kind = 0 : i32}, %src: !simulation.ref<!l16> {simulation.capture_kind = 3 : i32, simulation.descriptor_id = 27 : i64}, %dst: !simulation.ref<!l16> {simulation.capture_kind = 3 : i32, simulation.descriptor_id = 29 : i64}) attributes {entry_kind = 8 : i32, code_unit_id = 21 : i64, schedule.eval.raw_captures} {
  %start = arith.constant 0 : i32
  cf.br ^head(%start : i32)
^head(%i: i32):
  %limit = arith.constant 16 : i32
  %more = arith.cmpi slt, %i, %limit : i32
  cf.cond_br %more, ^body, ^exit
^body:
  %index = arith.extsi %i : i32 to i65
  %out = simulation.ref.dyn_extract %dst from %index : (!simulation.ref<!l16>, i65) -> !simulation.ref<!l1>
  %old = simulation.ref.load %out : !simulation.ref<!l1> -> !l1
  %value = simulation.logic.unary bit_not %old : (!l1) -> !l1
  simulation.ref.store %value to %out : !l1, !simulation.ref<!l1>
  %one = arith.constant 1 : i32
  %next = arith.addi %i, %one : i32
  cf.br ^head(%next : i32)
^exit:
  simulation.return
}
simulation.func @word_ordered_writes(%ctx: !simulation.context {simulation.capture_kind = 0 : i32}, %src: !simulation.ref<!l16> {simulation.capture_kind = 3 : i32, simulation.descriptor_id = 30 : i64}, %dst: !simulation.ref<!l16> {simulation.capture_kind = 3 : i32, simulation.descriptor_id = 32 : i64}) attributes {entry_kind = 8 : i32, code_unit_id = 23 : i64, schedule.eval.raw_captures} {
  %start = arith.constant 0 : i32
  cf.br ^head(%start : i32)
^head(%i: i32):
  %limit = arith.constant 16 : i32
  %more = arith.cmpi slt, %i, %limit : i32
  cf.cond_br %more, ^body, ^exit
^body:
  %index = arith.extsi %i : i32 to i65
  %out = simulation.ref.dyn_extract %dst from %index : (!simulation.ref<!l16>, i65) -> !simulation.ref<!l1>
  %in = simulation.ref.dyn_extract %src from %index : (!simulation.ref<!l16>, i65) -> !simulation.ref<!l1>
  %value = simulation.ref.load %in : !simulation.ref<!l1> -> !l1
  simulation.ref.store %value to %out : !l1, !simulation.ref<!l1>
  %inverse = simulation.logic.unary bit_not %value : (!l1) -> !l1
  simulation.ref.store %inverse to %out : !l1, !simulation.ref<!l1>
  %one = arith.constant 1 : i32
  %next = arith.addi %i, %one : i32
  cf.br ^head(%next : i32)
^exit:
  simulation.return
}
}
}
