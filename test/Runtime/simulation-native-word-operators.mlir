// RUN: obelisk-opt %s --schedule-optimize-native-words -o %t.words
// RUN: FileCheck %s --check-prefix=WORDS < %t.words
// RUN: obelisk-opt %t.words --convert-schedule-word-regions --convert-obelisk-sim-processes-to-llvm-coroutines | obelisk-translate --mlir-to-llvmir | %llvm_dist/bin/opt -passes='coro-early,coro-split<reuse-storage>,coro-cleanup,default<O0>' | %llc -filetype=obj -relocation-model=pic -o %t.o
// RUN: %target_cxx %t.o %runtime_link_flags -o %t.exe
// RUN: %t.exe | FileCheck %s --check-prefix=RESULT --implicit-check-not=' 0'

// Direct MLIR inputs. Native -O0 keeps constant folding from hiding codegen errors.
// Expected value/unknown planes encode the LRM truth tables explicitly.
// The 64 operator lanes enumerate all triples of 0, 1, X and Z.
// Expected planes are independent of the scalar implementation.
// WORDS-COUNT-55: schedule.word.region 64
// RESULT: unary_plus 1
// RESULT: unary_negate 1
// RESULT: unary_bit_not 1
// RESULT: unary_logical_not 1
// RESULT: binary_add 1
// RESULT: binary_sub 1
// RESULT: binary_mul 1
// RESULT: binary_udiv 1
// RESULT: binary_sdiv 1
// RESULT: binary_umod 1
// RESULT: binary_smod 1
// RESULT: binary_and 1
// RESULT: binary_or 1
// RESULT: binary_xor 1
// RESULT: binary_xnor 1
// RESULT: logical_and 1
// RESULT: logical_or 1
// RESULT: reduction_and 1
// RESULT: reduction_or 1
// RESULT: reduction_xor 1
// RESULT: reduction_nand 1
// RESULT: reduction_nor 1
// RESULT: reduction_xnor 1
// RESULT: compare_eq 1
// RESULT: compare_ne 1
// RESULT: compare_case_eq 1
// RESULT: compare_case_ne 1
// RESULT: compare_ult 1
// RESULT: compare_ule 1
// RESULT: compare_ugt 1
// RESULT: compare_uge 1
// RESULT: compare_slt 1
// RESULT: compare_sle 1
// RESULT: compare_sgt 1
// RESULT: compare_sge 1
// RESULT: compare_wild_eq 1
// RESULT: compare_wild_ne 1
// RESULT: compare_casez_eq 1
// RESULT: compare_casexz_eq 1
// RESULT: shift_left 1
// RESULT: shift_right 1
// RESULT: shift_right_arith 1
// RESULT: power 1
// RESULT: mux 1
// RESULT: is_true 1
// RESULT: to_bits 1
// RESULT: from_bits 1
// RESULT: integer_andi 1
// RESULT: integer_ori 1
// RESULT: integer_xori 1
// RESULT: integer_addi 1
// RESULT: integer_subi 1
// RESULT: integer_muli 1
// RESULT: integer_select 1
// RESULT: wide_constant_shift 1

!l1 = !simulation.logic<1>
!l64 = !simulation.logic<64>
module attributes {llvm.data_layout = "e-m:e-p:64:64-i64:64-n8:16:32:64-S128", llvm.target_triple = "x86_64-unknown-linux-gnu"} {
  simulation.design @operators attributes {schedule.nba.transient_observable = array<i64>, schedule.nba.private_influence = array<i64: 3,4,5,6>, compute_graph = #schedule.graph<version = 1, vpi = off, workers = 1, nodes = [], edges = [], regions = [#schedule.region<kind = active, groups = []>, #schedule.region<kind = nba, groups = []>, #schedule.region<kind = observed, groups = []>, #schedule.region<kind = reactive, groups = []>, #schedule.region<kind = postponed, groups = []>]>} {
    simulation.scope.decl 0
    simulation.code_unit.decl 1 in 0 root_initializer hierarchy "root"
    simulation.storage.decl 0 in 0 : !l64 design
    simulation.storage.decl 1 in 0 : !l64 design
    simulation.storage.decl 2 in 0 : !l64 design
    simulation.storage.decl 3 in 0 : !l64 design
    simulation.storage.decl 4 in 0 : !l64 design
    simulation.storage.decl 5 in 0 : i64 design
    simulation.storage.decl 6 in 0 : i64 design
    simulation.code_unit.decl 3 in 0 function hierarchy "word_unary_plus"
    simulation.code_unit.decl 5 in 0 function hierarchy "word_unary_negate"
    simulation.code_unit.decl 7 in 0 function hierarchy "word_unary_bit_not"
    simulation.code_unit.decl 9 in 0 function hierarchy "word_unary_logical_not"
    simulation.code_unit.decl 11 in 0 function hierarchy "word_binary_add"
    simulation.code_unit.decl 13 in 0 function hierarchy "word_binary_sub"
    simulation.code_unit.decl 15 in 0 function hierarchy "word_binary_mul"
    simulation.code_unit.decl 17 in 0 function hierarchy "word_binary_udiv"
    simulation.code_unit.decl 19 in 0 function hierarchy "word_binary_sdiv"
    simulation.code_unit.decl 21 in 0 function hierarchy "word_binary_umod"
    simulation.code_unit.decl 23 in 0 function hierarchy "word_binary_smod"
    simulation.code_unit.decl 25 in 0 function hierarchy "word_binary_and"
    simulation.code_unit.decl 27 in 0 function hierarchy "word_binary_or"
    simulation.code_unit.decl 29 in 0 function hierarchy "word_binary_xor"
    simulation.code_unit.decl 31 in 0 function hierarchy "word_binary_xnor"
    simulation.code_unit.decl 33 in 0 function hierarchy "word_logical_and"
    simulation.code_unit.decl 35 in 0 function hierarchy "word_logical_or"
    simulation.code_unit.decl 37 in 0 function hierarchy "word_reduction_and"
    simulation.code_unit.decl 39 in 0 function hierarchy "word_reduction_or"
    simulation.code_unit.decl 41 in 0 function hierarchy "word_reduction_xor"
    simulation.code_unit.decl 43 in 0 function hierarchy "word_reduction_nand"
    simulation.code_unit.decl 45 in 0 function hierarchy "word_reduction_nor"
    simulation.code_unit.decl 47 in 0 function hierarchy "word_reduction_xnor"
    simulation.code_unit.decl 49 in 0 function hierarchy "word_compare_eq"
    simulation.code_unit.decl 51 in 0 function hierarchy "word_compare_ne"
    simulation.code_unit.decl 53 in 0 function hierarchy "word_compare_case_eq"
    simulation.code_unit.decl 55 in 0 function hierarchy "word_compare_case_ne"
    simulation.code_unit.decl 57 in 0 function hierarchy "word_compare_ult"
    simulation.code_unit.decl 59 in 0 function hierarchy "word_compare_ule"
    simulation.code_unit.decl 61 in 0 function hierarchy "word_compare_ugt"
    simulation.code_unit.decl 63 in 0 function hierarchy "word_compare_uge"
    simulation.code_unit.decl 65 in 0 function hierarchy "word_compare_slt"
    simulation.code_unit.decl 67 in 0 function hierarchy "word_compare_sle"
    simulation.code_unit.decl 69 in 0 function hierarchy "word_compare_sgt"
    simulation.code_unit.decl 71 in 0 function hierarchy "word_compare_sge"
    simulation.code_unit.decl 73 in 0 function hierarchy "word_compare_wild_eq"
    simulation.code_unit.decl 75 in 0 function hierarchy "word_compare_wild_ne"
    simulation.code_unit.decl 77 in 0 function hierarchy "word_compare_casez_eq"
    simulation.code_unit.decl 79 in 0 function hierarchy "word_compare_casexz_eq"
    simulation.code_unit.decl 81 in 0 function hierarchy "word_shift_left"
    simulation.code_unit.decl 83 in 0 function hierarchy "word_shift_right"
    simulation.code_unit.decl 85 in 0 function hierarchy "word_shift_right_arith"
    simulation.code_unit.decl 87 in 0 function hierarchy "word_power"
    simulation.code_unit.decl 89 in 0 function hierarchy "word_mux"
    simulation.code_unit.decl 91 in 0 function hierarchy "word_is_true"
    simulation.code_unit.decl 93 in 0 function hierarchy "word_to_bits"
    simulation.code_unit.decl 95 in 0 function hierarchy "word_from_bits"
    simulation.code_unit.decl 97 in 0 function hierarchy "word_integer_andi"
    simulation.code_unit.decl 99 in 0 function hierarchy "word_integer_ori"
    simulation.code_unit.decl 101 in 0 function hierarchy "word_integer_xori"
    simulation.code_unit.decl 103 in 0 function hierarchy "word_integer_addi"
    simulation.code_unit.decl 105 in 0 function hierarchy "word_integer_subi"
    simulation.code_unit.decl 107 in 0 function hierarchy "word_integer_muli"
    simulation.code_unit.decl 109 in 0 function hierarchy "word_integer_select"
    simulation.code_unit.decl 111 in 0 function hierarchy "word_wide_constant_shift"
    simulation.func @root(%ctx: !simulation.context {simulation.capture_kind = 0 : i32}) attributes {entry_kind = 0 : i32, code_unit_id = 1 : i64} {
      %r0 = simulation.context.storage %ctx[0] : !simulation.ref<!l64>
      %r1 = simulation.context.storage %ctx[1] : !simulation.ref<!l64>
      %r2 = simulation.context.storage %ctx[2] : !simulation.ref<!l64>
      %r3 = simulation.context.storage %ctx[3] : !simulation.ref<!l64>
      %r4 = simulation.context.storage %ctx[4] : !simulation.ref<!l64>
      %r5 = simulation.context.storage %ctx[5] : !simulation.ref<i64>
      %r6 = simulation.context.storage %ctx[6] : !simulation.ref<i64>
      %input0 = simulation.logic.constant 12297829382473034410 : i64, 14757395258967641292 : i64 : !l64
      simulation.ref.store %input0 to %r0 : !l64, !simulation.ref<!l64>
      %input1 = simulation.logic.constant 17361641481138401520 : i64, 18374966859414961920 : i64 : !l64
      simulation.ref.store %input1 to %r1 : !l64, !simulation.ref<!l64>
      %input2 = simulation.logic.constant 18446462603027742720 : i64, 18446744069414584320 : i64 : !l64
      simulation.ref.store %input2 to %r2 : !l64, !simulation.ref<!l64>
      %true = arith.constant true
      simulation.call @word_unary_plus(%ctx, %r0, %r1, %r2, %r4) : (!simulation.context, !simulation.ref<!l64>, !simulation.ref<!l64>, !simulation.ref<!l64>, !simulation.ref<!l64>) -> ()
      %expected0 = simulation.logic.constant 12297829382473034410 : i64, 14757395258967641292 : i64 : !l64
      %v0_1 = simulation.ref.load %r4 : !simulation.ref<!l64> -> !l64
      %ok0_1 = simulation.logic.compare case_eq %v0_1, %expected0 : (!l64, !l64) -> i1
      %fmt0 = simulation.bytes.constant "unary_plus %0d"
      %ch0 = arith.constant 1 : i32
      simulation.display %ctx to %ch0(%fmt0, %ok0_1) newline = true radix = <decimal> flags = [0,0] : !simulation.bytes, i1
      simulation.call @word_unary_negate(%ctx, %r0, %r1, %r2, %r4) : (!simulation.context, !simulation.ref<!l64>, !simulation.ref<!l64>, !simulation.ref<!l64>, !simulation.ref<!l64>) -> ()
      %expected1 = simulation.logic.constant 2459565876494606882 : i64, 14757395258967641292 : i64 : !l64
      %v1_1 = simulation.ref.load %r4 : !simulation.ref<!l64> -> !l64
      %ok1_1 = simulation.logic.compare case_eq %v1_1, %expected1 : (!l64, !l64) -> i1
      %fmt1 = simulation.bytes.constant "unary_negate %0d"
      %ch1 = arith.constant 1 : i32
      simulation.display %ctx to %ch1(%fmt1, %ok1_1) newline = true radix = <decimal> flags = [0,0] : !simulation.bytes, i1
      simulation.call @word_unary_bit_not(%ctx, %r0, %r1, %r2, %r4) : (!simulation.context, !simulation.ref<!l64>, !simulation.ref<!l64>, !simulation.ref<!l64>, !simulation.ref<!l64>) -> ()
      %expected2 = simulation.logic.constant 1229782938247303441 : i64, 14757395258967641292 : i64 : !l64
      %v2_1 = simulation.ref.load %r4 : !simulation.ref<!l64> -> !l64
      %ok2_1 = simulation.logic.compare case_eq %v2_1, %expected2 : (!l64, !l64) -> i1
      %fmt2 = simulation.bytes.constant "unary_bit_not %0d"
      %ch2 = arith.constant 1 : i32
      simulation.display %ctx to %ch2(%fmt2, %ok2_1) newline = true radix = <decimal> flags = [0,0] : !simulation.bytes, i1
      simulation.call @word_unary_logical_not(%ctx, %r0, %r1, %r2, %r4) : (!simulation.context, !simulation.ref<!l64>, !simulation.ref<!l64>, !simulation.ref<!l64>, !simulation.ref<!l64>) -> ()
      %expected3 = simulation.logic.constant 1229782938247303441 : i64, 14757395258967641292 : i64 : !l64
      %v3_1 = simulation.ref.load %r4 : !simulation.ref<!l64> -> !l64
      %ok3_1 = simulation.logic.compare case_eq %v3_1, %expected3 : (!l64, !l64) -> i1
      %fmt3 = simulation.bytes.constant "unary_logical_not %0d"
      %ch3 = arith.constant 1 : i32
      simulation.display %ctx to %ch3(%fmt3, %ok3_1) newline = true radix = <decimal> flags = [0,0] : !simulation.bytes, i1
      simulation.call @word_binary_add(%ctx, %r0, %r1, %r2, %r4) : (!simulation.context, !simulation.ref<!l64>, !simulation.ref<!l64>, !simulation.ref<!l64>, !simulation.ref<!l64>) -> ()
      %expected4 = simulation.logic.constant 5066626891382802 : i64, 18432388630850633676 : i64 : !l64
      %v4_1 = simulation.ref.load %r4 : !simulation.ref<!l64> -> !l64
      %ok4_1 = simulation.logic.compare case_eq %v4_1, %expected4 : (!l64, !l64) -> i1
      %fmt4 = simulation.bytes.constant "binary_add %0d"
      %ch4 = arith.constant 1 : i32
      simulation.display %ctx to %ch4(%fmt4, %ok4_1) newline = true radix = <decimal> flags = [0,0] : !simulation.bytes, i1
      simulation.call @word_binary_sub(%ctx, %r0, %r1, %r2, %r4) : (!simulation.context, !simulation.ref<!l64>, !simulation.ref<!l64>, !simulation.ref<!l64>, !simulation.ref<!l64>) -> ()
      %expected5 = simulation.logic.constant 5066626891382802 : i64, 18432388630850633676 : i64 : !l64
      %v5_1 = simulation.ref.load %r4 : !simulation.ref<!l64> -> !l64
      %ok5_1 = simulation.logic.compare case_eq %v5_1, %expected5 : (!l64, !l64) -> i1
      %fmt5 = simulation.bytes.constant "binary_sub %0d"
      %ch5 = arith.constant 1 : i32
      simulation.display %ctx to %ch5(%fmt5, %ok5_1) newline = true radix = <decimal> flags = [0,0] : !simulation.bytes, i1
      simulation.call @word_binary_mul(%ctx, %r0, %r1, %r2, %r4) : (!simulation.context, !simulation.ref<!l64>, !simulation.ref<!l64>, !simulation.ref<!l64>, !simulation.ref<!l64>) -> ()
      %expected6 = simulation.logic.constant 9007336695791648 : i64, 18432388630850633676 : i64 : !l64
      %v6_1 = simulation.ref.load %r4 : !simulation.ref<!l64> -> !l64
      %ok6_1 = simulation.logic.compare case_eq %v6_1, %expected6 : (!l64, !l64) -> i1
      %fmt6 = simulation.bytes.constant "binary_mul %0d"
      %ch6 = arith.constant 1 : i32
      simulation.display %ctx to %ch6(%fmt6, %ok6_1) newline = true radix = <decimal> flags = [0,0] : !simulation.bytes, i1
      simulation.call @word_binary_udiv(%ctx, %r0, %r1, %r2, %r4) : (!simulation.context, !simulation.ref<!l64>, !simulation.ref<!l64>, !simulation.ref<!l64>, !simulation.ref<!l64>) -> ()
      %expected7 = simulation.logic.constant 9007336695791648 : i64, 18433233068665864143 : i64 : !l64
      %v7_1 = simulation.ref.load %r4 : !simulation.ref<!l64> -> !l64
      %ok7_1 = simulation.logic.compare case_eq %v7_1, %expected7 : (!l64, !l64) -> i1
      %fmt7 = simulation.bytes.constant "binary_udiv %0d"
      %ch7 = arith.constant 1 : i32
      simulation.display %ctx to %ch7(%fmt7, %ok7_1) newline = true radix = <decimal> flags = [0,0] : !simulation.bytes, i1
      simulation.call @word_binary_sdiv(%ctx, %r0, %r1, %r2, %r4) : (!simulation.context, !simulation.ref<!l64>, !simulation.ref<!l64>, !simulation.ref<!l64>, !simulation.ref<!l64>) -> ()
      %expected8 = simulation.logic.constant 9007336695791648 : i64, 18433233068665864143 : i64 : !l64
      %v8_1 = simulation.ref.load %r4 : !simulation.ref<!l64> -> !l64
      %ok8_1 = simulation.logic.compare case_eq %v8_1, %expected8 : (!l64, !l64) -> i1
      %fmt8 = simulation.bytes.constant "binary_sdiv %0d"
      %ch8 = arith.constant 1 : i32
      simulation.display %ctx to %ch8(%fmt8, %ok8_1) newline = true radix = <decimal> flags = [0,0] : !simulation.bytes, i1
      simulation.call @word_binary_umod(%ctx, %r0, %r1, %r2, %r4) : (!simulation.context, !simulation.ref<!l64>, !simulation.ref<!l64>, !simulation.ref<!l64>, !simulation.ref<!l64>) -> ()
      %expected9 = simulation.logic.constant 0 : i64, 18433233068665864143 : i64 : !l64
      %v9_1 = simulation.ref.load %r4 : !simulation.ref<!l64> -> !l64
      %ok9_1 = simulation.logic.compare case_eq %v9_1, %expected9 : (!l64, !l64) -> i1
      %fmt9 = simulation.bytes.constant "binary_umod %0d"
      %ch9 = arith.constant 1 : i32
      simulation.display %ctx to %ch9(%fmt9, %ok9_1) newline = true radix = <decimal> flags = [0,0] : !simulation.bytes, i1
      simulation.call @word_binary_smod(%ctx, %r0, %r1, %r2, %r4) : (!simulation.context, !simulation.ref<!l64>, !simulation.ref<!l64>, !simulation.ref<!l64>, !simulation.ref<!l64>) -> ()
      %expected10 = simulation.logic.constant 0 : i64, 18433233068665864143 : i64 : !l64
      %v10_1 = simulation.ref.load %r4 : !simulation.ref<!l64> -> !l64
      %ok10_1 = simulation.logic.compare case_eq %v10_1, %expected10 : (!l64, !l64) -> i1
      %fmt10 = simulation.bytes.constant "binary_smod %0d"
      %ch10 = arith.constant 1 : i32
      simulation.display %ctx to %ch10(%fmt10, %ok10_1) newline = true radix = <decimal> flags = [0,0] : !simulation.bytes, i1
      simulation.call @word_binary_and(%ctx, %r0, %r1, %r2, %r4) : (!simulation.context, !simulation.ref<!l64>, !simulation.ref<!l64>, !simulation.ref<!l64>, !simulation.ref<!l64>) -> ()
      %expected11 = simulation.logic.constant 9007336695791648 : i64, 17204013088962047680 : i64 : !l64
      %v11_1 = simulation.ref.load %r4 : !simulation.ref<!l64> -> !l64
      %ok11_1 = simulation.logic.compare case_eq %v11_1, %expected11 : (!l64, !l64) -> i1
      %fmt11 = simulation.bytes.constant "binary_and %0d"
      %ch11 = arith.constant 1 : i32
      simulation.display %ctx to %ch11(%fmt11, %ok11_1) newline = true radix = <decimal> flags = [0,0] : !simulation.bytes, i1
      simulation.call @word_binary_or(%ctx, %r0, %r1, %r2, %r4) : (!simulation.context, !simulation.ref<!l64>, !simulation.ref<!l64>, !simulation.ref<!l64>, !simulation.ref<!l64>) -> ()
      %expected12 = simulation.logic.constant 2518113565017252594 : i64, 15928349029420555532 : i64 : !l64
      %v12_1 = simulation.ref.load %r4 : !simulation.ref<!l64> -> !l64
      %ok12_1 = simulation.logic.compare case_eq %v12_1, %expected12 : (!l64, !l64) -> i1
      %fmt12 = simulation.bytes.constant "binary_or %0d"
      %ch12 = arith.constant 1 : i32
      simulation.display %ctx to %ch12(%fmt12, %ok12_1) newline = true radix = <decimal> flags = [0,0] : !simulation.bytes, i1
      simulation.call @word_binary_xor(%ctx, %r0, %r1, %r2, %r4) : (!simulation.context, !simulation.ref<!l64>, !simulation.ref<!l64>, !simulation.ref<!l64>, !simulation.ref<!l64>) -> ()
      %expected13 = simulation.logic.constant 5066626891382802 : i64, 18432388630850633676 : i64 : !l64
      %v13_1 = simulation.ref.load %r4 : !simulation.ref<!l64> -> !l64
      %ok13_1 = simulation.logic.compare case_eq %v13_1, %expected13 : (!l64, !l64) -> i1
      %fmt13 = simulation.bytes.constant "binary_xor %0d"
      %ch13 = arith.constant 1 : i32
      simulation.display %ctx to %ch13(%fmt13, %ok13_1) newline = true radix = <decimal> flags = [0,0] : !simulation.bytes, i1
      simulation.call @word_binary_xnor(%ctx, %r0, %r1, %r2, %r4) : (!simulation.context, !simulation.ref<!l64>, !simulation.ref<!l64>, !simulation.ref<!l64>, !simulation.ref<!l64>) -> ()
      %expected14 = simulation.logic.constant 9288815967535137 : i64, 18432388630850633676 : i64 : !l64
      %v14_1 = simulation.ref.load %r4 : !simulation.ref<!l64> -> !l64
      %ok14_1 = simulation.logic.compare case_eq %v14_1, %expected14 : (!l64, !l64) -> i1
      %fmt14 = simulation.bytes.constant "binary_xnor %0d"
      %ch14 = arith.constant 1 : i32
      simulation.display %ctx to %ch14(%fmt14, %ok14_1) newline = true radix = <decimal> flags = [0,0] : !simulation.bytes, i1
      simulation.call @word_logical_and(%ctx, %r0, %r1, %r2, %r4) : (!simulation.context, !simulation.ref<!l64>, !simulation.ref<!l64>, !simulation.ref<!l64>, !simulation.ref<!l64>) -> ()
      %expected15 = simulation.logic.constant 9007336695791648 : i64, 17204013088962047680 : i64 : !l64
      %v15_1 = simulation.ref.load %r4 : !simulation.ref<!l64> -> !l64
      %ok15_1 = simulation.logic.compare case_eq %v15_1, %expected15 : (!l64, !l64) -> i1
      %fmt15 = simulation.bytes.constant "logical_and %0d"
      %ch15 = arith.constant 1 : i32
      simulation.display %ctx to %ch15(%fmt15, %ok15_1) newline = true radix = <decimal> flags = [0,0] : !simulation.bytes, i1
      simulation.call @word_logical_or(%ctx, %r0, %r1, %r2, %r4) : (!simulation.context, !simulation.ref<!l64>, !simulation.ref<!l64>, !simulation.ref<!l64>, !simulation.ref<!l64>) -> ()
      %expected16 = simulation.logic.constant 2518113565017252594 : i64, 15928349029420555532 : i64 : !l64
      %v16_1 = simulation.ref.load %r4 : !simulation.ref<!l64> -> !l64
      %ok16_1 = simulation.logic.compare case_eq %v16_1, %expected16 : (!l64, !l64) -> i1
      %fmt16 = simulation.bytes.constant "logical_or %0d"
      %ch16 = arith.constant 1 : i32
      simulation.display %ctx to %ch16(%fmt16, %ok16_1) newline = true radix = <decimal> flags = [0,0] : !simulation.bytes, i1
      simulation.call @word_reduction_and(%ctx, %r0, %r1, %r2, %r4) : (!simulation.context, !simulation.ref<!l64>, !simulation.ref<!l64>, !simulation.ref<!l64>, !simulation.ref<!l64>) -> ()
      %expected17 = simulation.logic.constant 2459565876494606882 : i64, 14757395258967641292 : i64 : !l64
      %v17_1 = simulation.ref.load %r4 : !simulation.ref<!l64> -> !l64
      %ok17_1 = simulation.logic.compare case_eq %v17_1, %expected17 : (!l64, !l64) -> i1
      %fmt17 = simulation.bytes.constant "reduction_and %0d"
      %ch17 = arith.constant 1 : i32
      simulation.display %ctx to %ch17(%fmt17, %ok17_1) newline = true radix = <decimal> flags = [0,0] : !simulation.bytes, i1
      simulation.call @word_reduction_or(%ctx, %r0, %r1, %r2, %r4) : (!simulation.context, !simulation.ref<!l64>, !simulation.ref<!l64>, !simulation.ref<!l64>, !simulation.ref<!l64>) -> ()
      %expected18 = simulation.logic.constant 2459565876494606882 : i64, 14757395258967641292 : i64 : !l64
      %v18_1 = simulation.ref.load %r4 : !simulation.ref<!l64> -> !l64
      %ok18_1 = simulation.logic.compare case_eq %v18_1, %expected18 : (!l64, !l64) -> i1
      %fmt18 = simulation.bytes.constant "reduction_or %0d"
      %ch18 = arith.constant 1 : i32
      simulation.display %ctx to %ch18(%fmt18, %ok18_1) newline = true radix = <decimal> flags = [0,0] : !simulation.bytes, i1
      simulation.call @word_reduction_xor(%ctx, %r0, %r1, %r2, %r4) : (!simulation.context, !simulation.ref<!l64>, !simulation.ref<!l64>, !simulation.ref<!l64>, !simulation.ref<!l64>) -> ()
      %expected19 = simulation.logic.constant 2459565876494606882 : i64, 14757395258967641292 : i64 : !l64
      %v19_1 = simulation.ref.load %r4 : !simulation.ref<!l64> -> !l64
      %ok19_1 = simulation.logic.compare case_eq %v19_1, %expected19 : (!l64, !l64) -> i1
      %fmt19 = simulation.bytes.constant "reduction_xor %0d"
      %ch19 = arith.constant 1 : i32
      simulation.display %ctx to %ch19(%fmt19, %ok19_1) newline = true radix = <decimal> flags = [0,0] : !simulation.bytes, i1
      simulation.call @word_reduction_nand(%ctx, %r0, %r1, %r2, %r4) : (!simulation.context, !simulation.ref<!l64>, !simulation.ref<!l64>, !simulation.ref<!l64>, !simulation.ref<!l64>) -> ()
      %expected20 = simulation.logic.constant 1229782938247303441 : i64, 14757395258967641292 : i64 : !l64
      %v20_1 = simulation.ref.load %r4 : !simulation.ref<!l64> -> !l64
      %ok20_1 = simulation.logic.compare case_eq %v20_1, %expected20 : (!l64, !l64) -> i1
      %fmt20 = simulation.bytes.constant "reduction_nand %0d"
      %ch20 = arith.constant 1 : i32
      simulation.display %ctx to %ch20(%fmt20, %ok20_1) newline = true radix = <decimal> flags = [0,0] : !simulation.bytes, i1
      simulation.call @word_reduction_nor(%ctx, %r0, %r1, %r2, %r4) : (!simulation.context, !simulation.ref<!l64>, !simulation.ref<!l64>, !simulation.ref<!l64>, !simulation.ref<!l64>) -> ()
      %expected21 = simulation.logic.constant 1229782938247303441 : i64, 14757395258967641292 : i64 : !l64
      %v21_1 = simulation.ref.load %r4 : !simulation.ref<!l64> -> !l64
      %ok21_1 = simulation.logic.compare case_eq %v21_1, %expected21 : (!l64, !l64) -> i1
      %fmt21 = simulation.bytes.constant "reduction_nor %0d"
      %ch21 = arith.constant 1 : i32
      simulation.display %ctx to %ch21(%fmt21, %ok21_1) newline = true radix = <decimal> flags = [0,0] : !simulation.bytes, i1
      simulation.call @word_reduction_xnor(%ctx, %r0, %r1, %r2, %r4) : (!simulation.context, !simulation.ref<!l64>, !simulation.ref<!l64>, !simulation.ref<!l64>, !simulation.ref<!l64>) -> ()
      %expected22 = simulation.logic.constant 1229782938247303441 : i64, 14757395258967641292 : i64 : !l64
      %v22_1 = simulation.ref.load %r4 : !simulation.ref<!l64> -> !l64
      %ok22_1 = simulation.logic.compare case_eq %v22_1, %expected22 : (!l64, !l64) -> i1
      %fmt22 = simulation.bytes.constant "reduction_xnor %0d"
      %ch22 = arith.constant 1 : i32
      simulation.display %ctx to %ch22(%fmt22, %ok22_1) newline = true radix = <decimal> flags = [0,0] : !simulation.bytes, i1
      simulation.call @word_compare_eq(%ctx, %r0, %r1, %r2, %r4) : (!simulation.context, !simulation.ref<!l64>, !simulation.ref<!l64>, !simulation.ref<!l64>, !simulation.ref<!l64>) -> ()
      %expected23 = simulation.logic.constant 9288815967535137 : i64, 18432388630850633676 : i64 : !l64
      %v23_1 = simulation.ref.load %r4 : !simulation.ref<!l64> -> !l64
      %ok23_1 = simulation.logic.compare case_eq %v23_1, %expected23 : (!l64, !l64) -> i1
      %fmt23 = simulation.bytes.constant "compare_eq %0d"
      %ch23 = arith.constant 1 : i32
      simulation.display %ctx to %ch23(%fmt23, %ok23_1) newline = true radix = <decimal> flags = [0,0] : !simulation.bytes, i1
      simulation.call @word_compare_ne(%ctx, %r0, %r1, %r2, %r4) : (!simulation.context, !simulation.ref<!l64>, !simulation.ref<!l64>, !simulation.ref<!l64>, !simulation.ref<!l64>) -> ()
      %expected24 = simulation.logic.constant 5066626891382802 : i64, 18432388630850633676 : i64 : !l64
      %v24_1 = simulation.ref.load %r4 : !simulation.ref<!l64> -> !l64
      %ok24_1 = simulation.logic.compare case_eq %v24_1, %expected24 : (!l64, !l64) -> i1
      %fmt24 = simulation.bytes.constant "compare_ne %0d"
      %ch24 = arith.constant 1 : i32
      simulation.display %ctx to %ch24(%fmt24, %ok24_1) newline = true radix = <decimal> flags = [0,0] : !simulation.bytes, i1
      simulation.call @word_compare_case_eq(%ctx, %r0, %r1, %r2, %r6) : (!simulation.context, !simulation.ref<!l64>, !simulation.ref<!l64>, !simulation.ref<!l64>, !simulation.ref<i64>) -> ()
      %expected25 = arith.constant 9521036366723515425 : i64
      %v25_1 = simulation.ref.load %r6 : !simulation.ref<i64> -> i64
      %ok25_1 = arith.cmpi eq, %v25_1, %expected25 : i64
      %fmt25 = simulation.bytes.constant "compare_case_eq %0d"
      %ch25 = arith.constant 1 : i32
      simulation.display %ctx to %ch25(%fmt25, %ok25_1) newline = true radix = <decimal> flags = [0,0] : !simulation.bytes, i1
      simulation.call @word_compare_case_ne(%ctx, %r0, %r1, %r2, %r6) : (!simulation.context, !simulation.ref<!l64>, !simulation.ref<!l64>, !simulation.ref<!l64>, !simulation.ref<i64>) -> ()
      %expected26 = arith.constant 8925707706986036190 : i64
      %v26_1 = simulation.ref.load %r6 : !simulation.ref<i64> -> i64
      %ok26_1 = arith.cmpi eq, %v26_1, %expected26 : i64
      %fmt26 = simulation.bytes.constant "compare_case_ne %0d"
      %ch26 = arith.constant 1 : i32
      simulation.display %ctx to %ch26(%fmt26, %ok26_1) newline = true radix = <decimal> flags = [0,0] : !simulation.bytes, i1
      simulation.call @word_compare_ult(%ctx, %r0, %r1, %r2, %r4) : (!simulation.context, !simulation.ref<!l64>, !simulation.ref<!l64>, !simulation.ref<!l64>, !simulation.ref<!l64>) -> ()
      %expected27 = simulation.logic.constant 4503668347895824 : i64, 18432388630850633676 : i64 : !l64
      %v27_1 = simulation.ref.load %r4 : !simulation.ref<!l64> -> !l64
      %ok27_1 = simulation.logic.compare case_eq %v27_1, %expected27 : (!l64, !l64) -> i1
      %fmt27 = simulation.bytes.constant "compare_ult %0d"
      %ch27 = arith.constant 1 : i32
      simulation.display %ctx to %ch27(%fmt27, %ok27_1) newline = true radix = <decimal> flags = [0,0] : !simulation.bytes, i1
      simulation.call @word_compare_ule(%ctx, %r0, %r1, %r2, %r4) : (!simulation.context, !simulation.ref<!l64>, !simulation.ref<!l64>, !simulation.ref<!l64>, !simulation.ref<!l64>) -> ()
      %expected28 = simulation.logic.constant 13792484315430961 : i64, 18432388630850633676 : i64 : !l64
      %v28_1 = simulation.ref.load %r4 : !simulation.ref<!l64> -> !l64
      %ok28_1 = simulation.logic.compare case_eq %v28_1, %expected28 : (!l64, !l64) -> i1
      %fmt28 = simulation.bytes.constant "compare_ule %0d"
      %ch28 = arith.constant 1 : i32
      simulation.display %ctx to %ch28(%fmt28, %ok28_1) newline = true radix = <decimal> flags = [0,0] : !simulation.bytes, i1
      simulation.call @word_compare_ugt(%ctx, %r0, %r1, %r2, %r4) : (!simulation.context, !simulation.ref<!l64>, !simulation.ref<!l64>, !simulation.ref<!l64>, !simulation.ref<!l64>) -> ()
      %expected29 = simulation.logic.constant 562958543486978 : i64, 18432388630850633676 : i64 : !l64
      %v29_1 = simulation.ref.load %r4 : !simulation.ref<!l64> -> !l64
      %ok29_1 = simulation.logic.compare case_eq %v29_1, %expected29 : (!l64, !l64) -> i1
      %fmt29 = simulation.bytes.constant "compare_ugt %0d"
      %ch29 = arith.constant 1 : i32
      simulation.display %ctx to %ch29(%fmt29, %ok29_1) newline = true radix = <decimal> flags = [0,0] : !simulation.bytes, i1
      simulation.call @word_compare_uge(%ctx, %r0, %r1, %r2, %r4) : (!simulation.context, !simulation.ref<!l64>, !simulation.ref<!l64>, !simulation.ref<!l64>, !simulation.ref<!l64>) -> ()
      %expected30 = simulation.logic.constant 9851774511022115 : i64, 18432388630850633676 : i64 : !l64
      %v30_1 = simulation.ref.load %r4 : !simulation.ref<!l64> -> !l64
      %ok30_1 = simulation.logic.compare case_eq %v30_1, %expected30 : (!l64, !l64) -> i1
      %fmt30 = simulation.bytes.constant "compare_uge %0d"
      %ch30 = arith.constant 1 : i32
      simulation.display %ctx to %ch30(%fmt30, %ok30_1) newline = true radix = <decimal> flags = [0,0] : !simulation.bytes, i1
      simulation.call @word_compare_slt(%ctx, %r0, %r1, %r2, %r4) : (!simulation.context, !simulation.ref<!l64>, !simulation.ref<!l64>, !simulation.ref<!l64>, !simulation.ref<!l64>) -> ()
      %expected31 = simulation.logic.constant 562958543486978 : i64, 18432388630850633676 : i64 : !l64
      %v31_1 = simulation.ref.load %r4 : !simulation.ref<!l64> -> !l64
      %ok31_1 = simulation.logic.compare case_eq %v31_1, %expected31 : (!l64, !l64) -> i1
      %fmt31 = simulation.bytes.constant "compare_slt %0d"
      %ch31 = arith.constant 1 : i32
      simulation.display %ctx to %ch31(%fmt31, %ok31_1) newline = true radix = <decimal> flags = [0,0] : !simulation.bytes, i1
      simulation.call @word_compare_sle(%ctx, %r0, %r1, %r2, %r4) : (!simulation.context, !simulation.ref<!l64>, !simulation.ref<!l64>, !simulation.ref<!l64>, !simulation.ref<!l64>) -> ()
      %expected32 = simulation.logic.constant 9851774511022115 : i64, 18432388630850633676 : i64 : !l64
      %v32_1 = simulation.ref.load %r4 : !simulation.ref<!l64> -> !l64
      %ok32_1 = simulation.logic.compare case_eq %v32_1, %expected32 : (!l64, !l64) -> i1
      %fmt32 = simulation.bytes.constant "compare_sle %0d"
      %ch32 = arith.constant 1 : i32
      simulation.display %ctx to %ch32(%fmt32, %ok32_1) newline = true radix = <decimal> flags = [0,0] : !simulation.bytes, i1
      simulation.call @word_compare_sgt(%ctx, %r0, %r1, %r2, %r4) : (!simulation.context, !simulation.ref<!l64>, !simulation.ref<!l64>, !simulation.ref<!l64>, !simulation.ref<!l64>) -> ()
      %expected33 = simulation.logic.constant 4503668347895824 : i64, 18432388630850633676 : i64 : !l64
      %v33_1 = simulation.ref.load %r4 : !simulation.ref<!l64> -> !l64
      %ok33_1 = simulation.logic.compare case_eq %v33_1, %expected33 : (!l64, !l64) -> i1
      %fmt33 = simulation.bytes.constant "compare_sgt %0d"
      %ch33 = arith.constant 1 : i32
      simulation.display %ctx to %ch33(%fmt33, %ok33_1) newline = true radix = <decimal> flags = [0,0] : !simulation.bytes, i1
      simulation.call @word_compare_sge(%ctx, %r0, %r1, %r2, %r4) : (!simulation.context, !simulation.ref<!l64>, !simulation.ref<!l64>, !simulation.ref<!l64>, !simulation.ref<!l64>) -> ()
      %expected34 = simulation.logic.constant 13792484315430961 : i64, 18432388630850633676 : i64 : !l64
      %v34_1 = simulation.ref.load %r4 : !simulation.ref<!l64> -> !l64
      %ok34_1 = simulation.logic.compare case_eq %v34_1, %expected34 : (!l64, !l64) -> i1
      %fmt34 = simulation.bytes.constant "compare_sge %0d"
      %ch34 = arith.constant 1 : i32
      simulation.display %ctx to %ch34(%fmt34, %ok34_1) newline = true radix = <decimal> flags = [0,0] : !simulation.bytes, i1
      simulation.call @word_compare_wild_eq(%ctx, %r0, %r1, %r2, %r4) : (!simulation.context, !simulation.ref<!l64>, !simulation.ref<!l64>, !simulation.ref<!l64>, !simulation.ref<!l64>) -> ()
      %expected35 = simulation.logic.constant 18384255675382497057 : i64, 57421771435671756 : i64 : !l64
      %v35_1 = simulation.ref.load %r4 : !simulation.ref<!l64> -> !l64
      %ok35_1 = simulation.logic.compare case_eq %v35_1, %expected35 : (!l64, !l64) -> i1
      %fmt35 = simulation.bytes.constant "compare_wild_eq %0d"
      %ch35 = arith.constant 1 : i32
      simulation.display %ctx to %ch35(%fmt35, %ok35_1) newline = true radix = <decimal> flags = [0,0] : !simulation.bytes, i1
      simulation.call @word_compare_wild_ne(%ctx, %r0, %r1, %r2, %r4) : (!simulation.context, !simulation.ref<!l64>, !simulation.ref<!l64>, !simulation.ref<!l64>, !simulation.ref<!l64>) -> ()
      %expected36 = simulation.logic.constant 5066626891382802 : i64, 57421771435671756 : i64 : !l64
      %v36_1 = simulation.ref.load %r4 : !simulation.ref<!l64> -> !l64
      %ok36_1 = simulation.logic.compare case_eq %v36_1, %expected36 : (!l64, !l64) -> i1
      %fmt36 = simulation.bytes.constant "compare_wild_ne %0d"
      %ch36 = arith.constant 1 : i32
      simulation.display %ctx to %ch36(%fmt36, %ok36_1) newline = true radix = <decimal> flags = [0,0] : !simulation.bytes, i1
      simulation.call @word_compare_casez_eq(%ctx, %r0, %r1, %r2, %r6) : (!simulation.context, !simulation.ref<!l64>, !simulation.ref<!l64>, !simulation.ref<!l64>, !simulation.ref<i64>) -> ()
      %expected37 = arith.constant 18206360775640612009 : i64
      %v37_1 = simulation.ref.load %r6 : !simulation.ref<i64> -> i64
      %ok37_1 = arith.cmpi eq, %v37_1, %expected37 : i64
      %fmt37 = simulation.bytes.constant "compare_casez_eq %0d"
      %ch37 = arith.constant 1 : i32
      simulation.display %ctx to %ch37(%fmt37, %ok37_1) newline = true radix = <decimal> flags = [0,0] : !simulation.bytes, i1
      simulation.call @word_compare_casexz_eq(%ctx, %r0, %r1, %r2, %r6) : (!simulation.context, !simulation.ref<!l64>, !simulation.ref<!l64>, !simulation.ref<!l64>, !simulation.ref<i64>) -> ()
      %expected38 = arith.constant 18441677446818168813 : i64
      %v38_1 = simulation.ref.load %r6 : !simulation.ref<i64> -> i64
      %ok38_1 = arith.cmpi eq, %v38_1, %expected38 : i64
      %fmt38 = simulation.bytes.constant "compare_casexz_eq %0d"
      %ch38 = arith.constant 1 : i32
      simulation.display %ctx to %ch38(%fmt38, %ok38_1) newline = true radix = <decimal> flags = [0,0] : !simulation.bytes, i1
      simulation.call @word_shift_left(%ctx, %r0, %r1, %r2, %r4) : (!simulation.context, !simulation.ref<!l64>, !simulation.ref<!l64>, !simulation.ref<!l64>, !simulation.ref<!l64>) -> ()
      %expected39 = simulation.logic.constant 2814792717434890 : i64, 18378344610675883788 : i64 : !l64
      %v39_1 = simulation.ref.load %r4 : !simulation.ref<!l64> -> !l64
      %ok39_1 = simulation.logic.compare case_eq %v39_1, %expected39 : (!l64, !l64) -> i1
      %fmt39 = simulation.bytes.constant "shift_left %0d"
      %ch39 = arith.constant 1 : i32
      simulation.display %ctx to %ch39(%fmt39, %ok39_1) newline = true radix = <decimal> flags = [0,0] : !simulation.bytes, i1
      simulation.call @word_shift_right(%ctx, %r0, %r1, %r2, %r4) : (!simulation.context, !simulation.ref<!l64>, !simulation.ref<!l64>, !simulation.ref<!l64>, !simulation.ref<!l64>) -> ()
      %expected40 = simulation.logic.constant 2814792717434890 : i64, 18378344610675883788 : i64 : !l64
      %v40_1 = simulation.ref.load %r4 : !simulation.ref<!l64> -> !l64
      %ok40_1 = simulation.logic.compare case_eq %v40_1, %expected40 : (!l64, !l64) -> i1
      %fmt40 = simulation.bytes.constant "shift_right %0d"
      %ch40 = arith.constant 1 : i32
      simulation.display %ctx to %ch40(%fmt40, %ok40_1) newline = true radix = <decimal> flags = [0,0] : !simulation.bytes, i1
      simulation.call @word_shift_right_arith(%ctx, %r0, %r1, %r2, %r4) : (!simulation.context, !simulation.ref<!l64>, !simulation.ref<!l64>, !simulation.ref<!l64>, !simulation.ref<!l64>) -> ()
      %expected41 = simulation.logic.constant 47851476196393130 : i64, 18432388630850633676 : i64 : !l64
      %v41_1 = simulation.ref.load %r4 : !simulation.ref<!l64> -> !l64
      %ok41_1 = simulation.logic.compare case_eq %v41_1, %expected41 : (!l64, !l64) -> i1
      %fmt41 = simulation.bytes.constant "shift_right_arith %0d"
      %ch41 = arith.constant 1 : i32
      simulation.display %ctx to %ch41(%fmt41, %ok41_1) newline = true radix = <decimal> flags = [0,0] : !simulation.bytes, i1
      simulation.call @word_power(%ctx, %r0, %r1, %r2, %r4) : (!simulation.context, !simulation.ref<!l64>, !simulation.ref<!l64>, !simulation.ref<!l64>, !simulation.ref<!l64>) -> ()
      %expected42 = simulation.logic.constant 9851774511022115 : i64, 18432388630850633676 : i64 : !l64
      %v42_1 = simulation.ref.load %r4 : !simulation.ref<!l64> -> !l64
      %ok42_1 = simulation.logic.compare case_eq %v42_1, %expected42 : (!l64, !l64) -> i1
      %fmt42 = simulation.bytes.constant "power %0d"
      %ch42 = arith.constant 1 : i32
      simulation.display %ctx to %ch42(%fmt42, %ok42_1) newline = true radix = <decimal> flags = [0,0] : !simulation.bytes, i1
      simulation.call @word_mux(%ctx, %r0, %r1, %r2, %r4) : (!simulation.context, !simulation.ref<!l64>, !simulation.ref<!l64>, !simulation.ref<!l64>, !simulation.ref<!l64>) -> ()
      %expected43 = simulation.logic.constant 3544649704366219296 : i64, 18437173778171358912 : i64 : !l64
      %v43_1 = simulation.ref.load %r4 : !simulation.ref<!l64> -> !l64
      %ok43_1 = simulation.logic.compare case_eq %v43_1, %expected43 : (!l64, !l64) -> i1
      %fmt43 = simulation.bytes.constant "mux %0d"
      %ch43 = arith.constant 1 : i32
      simulation.display %ctx to %ch43(%fmt43, %ok43_1) newline = true radix = <decimal> flags = [0,0] : !simulation.bytes, i1
      simulation.call @word_is_true(%ctx, %r0, %r1, %r2, %r6) : (!simulation.context, !simulation.ref<!l64>, !simulation.ref<!l64>, !simulation.ref<!l64>, !simulation.ref<i64>) -> ()
      %expected44 = arith.constant 2459565876494606882 : i64
      %v44_1 = simulation.ref.load %r6 : !simulation.ref<i64> -> i64
      %ok44_1 = arith.cmpi eq, %v44_1, %expected44 : i64
      %fmt44 = simulation.bytes.constant "is_true %0d"
      %ch44 = arith.constant 1 : i32
      simulation.display %ctx to %ch44(%fmt44, %ok44_1) newline = true radix = <decimal> flags = [0,0] : !simulation.bytes, i1
      simulation.call @word_to_bits(%ctx, %r0, %r1, %r2, %r6) : (!simulation.context, !simulation.ref<!l64>, !simulation.ref<!l64>, !simulation.ref<!l64>, !simulation.ref<i64>) -> ()
      %expected45 = arith.constant 2459565876494606882 : i64
      %v45_1 = simulation.ref.load %r6 : !simulation.ref<i64> -> i64
      %ok45_1 = arith.cmpi eq, %v45_1, %expected45 : i64
      %fmt45 = simulation.bytes.constant "to_bits %0d"
      %ch45 = arith.constant 1 : i32
      simulation.display %ctx to %ch45(%fmt45, %ok45_1) newline = true radix = <decimal> flags = [0,0] : !simulation.bytes, i1
      simulation.call @word_from_bits(%ctx, %r0, %r1, %r2, %r4) : (!simulation.context, !simulation.ref<!l64>, !simulation.ref<!l64>, !simulation.ref<!l64>, !simulation.ref<!l64>) -> ()
      %expected46 = simulation.logic.constant 2459565876494606882 : i64, 0 : i64 : !l64
      %v46_1 = simulation.ref.load %r4 : !simulation.ref<!l64> -> !l64
      %ok46_1 = simulation.logic.compare case_eq %v46_1, %expected46 : (!l64, !l64) -> i1
      %fmt46 = simulation.bytes.constant "from_bits %0d"
      %ch46 = arith.constant 1 : i32
      simulation.display %ctx to %ch46(%fmt46, %ok46_1) newline = true radix = <decimal> flags = [0,0] : !simulation.bytes, i1
      simulation.call @word_integer_andi(%ctx, %r0, %r1, %r2, %r6) : (!simulation.context, !simulation.ref<!l64>, !simulation.ref<!l64>, !simulation.ref<!l64>, !simulation.ref<i64>) -> ()
      %expected47 = arith.constant 9007336695791648 : i64
      %v47_1 = simulation.ref.load %r6 : !simulation.ref<i64> -> i64
      %ok47_1 = arith.cmpi eq, %v47_1, %expected47 : i64
      %fmt47 = simulation.bytes.constant "integer_andi %0d"
      %ch47 = arith.constant 1 : i32
      simulation.display %ctx to %ch47(%fmt47, %ok47_1) newline = true radix = <decimal> flags = [0,0] : !simulation.bytes, i1
      simulation.call @word_integer_ori(%ctx, %r0, %r1, %r2, %r6) : (!simulation.context, !simulation.ref<!l64>, !simulation.ref<!l64>, !simulation.ref<!l64>, !simulation.ref<i64>) -> ()
      %expected48 = arith.constant 2518113565017252594 : i64
      %v48_1 = simulation.ref.load %r6 : !simulation.ref<i64> -> i64
      %ok48_1 = arith.cmpi eq, %v48_1, %expected48 : i64
      %fmt48 = simulation.bytes.constant "integer_ori %0d"
      %ch48 = arith.constant 1 : i32
      simulation.display %ctx to %ch48(%fmt48, %ok48_1) newline = true radix = <decimal> flags = [0,0] : !simulation.bytes, i1
      simulation.call @word_integer_xori(%ctx, %r0, %r1, %r2, %r6) : (!simulation.context, !simulation.ref<!l64>, !simulation.ref<!l64>, !simulation.ref<!l64>, !simulation.ref<i64>) -> ()
      %expected49 = arith.constant 2509106228321460946 : i64
      %v49_1 = simulation.ref.load %r6 : !simulation.ref<i64> -> i64
      %ok49_1 = arith.cmpi eq, %v49_1, %expected49 : i64
      %fmt49 = simulation.bytes.constant "integer_xori %0d"
      %ch49 = arith.constant 1 : i32
      simulation.display %ctx to %ch49(%fmt49, %ok49_1) newline = true radix = <decimal> flags = [0,0] : !simulation.bytes, i1
      simulation.call @word_integer_addi(%ctx, %r0, %r1, %r2, %r6) : (!simulation.context, !simulation.ref<!l64>, !simulation.ref<!l64>, !simulation.ref<!l64>, !simulation.ref<i64>) -> ()
      %expected50 = arith.constant 2509106228321460946 : i64
      %v50_1 = simulation.ref.load %r6 : !simulation.ref<i64> -> i64
      %ok50_1 = arith.cmpi eq, %v50_1, %expected50 : i64
      %fmt50 = simulation.bytes.constant "integer_addi %0d"
      %ch50 = arith.constant 1 : i32
      simulation.display %ctx to %ch50(%fmt50, %ok50_1) newline = true radix = <decimal> flags = [0,0] : !simulation.bytes, i1
      simulation.call @word_integer_subi(%ctx, %r0, %r1, %r2, %r6) : (!simulation.context, !simulation.ref<!l64>, !simulation.ref<!l64>, !simulation.ref<!l64>, !simulation.ref<i64>) -> ()
      %expected51 = arith.constant 2509106228321460946 : i64
      %v51_1 = simulation.ref.load %r6 : !simulation.ref<i64> -> i64
      %ok51_1 = arith.cmpi eq, %v51_1, %expected51 : i64
      %fmt51 = simulation.bytes.constant "integer_subi %0d"
      %ch51 = arith.constant 1 : i32
      simulation.display %ctx to %ch51(%fmt51, %ok51_1) newline = true radix = <decimal> flags = [0,0] : !simulation.bytes, i1
      simulation.call @word_integer_muli(%ctx, %r0, %r1, %r2, %r6) : (!simulation.context, !simulation.ref<!l64>, !simulation.ref<!l64>, !simulation.ref<!l64>, !simulation.ref<i64>) -> ()
      %expected52 = arith.constant 9007336695791648 : i64
      %v52_1 = simulation.ref.load %r6 : !simulation.ref<i64> -> i64
      %ok52_1 = arith.cmpi eq, %v52_1, %expected52 : i64
      %fmt52 = simulation.bytes.constant "integer_muli %0d"
      %ch52 = arith.constant 1 : i32
      simulation.display %ctx to %ch52(%fmt52, %ok52_1) newline = true radix = <decimal> flags = [0,0] : !simulation.bytes, i1
      simulation.call @word_integer_select(%ctx, %r0, %r1, %r2, %r6) : (!simulation.context, !simulation.ref<!l64>, !simulation.ref<!l64>, !simulation.ref<!l64>, !simulation.ref<i64>) -> ()
      %expected53 = arith.constant 9007340418039840 : i64
      %v53_1 = simulation.ref.load %r6 : !simulation.ref<i64> -> i64
      %ok53_1 = arith.cmpi eq, %v53_1, %expected53 : i64
      %fmt53 = simulation.bytes.constant "integer_select %0d"
      %ch53 = arith.constant 1 : i32
      simulation.display %ctx to %ch53(%fmt53, %ok53_1) newline = true radix = <decimal> flags = [0,0] : !simulation.bytes, i1
      simulation.call @word_wide_constant_shift(%ctx, %r0, %r1, %r2, %r4) : (!simulation.context, !simulation.ref<!l64>, !simulation.ref<!l64>, !simulation.ref<!l64>, !simulation.ref<!l64>) -> ()
      %expected54 = simulation.logic.constant 0 : i64, 0 : i64 : !l64
      %v54_1 = simulation.ref.load %r4 : !simulation.ref<!l64> -> !l64
      %ok54_1 = simulation.logic.compare case_eq %v54_1, %expected54 : (!l64, !l64) -> i1
      %fmt54 = simulation.bytes.constant "wide_constant_shift %0d"
      %ch54 = arith.constant 1 : i32
      simulation.display %ctx to %ch54(%fmt54, %ok54_1) newline = true radix = <decimal> flags = [0,0] : !simulation.bytes, i1
      simulation.return
    }
    simulation.func @word_unary_plus(%ctx: !simulation.context {simulation.capture_kind = 0 : i32}, %r0: !simulation.ref<!l64> {simulation.capture_kind = 3 : i32, simulation.descriptor_id = 0 : i64}, %r1: !simulation.ref<!l64> {simulation.capture_kind = 3 : i32, simulation.descriptor_id = 1 : i64}, %r2: !simulation.ref<!l64> {simulation.capture_kind = 3 : i32, simulation.descriptor_id = 2 : i64}, %out: !simulation.ref<!l64> {simulation.capture_kind = 3 : i32, simulation.descriptor_id = 4 : i64}) attributes {entry_kind = 8 : i32, code_unit_id = 3 : i64, schedule.eval.raw_captures} {
      %zero = arith.constant 0 : i32
      cf.br ^head(%zero : i32)
    ^head(%i: i32):
      %limit = arith.constant 64 : i32
      %more = arith.cmpi ult, %i, %limit : i32
      cf.cond_br %more, ^body, ^exit
    ^body:
      %ar = simulation.ref.dyn_extract %r0 from %i : (!simulation.ref<!l64>, i32) -> !simulation.ref<!l1>
      %a = simulation.ref.load %ar : !simulation.ref<!l1> -> !l1
      %br = simulation.ref.dyn_extract %r1 from %i : (!simulation.ref<!l64>, i32) -> !simulation.ref<!l1>
      %b = simulation.ref.load %br : !simulation.ref<!l1> -> !l1
      %cr = simulation.ref.dyn_extract %r2 from %i : (!simulation.ref<!l64>, i32) -> !simulation.ref<!l1>
      %c = simulation.ref.load %cr : !simulation.ref<!l1> -> !l1
      %result = simulation.logic.unary plus %a : (!l1) -> !l1
      %dst = simulation.ref.dyn_extract %out from %i : (!simulation.ref<!l64>, i32) -> !simulation.ref<!l1>
      simulation.ref.store %result to %dst : !l1, !simulation.ref<!l1>
      %one = arith.constant 1 : i32
      %next = arith.addi %i, %one : i32
      cf.br ^head(%next : i32)
    ^exit:
      simulation.return
    }
    simulation.func @word_unary_negate(%ctx: !simulation.context {simulation.capture_kind = 0 : i32}, %r0: !simulation.ref<!l64> {simulation.capture_kind = 3 : i32, simulation.descriptor_id = 0 : i64}, %r1: !simulation.ref<!l64> {simulation.capture_kind = 3 : i32, simulation.descriptor_id = 1 : i64}, %r2: !simulation.ref<!l64> {simulation.capture_kind = 3 : i32, simulation.descriptor_id = 2 : i64}, %out: !simulation.ref<!l64> {simulation.capture_kind = 3 : i32, simulation.descriptor_id = 4 : i64}) attributes {entry_kind = 8 : i32, code_unit_id = 5 : i64, schedule.eval.raw_captures} {
      %zero = arith.constant 0 : i32
      cf.br ^head(%zero : i32)
    ^head(%i: i32):
      %limit = arith.constant 64 : i32
      %more = arith.cmpi ult, %i, %limit : i32
      cf.cond_br %more, ^body, ^exit
    ^body:
      %ar = simulation.ref.dyn_extract %r0 from %i : (!simulation.ref<!l64>, i32) -> !simulation.ref<!l1>
      %a = simulation.ref.load %ar : !simulation.ref<!l1> -> !l1
      %br = simulation.ref.dyn_extract %r1 from %i : (!simulation.ref<!l64>, i32) -> !simulation.ref<!l1>
      %b = simulation.ref.load %br : !simulation.ref<!l1> -> !l1
      %cr = simulation.ref.dyn_extract %r2 from %i : (!simulation.ref<!l64>, i32) -> !simulation.ref<!l1>
      %c = simulation.ref.load %cr : !simulation.ref<!l1> -> !l1
      %result = simulation.logic.unary negate %a : (!l1) -> !l1
      %dst = simulation.ref.dyn_extract %out from %i : (!simulation.ref<!l64>, i32) -> !simulation.ref<!l1>
      simulation.ref.store %result to %dst : !l1, !simulation.ref<!l1>
      %one = arith.constant 1 : i32
      %next = arith.addi %i, %one : i32
      cf.br ^head(%next : i32)
    ^exit:
      simulation.return
    }
    simulation.func @word_unary_bit_not(%ctx: !simulation.context {simulation.capture_kind = 0 : i32}, %r0: !simulation.ref<!l64> {simulation.capture_kind = 3 : i32, simulation.descriptor_id = 0 : i64}, %r1: !simulation.ref<!l64> {simulation.capture_kind = 3 : i32, simulation.descriptor_id = 1 : i64}, %r2: !simulation.ref<!l64> {simulation.capture_kind = 3 : i32, simulation.descriptor_id = 2 : i64}, %out: !simulation.ref<!l64> {simulation.capture_kind = 3 : i32, simulation.descriptor_id = 4 : i64}) attributes {entry_kind = 8 : i32, code_unit_id = 7 : i64, schedule.eval.raw_captures} {
      %zero = arith.constant 0 : i32
      cf.br ^head(%zero : i32)
    ^head(%i: i32):
      %limit = arith.constant 64 : i32
      %more = arith.cmpi ult, %i, %limit : i32
      cf.cond_br %more, ^body, ^exit
    ^body:
      %ar = simulation.ref.dyn_extract %r0 from %i : (!simulation.ref<!l64>, i32) -> !simulation.ref<!l1>
      %a = simulation.ref.load %ar : !simulation.ref<!l1> -> !l1
      %br = simulation.ref.dyn_extract %r1 from %i : (!simulation.ref<!l64>, i32) -> !simulation.ref<!l1>
      %b = simulation.ref.load %br : !simulation.ref<!l1> -> !l1
      %cr = simulation.ref.dyn_extract %r2 from %i : (!simulation.ref<!l64>, i32) -> !simulation.ref<!l1>
      %c = simulation.ref.load %cr : !simulation.ref<!l1> -> !l1
      %result = simulation.logic.unary bit_not %a : (!l1) -> !l1
      %dst = simulation.ref.dyn_extract %out from %i : (!simulation.ref<!l64>, i32) -> !simulation.ref<!l1>
      simulation.ref.store %result to %dst : !l1, !simulation.ref<!l1>
      %one = arith.constant 1 : i32
      %next = arith.addi %i, %one : i32
      cf.br ^head(%next : i32)
    ^exit:
      simulation.return
    }
    simulation.func @word_unary_logical_not(%ctx: !simulation.context {simulation.capture_kind = 0 : i32}, %r0: !simulation.ref<!l64> {simulation.capture_kind = 3 : i32, simulation.descriptor_id = 0 : i64}, %r1: !simulation.ref<!l64> {simulation.capture_kind = 3 : i32, simulation.descriptor_id = 1 : i64}, %r2: !simulation.ref<!l64> {simulation.capture_kind = 3 : i32, simulation.descriptor_id = 2 : i64}, %out: !simulation.ref<!l64> {simulation.capture_kind = 3 : i32, simulation.descriptor_id = 4 : i64}) attributes {entry_kind = 8 : i32, code_unit_id = 9 : i64, schedule.eval.raw_captures} {
      %zero = arith.constant 0 : i32
      cf.br ^head(%zero : i32)
    ^head(%i: i32):
      %limit = arith.constant 64 : i32
      %more = arith.cmpi ult, %i, %limit : i32
      cf.cond_br %more, ^body, ^exit
    ^body:
      %ar = simulation.ref.dyn_extract %r0 from %i : (!simulation.ref<!l64>, i32) -> !simulation.ref<!l1>
      %a = simulation.ref.load %ar : !simulation.ref<!l1> -> !l1
      %br = simulation.ref.dyn_extract %r1 from %i : (!simulation.ref<!l64>, i32) -> !simulation.ref<!l1>
      %b = simulation.ref.load %br : !simulation.ref<!l1> -> !l1
      %cr = simulation.ref.dyn_extract %r2 from %i : (!simulation.ref<!l64>, i32) -> !simulation.ref<!l1>
      %c = simulation.ref.load %cr : !simulation.ref<!l1> -> !l1
      %result = simulation.logic.unary logical_not %a : (!l1) -> !l1
      %dst = simulation.ref.dyn_extract %out from %i : (!simulation.ref<!l64>, i32) -> !simulation.ref<!l1>
      simulation.ref.store %result to %dst : !l1, !simulation.ref<!l1>
      %one = arith.constant 1 : i32
      %next = arith.addi %i, %one : i32
      cf.br ^head(%next : i32)
    ^exit:
      simulation.return
    }
    simulation.func @word_binary_add(%ctx: !simulation.context {simulation.capture_kind = 0 : i32}, %r0: !simulation.ref<!l64> {simulation.capture_kind = 3 : i32, simulation.descriptor_id = 0 : i64}, %r1: !simulation.ref<!l64> {simulation.capture_kind = 3 : i32, simulation.descriptor_id = 1 : i64}, %r2: !simulation.ref<!l64> {simulation.capture_kind = 3 : i32, simulation.descriptor_id = 2 : i64}, %out: !simulation.ref<!l64> {simulation.capture_kind = 3 : i32, simulation.descriptor_id = 4 : i64}) attributes {entry_kind = 8 : i32, code_unit_id = 11 : i64, schedule.eval.raw_captures} {
      %zero = arith.constant 0 : i32
      cf.br ^head(%zero : i32)
    ^head(%i: i32):
      %limit = arith.constant 64 : i32
      %more = arith.cmpi ult, %i, %limit : i32
      cf.cond_br %more, ^body, ^exit
    ^body:
      %ar = simulation.ref.dyn_extract %r0 from %i : (!simulation.ref<!l64>, i32) -> !simulation.ref<!l1>
      %a = simulation.ref.load %ar : !simulation.ref<!l1> -> !l1
      %br = simulation.ref.dyn_extract %r1 from %i : (!simulation.ref<!l64>, i32) -> !simulation.ref<!l1>
      %b = simulation.ref.load %br : !simulation.ref<!l1> -> !l1
      %cr = simulation.ref.dyn_extract %r2 from %i : (!simulation.ref<!l64>, i32) -> !simulation.ref<!l1>
      %c = simulation.ref.load %cr : !simulation.ref<!l1> -> !l1
      %result = simulation.logic.binary add %a, %b : !l1
      %dst = simulation.ref.dyn_extract %out from %i : (!simulation.ref<!l64>, i32) -> !simulation.ref<!l1>
      simulation.ref.store %result to %dst : !l1, !simulation.ref<!l1>
      %one = arith.constant 1 : i32
      %next = arith.addi %i, %one : i32
      cf.br ^head(%next : i32)
    ^exit:
      simulation.return
    }
    simulation.func @word_binary_sub(%ctx: !simulation.context {simulation.capture_kind = 0 : i32}, %r0: !simulation.ref<!l64> {simulation.capture_kind = 3 : i32, simulation.descriptor_id = 0 : i64}, %r1: !simulation.ref<!l64> {simulation.capture_kind = 3 : i32, simulation.descriptor_id = 1 : i64}, %r2: !simulation.ref<!l64> {simulation.capture_kind = 3 : i32, simulation.descriptor_id = 2 : i64}, %out: !simulation.ref<!l64> {simulation.capture_kind = 3 : i32, simulation.descriptor_id = 4 : i64}) attributes {entry_kind = 8 : i32, code_unit_id = 13 : i64, schedule.eval.raw_captures} {
      %zero = arith.constant 0 : i32
      cf.br ^head(%zero : i32)
    ^head(%i: i32):
      %limit = arith.constant 64 : i32
      %more = arith.cmpi ult, %i, %limit : i32
      cf.cond_br %more, ^body, ^exit
    ^body:
      %ar = simulation.ref.dyn_extract %r0 from %i : (!simulation.ref<!l64>, i32) -> !simulation.ref<!l1>
      %a = simulation.ref.load %ar : !simulation.ref<!l1> -> !l1
      %br = simulation.ref.dyn_extract %r1 from %i : (!simulation.ref<!l64>, i32) -> !simulation.ref<!l1>
      %b = simulation.ref.load %br : !simulation.ref<!l1> -> !l1
      %cr = simulation.ref.dyn_extract %r2 from %i : (!simulation.ref<!l64>, i32) -> !simulation.ref<!l1>
      %c = simulation.ref.load %cr : !simulation.ref<!l1> -> !l1
      %result = simulation.logic.binary sub %a, %b : !l1
      %dst = simulation.ref.dyn_extract %out from %i : (!simulation.ref<!l64>, i32) -> !simulation.ref<!l1>
      simulation.ref.store %result to %dst : !l1, !simulation.ref<!l1>
      %one = arith.constant 1 : i32
      %next = arith.addi %i, %one : i32
      cf.br ^head(%next : i32)
    ^exit:
      simulation.return
    }
    simulation.func @word_binary_mul(%ctx: !simulation.context {simulation.capture_kind = 0 : i32}, %r0: !simulation.ref<!l64> {simulation.capture_kind = 3 : i32, simulation.descriptor_id = 0 : i64}, %r1: !simulation.ref<!l64> {simulation.capture_kind = 3 : i32, simulation.descriptor_id = 1 : i64}, %r2: !simulation.ref<!l64> {simulation.capture_kind = 3 : i32, simulation.descriptor_id = 2 : i64}, %out: !simulation.ref<!l64> {simulation.capture_kind = 3 : i32, simulation.descriptor_id = 4 : i64}) attributes {entry_kind = 8 : i32, code_unit_id = 15 : i64, schedule.eval.raw_captures} {
      %zero = arith.constant 0 : i32
      cf.br ^head(%zero : i32)
    ^head(%i: i32):
      %limit = arith.constant 64 : i32
      %more = arith.cmpi ult, %i, %limit : i32
      cf.cond_br %more, ^body, ^exit
    ^body:
      %ar = simulation.ref.dyn_extract %r0 from %i : (!simulation.ref<!l64>, i32) -> !simulation.ref<!l1>
      %a = simulation.ref.load %ar : !simulation.ref<!l1> -> !l1
      %br = simulation.ref.dyn_extract %r1 from %i : (!simulation.ref<!l64>, i32) -> !simulation.ref<!l1>
      %b = simulation.ref.load %br : !simulation.ref<!l1> -> !l1
      %cr = simulation.ref.dyn_extract %r2 from %i : (!simulation.ref<!l64>, i32) -> !simulation.ref<!l1>
      %c = simulation.ref.load %cr : !simulation.ref<!l1> -> !l1
      %result = simulation.logic.binary mul %a, %b : !l1
      %dst = simulation.ref.dyn_extract %out from %i : (!simulation.ref<!l64>, i32) -> !simulation.ref<!l1>
      simulation.ref.store %result to %dst : !l1, !simulation.ref<!l1>
      %one = arith.constant 1 : i32
      %next = arith.addi %i, %one : i32
      cf.br ^head(%next : i32)
    ^exit:
      simulation.return
    }
    simulation.func @word_binary_udiv(%ctx: !simulation.context {simulation.capture_kind = 0 : i32}, %r0: !simulation.ref<!l64> {simulation.capture_kind = 3 : i32, simulation.descriptor_id = 0 : i64}, %r1: !simulation.ref<!l64> {simulation.capture_kind = 3 : i32, simulation.descriptor_id = 1 : i64}, %r2: !simulation.ref<!l64> {simulation.capture_kind = 3 : i32, simulation.descriptor_id = 2 : i64}, %out: !simulation.ref<!l64> {simulation.capture_kind = 3 : i32, simulation.descriptor_id = 4 : i64}) attributes {entry_kind = 8 : i32, code_unit_id = 17 : i64, schedule.eval.raw_captures} {
      %zero = arith.constant 0 : i32
      cf.br ^head(%zero : i32)
    ^head(%i: i32):
      %limit = arith.constant 64 : i32
      %more = arith.cmpi ult, %i, %limit : i32
      cf.cond_br %more, ^body, ^exit
    ^body:
      %ar = simulation.ref.dyn_extract %r0 from %i : (!simulation.ref<!l64>, i32) -> !simulation.ref<!l1>
      %a = simulation.ref.load %ar : !simulation.ref<!l1> -> !l1
      %br = simulation.ref.dyn_extract %r1 from %i : (!simulation.ref<!l64>, i32) -> !simulation.ref<!l1>
      %b = simulation.ref.load %br : !simulation.ref<!l1> -> !l1
      %cr = simulation.ref.dyn_extract %r2 from %i : (!simulation.ref<!l64>, i32) -> !simulation.ref<!l1>
      %c = simulation.ref.load %cr : !simulation.ref<!l1> -> !l1
      %result = simulation.logic.binary udiv %a, %b : !l1
      %dst = simulation.ref.dyn_extract %out from %i : (!simulation.ref<!l64>, i32) -> !simulation.ref<!l1>
      simulation.ref.store %result to %dst : !l1, !simulation.ref<!l1>
      %one = arith.constant 1 : i32
      %next = arith.addi %i, %one : i32
      cf.br ^head(%next : i32)
    ^exit:
      simulation.return
    }
    simulation.func @word_binary_sdiv(%ctx: !simulation.context {simulation.capture_kind = 0 : i32}, %r0: !simulation.ref<!l64> {simulation.capture_kind = 3 : i32, simulation.descriptor_id = 0 : i64}, %r1: !simulation.ref<!l64> {simulation.capture_kind = 3 : i32, simulation.descriptor_id = 1 : i64}, %r2: !simulation.ref<!l64> {simulation.capture_kind = 3 : i32, simulation.descriptor_id = 2 : i64}, %out: !simulation.ref<!l64> {simulation.capture_kind = 3 : i32, simulation.descriptor_id = 4 : i64}) attributes {entry_kind = 8 : i32, code_unit_id = 19 : i64, schedule.eval.raw_captures} {
      %zero = arith.constant 0 : i32
      cf.br ^head(%zero : i32)
    ^head(%i: i32):
      %limit = arith.constant 64 : i32
      %more = arith.cmpi ult, %i, %limit : i32
      cf.cond_br %more, ^body, ^exit
    ^body:
      %ar = simulation.ref.dyn_extract %r0 from %i : (!simulation.ref<!l64>, i32) -> !simulation.ref<!l1>
      %a = simulation.ref.load %ar : !simulation.ref<!l1> -> !l1
      %br = simulation.ref.dyn_extract %r1 from %i : (!simulation.ref<!l64>, i32) -> !simulation.ref<!l1>
      %b = simulation.ref.load %br : !simulation.ref<!l1> -> !l1
      %cr = simulation.ref.dyn_extract %r2 from %i : (!simulation.ref<!l64>, i32) -> !simulation.ref<!l1>
      %c = simulation.ref.load %cr : !simulation.ref<!l1> -> !l1
      %result = simulation.logic.binary sdiv %a, %b : !l1
      %dst = simulation.ref.dyn_extract %out from %i : (!simulation.ref<!l64>, i32) -> !simulation.ref<!l1>
      simulation.ref.store %result to %dst : !l1, !simulation.ref<!l1>
      %one = arith.constant 1 : i32
      %next = arith.addi %i, %one : i32
      cf.br ^head(%next : i32)
    ^exit:
      simulation.return
    }
    simulation.func @word_binary_umod(%ctx: !simulation.context {simulation.capture_kind = 0 : i32}, %r0: !simulation.ref<!l64> {simulation.capture_kind = 3 : i32, simulation.descriptor_id = 0 : i64}, %r1: !simulation.ref<!l64> {simulation.capture_kind = 3 : i32, simulation.descriptor_id = 1 : i64}, %r2: !simulation.ref<!l64> {simulation.capture_kind = 3 : i32, simulation.descriptor_id = 2 : i64}, %out: !simulation.ref<!l64> {simulation.capture_kind = 3 : i32, simulation.descriptor_id = 4 : i64}) attributes {entry_kind = 8 : i32, code_unit_id = 21 : i64, schedule.eval.raw_captures} {
      %zero = arith.constant 0 : i32
      cf.br ^head(%zero : i32)
    ^head(%i: i32):
      %limit = arith.constant 64 : i32
      %more = arith.cmpi ult, %i, %limit : i32
      cf.cond_br %more, ^body, ^exit
    ^body:
      %ar = simulation.ref.dyn_extract %r0 from %i : (!simulation.ref<!l64>, i32) -> !simulation.ref<!l1>
      %a = simulation.ref.load %ar : !simulation.ref<!l1> -> !l1
      %br = simulation.ref.dyn_extract %r1 from %i : (!simulation.ref<!l64>, i32) -> !simulation.ref<!l1>
      %b = simulation.ref.load %br : !simulation.ref<!l1> -> !l1
      %cr = simulation.ref.dyn_extract %r2 from %i : (!simulation.ref<!l64>, i32) -> !simulation.ref<!l1>
      %c = simulation.ref.load %cr : !simulation.ref<!l1> -> !l1
      %result = simulation.logic.binary umod %a, %b : !l1
      %dst = simulation.ref.dyn_extract %out from %i : (!simulation.ref<!l64>, i32) -> !simulation.ref<!l1>
      simulation.ref.store %result to %dst : !l1, !simulation.ref<!l1>
      %one = arith.constant 1 : i32
      %next = arith.addi %i, %one : i32
      cf.br ^head(%next : i32)
    ^exit:
      simulation.return
    }
    simulation.func @word_binary_smod(%ctx: !simulation.context {simulation.capture_kind = 0 : i32}, %r0: !simulation.ref<!l64> {simulation.capture_kind = 3 : i32, simulation.descriptor_id = 0 : i64}, %r1: !simulation.ref<!l64> {simulation.capture_kind = 3 : i32, simulation.descriptor_id = 1 : i64}, %r2: !simulation.ref<!l64> {simulation.capture_kind = 3 : i32, simulation.descriptor_id = 2 : i64}, %out: !simulation.ref<!l64> {simulation.capture_kind = 3 : i32, simulation.descriptor_id = 4 : i64}) attributes {entry_kind = 8 : i32, code_unit_id = 23 : i64, schedule.eval.raw_captures} {
      %zero = arith.constant 0 : i32
      cf.br ^head(%zero : i32)
    ^head(%i: i32):
      %limit = arith.constant 64 : i32
      %more = arith.cmpi ult, %i, %limit : i32
      cf.cond_br %more, ^body, ^exit
    ^body:
      %ar = simulation.ref.dyn_extract %r0 from %i : (!simulation.ref<!l64>, i32) -> !simulation.ref<!l1>
      %a = simulation.ref.load %ar : !simulation.ref<!l1> -> !l1
      %br = simulation.ref.dyn_extract %r1 from %i : (!simulation.ref<!l64>, i32) -> !simulation.ref<!l1>
      %b = simulation.ref.load %br : !simulation.ref<!l1> -> !l1
      %cr = simulation.ref.dyn_extract %r2 from %i : (!simulation.ref<!l64>, i32) -> !simulation.ref<!l1>
      %c = simulation.ref.load %cr : !simulation.ref<!l1> -> !l1
      %result = simulation.logic.binary smod %a, %b : !l1
      %dst = simulation.ref.dyn_extract %out from %i : (!simulation.ref<!l64>, i32) -> !simulation.ref<!l1>
      simulation.ref.store %result to %dst : !l1, !simulation.ref<!l1>
      %one = arith.constant 1 : i32
      %next = arith.addi %i, %one : i32
      cf.br ^head(%next : i32)
    ^exit:
      simulation.return
    }
    simulation.func @word_binary_and(%ctx: !simulation.context {simulation.capture_kind = 0 : i32}, %r0: !simulation.ref<!l64> {simulation.capture_kind = 3 : i32, simulation.descriptor_id = 0 : i64}, %r1: !simulation.ref<!l64> {simulation.capture_kind = 3 : i32, simulation.descriptor_id = 1 : i64}, %r2: !simulation.ref<!l64> {simulation.capture_kind = 3 : i32, simulation.descriptor_id = 2 : i64}, %out: !simulation.ref<!l64> {simulation.capture_kind = 3 : i32, simulation.descriptor_id = 4 : i64}) attributes {entry_kind = 8 : i32, code_unit_id = 25 : i64, schedule.eval.raw_captures} {
      %zero = arith.constant 0 : i32
      cf.br ^head(%zero : i32)
    ^head(%i: i32):
      %limit = arith.constant 64 : i32
      %more = arith.cmpi ult, %i, %limit : i32
      cf.cond_br %more, ^body, ^exit
    ^body:
      %ar = simulation.ref.dyn_extract %r0 from %i : (!simulation.ref<!l64>, i32) -> !simulation.ref<!l1>
      %a = simulation.ref.load %ar : !simulation.ref<!l1> -> !l1
      %br = simulation.ref.dyn_extract %r1 from %i : (!simulation.ref<!l64>, i32) -> !simulation.ref<!l1>
      %b = simulation.ref.load %br : !simulation.ref<!l1> -> !l1
      %cr = simulation.ref.dyn_extract %r2 from %i : (!simulation.ref<!l64>, i32) -> !simulation.ref<!l1>
      %c = simulation.ref.load %cr : !simulation.ref<!l1> -> !l1
      %result = simulation.logic.binary and %a, %b : !l1
      %dst = simulation.ref.dyn_extract %out from %i : (!simulation.ref<!l64>, i32) -> !simulation.ref<!l1>
      simulation.ref.store %result to %dst : !l1, !simulation.ref<!l1>
      %one = arith.constant 1 : i32
      %next = arith.addi %i, %one : i32
      cf.br ^head(%next : i32)
    ^exit:
      simulation.return
    }
    simulation.func @word_binary_or(%ctx: !simulation.context {simulation.capture_kind = 0 : i32}, %r0: !simulation.ref<!l64> {simulation.capture_kind = 3 : i32, simulation.descriptor_id = 0 : i64}, %r1: !simulation.ref<!l64> {simulation.capture_kind = 3 : i32, simulation.descriptor_id = 1 : i64}, %r2: !simulation.ref<!l64> {simulation.capture_kind = 3 : i32, simulation.descriptor_id = 2 : i64}, %out: !simulation.ref<!l64> {simulation.capture_kind = 3 : i32, simulation.descriptor_id = 4 : i64}) attributes {entry_kind = 8 : i32, code_unit_id = 27 : i64, schedule.eval.raw_captures} {
      %zero = arith.constant 0 : i32
      cf.br ^head(%zero : i32)
    ^head(%i: i32):
      %limit = arith.constant 64 : i32
      %more = arith.cmpi ult, %i, %limit : i32
      cf.cond_br %more, ^body, ^exit
    ^body:
      %ar = simulation.ref.dyn_extract %r0 from %i : (!simulation.ref<!l64>, i32) -> !simulation.ref<!l1>
      %a = simulation.ref.load %ar : !simulation.ref<!l1> -> !l1
      %br = simulation.ref.dyn_extract %r1 from %i : (!simulation.ref<!l64>, i32) -> !simulation.ref<!l1>
      %b = simulation.ref.load %br : !simulation.ref<!l1> -> !l1
      %cr = simulation.ref.dyn_extract %r2 from %i : (!simulation.ref<!l64>, i32) -> !simulation.ref<!l1>
      %c = simulation.ref.load %cr : !simulation.ref<!l1> -> !l1
      %result = simulation.logic.binary or %a, %b : !l1
      %dst = simulation.ref.dyn_extract %out from %i : (!simulation.ref<!l64>, i32) -> !simulation.ref<!l1>
      simulation.ref.store %result to %dst : !l1, !simulation.ref<!l1>
      %one = arith.constant 1 : i32
      %next = arith.addi %i, %one : i32
      cf.br ^head(%next : i32)
    ^exit:
      simulation.return
    }
    simulation.func @word_binary_xor(%ctx: !simulation.context {simulation.capture_kind = 0 : i32}, %r0: !simulation.ref<!l64> {simulation.capture_kind = 3 : i32, simulation.descriptor_id = 0 : i64}, %r1: !simulation.ref<!l64> {simulation.capture_kind = 3 : i32, simulation.descriptor_id = 1 : i64}, %r2: !simulation.ref<!l64> {simulation.capture_kind = 3 : i32, simulation.descriptor_id = 2 : i64}, %out: !simulation.ref<!l64> {simulation.capture_kind = 3 : i32, simulation.descriptor_id = 4 : i64}) attributes {entry_kind = 8 : i32, code_unit_id = 29 : i64, schedule.eval.raw_captures} {
      %zero = arith.constant 0 : i32
      cf.br ^head(%zero : i32)
    ^head(%i: i32):
      %limit = arith.constant 64 : i32
      %more = arith.cmpi ult, %i, %limit : i32
      cf.cond_br %more, ^body, ^exit
    ^body:
      %ar = simulation.ref.dyn_extract %r0 from %i : (!simulation.ref<!l64>, i32) -> !simulation.ref<!l1>
      %a = simulation.ref.load %ar : !simulation.ref<!l1> -> !l1
      %br = simulation.ref.dyn_extract %r1 from %i : (!simulation.ref<!l64>, i32) -> !simulation.ref<!l1>
      %b = simulation.ref.load %br : !simulation.ref<!l1> -> !l1
      %cr = simulation.ref.dyn_extract %r2 from %i : (!simulation.ref<!l64>, i32) -> !simulation.ref<!l1>
      %c = simulation.ref.load %cr : !simulation.ref<!l1> -> !l1
      %result = simulation.logic.binary xor %a, %b : !l1
      %dst = simulation.ref.dyn_extract %out from %i : (!simulation.ref<!l64>, i32) -> !simulation.ref<!l1>
      simulation.ref.store %result to %dst : !l1, !simulation.ref<!l1>
      %one = arith.constant 1 : i32
      %next = arith.addi %i, %one : i32
      cf.br ^head(%next : i32)
    ^exit:
      simulation.return
    }
    simulation.func @word_binary_xnor(%ctx: !simulation.context {simulation.capture_kind = 0 : i32}, %r0: !simulation.ref<!l64> {simulation.capture_kind = 3 : i32, simulation.descriptor_id = 0 : i64}, %r1: !simulation.ref<!l64> {simulation.capture_kind = 3 : i32, simulation.descriptor_id = 1 : i64}, %r2: !simulation.ref<!l64> {simulation.capture_kind = 3 : i32, simulation.descriptor_id = 2 : i64}, %out: !simulation.ref<!l64> {simulation.capture_kind = 3 : i32, simulation.descriptor_id = 4 : i64}) attributes {entry_kind = 8 : i32, code_unit_id = 31 : i64, schedule.eval.raw_captures} {
      %zero = arith.constant 0 : i32
      cf.br ^head(%zero : i32)
    ^head(%i: i32):
      %limit = arith.constant 64 : i32
      %more = arith.cmpi ult, %i, %limit : i32
      cf.cond_br %more, ^body, ^exit
    ^body:
      %ar = simulation.ref.dyn_extract %r0 from %i : (!simulation.ref<!l64>, i32) -> !simulation.ref<!l1>
      %a = simulation.ref.load %ar : !simulation.ref<!l1> -> !l1
      %br = simulation.ref.dyn_extract %r1 from %i : (!simulation.ref<!l64>, i32) -> !simulation.ref<!l1>
      %b = simulation.ref.load %br : !simulation.ref<!l1> -> !l1
      %cr = simulation.ref.dyn_extract %r2 from %i : (!simulation.ref<!l64>, i32) -> !simulation.ref<!l1>
      %c = simulation.ref.load %cr : !simulation.ref<!l1> -> !l1
      %result = simulation.logic.binary xnor %a, %b : !l1
      %dst = simulation.ref.dyn_extract %out from %i : (!simulation.ref<!l64>, i32) -> !simulation.ref<!l1>
      simulation.ref.store %result to %dst : !l1, !simulation.ref<!l1>
      %one = arith.constant 1 : i32
      %next = arith.addi %i, %one : i32
      cf.br ^head(%next : i32)
    ^exit:
      simulation.return
    }
    simulation.func @word_logical_and(%ctx: !simulation.context {simulation.capture_kind = 0 : i32}, %r0: !simulation.ref<!l64> {simulation.capture_kind = 3 : i32, simulation.descriptor_id = 0 : i64}, %r1: !simulation.ref<!l64> {simulation.capture_kind = 3 : i32, simulation.descriptor_id = 1 : i64}, %r2: !simulation.ref<!l64> {simulation.capture_kind = 3 : i32, simulation.descriptor_id = 2 : i64}, %out: !simulation.ref<!l64> {simulation.capture_kind = 3 : i32, simulation.descriptor_id = 4 : i64}) attributes {entry_kind = 8 : i32, code_unit_id = 33 : i64, schedule.eval.raw_captures} {
      %zero = arith.constant 0 : i32
      cf.br ^head(%zero : i32)
    ^head(%i: i32):
      %limit = arith.constant 64 : i32
      %more = arith.cmpi ult, %i, %limit : i32
      cf.cond_br %more, ^body, ^exit
    ^body:
      %ar = simulation.ref.dyn_extract %r0 from %i : (!simulation.ref<!l64>, i32) -> !simulation.ref<!l1>
      %a = simulation.ref.load %ar : !simulation.ref<!l1> -> !l1
      %br = simulation.ref.dyn_extract %r1 from %i : (!simulation.ref<!l64>, i32) -> !simulation.ref<!l1>
      %b = simulation.ref.load %br : !simulation.ref<!l1> -> !l1
      %cr = simulation.ref.dyn_extract %r2 from %i : (!simulation.ref<!l64>, i32) -> !simulation.ref<!l1>
      %c = simulation.ref.load %cr : !simulation.ref<!l1> -> !l1
      %result = simulation.logic.logical and %a, %b : (!l1, !l1) -> !l1
      %dst = simulation.ref.dyn_extract %out from %i : (!simulation.ref<!l64>, i32) -> !simulation.ref<!l1>
      simulation.ref.store %result to %dst : !l1, !simulation.ref<!l1>
      %one = arith.constant 1 : i32
      %next = arith.addi %i, %one : i32
      cf.br ^head(%next : i32)
    ^exit:
      simulation.return
    }
    simulation.func @word_logical_or(%ctx: !simulation.context {simulation.capture_kind = 0 : i32}, %r0: !simulation.ref<!l64> {simulation.capture_kind = 3 : i32, simulation.descriptor_id = 0 : i64}, %r1: !simulation.ref<!l64> {simulation.capture_kind = 3 : i32, simulation.descriptor_id = 1 : i64}, %r2: !simulation.ref<!l64> {simulation.capture_kind = 3 : i32, simulation.descriptor_id = 2 : i64}, %out: !simulation.ref<!l64> {simulation.capture_kind = 3 : i32, simulation.descriptor_id = 4 : i64}) attributes {entry_kind = 8 : i32, code_unit_id = 35 : i64, schedule.eval.raw_captures} {
      %zero = arith.constant 0 : i32
      cf.br ^head(%zero : i32)
    ^head(%i: i32):
      %limit = arith.constant 64 : i32
      %more = arith.cmpi ult, %i, %limit : i32
      cf.cond_br %more, ^body, ^exit
    ^body:
      %ar = simulation.ref.dyn_extract %r0 from %i : (!simulation.ref<!l64>, i32) -> !simulation.ref<!l1>
      %a = simulation.ref.load %ar : !simulation.ref<!l1> -> !l1
      %br = simulation.ref.dyn_extract %r1 from %i : (!simulation.ref<!l64>, i32) -> !simulation.ref<!l1>
      %b = simulation.ref.load %br : !simulation.ref<!l1> -> !l1
      %cr = simulation.ref.dyn_extract %r2 from %i : (!simulation.ref<!l64>, i32) -> !simulation.ref<!l1>
      %c = simulation.ref.load %cr : !simulation.ref<!l1> -> !l1
      %result = simulation.logic.logical or %a, %b : (!l1, !l1) -> !l1
      %dst = simulation.ref.dyn_extract %out from %i : (!simulation.ref<!l64>, i32) -> !simulation.ref<!l1>
      simulation.ref.store %result to %dst : !l1, !simulation.ref<!l1>
      %one = arith.constant 1 : i32
      %next = arith.addi %i, %one : i32
      cf.br ^head(%next : i32)
    ^exit:
      simulation.return
    }
    simulation.func @word_reduction_and(%ctx: !simulation.context {simulation.capture_kind = 0 : i32}, %r0: !simulation.ref<!l64> {simulation.capture_kind = 3 : i32, simulation.descriptor_id = 0 : i64}, %r1: !simulation.ref<!l64> {simulation.capture_kind = 3 : i32, simulation.descriptor_id = 1 : i64}, %r2: !simulation.ref<!l64> {simulation.capture_kind = 3 : i32, simulation.descriptor_id = 2 : i64}, %out: !simulation.ref<!l64> {simulation.capture_kind = 3 : i32, simulation.descriptor_id = 4 : i64}) attributes {entry_kind = 8 : i32, code_unit_id = 37 : i64, schedule.eval.raw_captures} {
      %zero = arith.constant 0 : i32
      cf.br ^head(%zero : i32)
    ^head(%i: i32):
      %limit = arith.constant 64 : i32
      %more = arith.cmpi ult, %i, %limit : i32
      cf.cond_br %more, ^body, ^exit
    ^body:
      %ar = simulation.ref.dyn_extract %r0 from %i : (!simulation.ref<!l64>, i32) -> !simulation.ref<!l1>
      %a = simulation.ref.load %ar : !simulation.ref<!l1> -> !l1
      %br = simulation.ref.dyn_extract %r1 from %i : (!simulation.ref<!l64>, i32) -> !simulation.ref<!l1>
      %b = simulation.ref.load %br : !simulation.ref<!l1> -> !l1
      %cr = simulation.ref.dyn_extract %r2 from %i : (!simulation.ref<!l64>, i32) -> !simulation.ref<!l1>
      %c = simulation.ref.load %cr : !simulation.ref<!l1> -> !l1
      %result = simulation.logic.reduction and %a : !l1 -> !l1
      %dst = simulation.ref.dyn_extract %out from %i : (!simulation.ref<!l64>, i32) -> !simulation.ref<!l1>
      simulation.ref.store %result to %dst : !l1, !simulation.ref<!l1>
      %one = arith.constant 1 : i32
      %next = arith.addi %i, %one : i32
      cf.br ^head(%next : i32)
    ^exit:
      simulation.return
    }
    simulation.func @word_reduction_or(%ctx: !simulation.context {simulation.capture_kind = 0 : i32}, %r0: !simulation.ref<!l64> {simulation.capture_kind = 3 : i32, simulation.descriptor_id = 0 : i64}, %r1: !simulation.ref<!l64> {simulation.capture_kind = 3 : i32, simulation.descriptor_id = 1 : i64}, %r2: !simulation.ref<!l64> {simulation.capture_kind = 3 : i32, simulation.descriptor_id = 2 : i64}, %out: !simulation.ref<!l64> {simulation.capture_kind = 3 : i32, simulation.descriptor_id = 4 : i64}) attributes {entry_kind = 8 : i32, code_unit_id = 39 : i64, schedule.eval.raw_captures} {
      %zero = arith.constant 0 : i32
      cf.br ^head(%zero : i32)
    ^head(%i: i32):
      %limit = arith.constant 64 : i32
      %more = arith.cmpi ult, %i, %limit : i32
      cf.cond_br %more, ^body, ^exit
    ^body:
      %ar = simulation.ref.dyn_extract %r0 from %i : (!simulation.ref<!l64>, i32) -> !simulation.ref<!l1>
      %a = simulation.ref.load %ar : !simulation.ref<!l1> -> !l1
      %br = simulation.ref.dyn_extract %r1 from %i : (!simulation.ref<!l64>, i32) -> !simulation.ref<!l1>
      %b = simulation.ref.load %br : !simulation.ref<!l1> -> !l1
      %cr = simulation.ref.dyn_extract %r2 from %i : (!simulation.ref<!l64>, i32) -> !simulation.ref<!l1>
      %c = simulation.ref.load %cr : !simulation.ref<!l1> -> !l1
      %result = simulation.logic.reduction or %a : !l1 -> !l1
      %dst = simulation.ref.dyn_extract %out from %i : (!simulation.ref<!l64>, i32) -> !simulation.ref<!l1>
      simulation.ref.store %result to %dst : !l1, !simulation.ref<!l1>
      %one = arith.constant 1 : i32
      %next = arith.addi %i, %one : i32
      cf.br ^head(%next : i32)
    ^exit:
      simulation.return
    }
    simulation.func @word_reduction_xor(%ctx: !simulation.context {simulation.capture_kind = 0 : i32}, %r0: !simulation.ref<!l64> {simulation.capture_kind = 3 : i32, simulation.descriptor_id = 0 : i64}, %r1: !simulation.ref<!l64> {simulation.capture_kind = 3 : i32, simulation.descriptor_id = 1 : i64}, %r2: !simulation.ref<!l64> {simulation.capture_kind = 3 : i32, simulation.descriptor_id = 2 : i64}, %out: !simulation.ref<!l64> {simulation.capture_kind = 3 : i32, simulation.descriptor_id = 4 : i64}) attributes {entry_kind = 8 : i32, code_unit_id = 41 : i64, schedule.eval.raw_captures} {
      %zero = arith.constant 0 : i32
      cf.br ^head(%zero : i32)
    ^head(%i: i32):
      %limit = arith.constant 64 : i32
      %more = arith.cmpi ult, %i, %limit : i32
      cf.cond_br %more, ^body, ^exit
    ^body:
      %ar = simulation.ref.dyn_extract %r0 from %i : (!simulation.ref<!l64>, i32) -> !simulation.ref<!l1>
      %a = simulation.ref.load %ar : !simulation.ref<!l1> -> !l1
      %br = simulation.ref.dyn_extract %r1 from %i : (!simulation.ref<!l64>, i32) -> !simulation.ref<!l1>
      %b = simulation.ref.load %br : !simulation.ref<!l1> -> !l1
      %cr = simulation.ref.dyn_extract %r2 from %i : (!simulation.ref<!l64>, i32) -> !simulation.ref<!l1>
      %c = simulation.ref.load %cr : !simulation.ref<!l1> -> !l1
      %result = simulation.logic.reduction xor %a : !l1 -> !l1
      %dst = simulation.ref.dyn_extract %out from %i : (!simulation.ref<!l64>, i32) -> !simulation.ref<!l1>
      simulation.ref.store %result to %dst : !l1, !simulation.ref<!l1>
      %one = arith.constant 1 : i32
      %next = arith.addi %i, %one : i32
      cf.br ^head(%next : i32)
    ^exit:
      simulation.return
    }
    simulation.func @word_reduction_nand(%ctx: !simulation.context {simulation.capture_kind = 0 : i32}, %r0: !simulation.ref<!l64> {simulation.capture_kind = 3 : i32, simulation.descriptor_id = 0 : i64}, %r1: !simulation.ref<!l64> {simulation.capture_kind = 3 : i32, simulation.descriptor_id = 1 : i64}, %r2: !simulation.ref<!l64> {simulation.capture_kind = 3 : i32, simulation.descriptor_id = 2 : i64}, %out: !simulation.ref<!l64> {simulation.capture_kind = 3 : i32, simulation.descriptor_id = 4 : i64}) attributes {entry_kind = 8 : i32, code_unit_id = 43 : i64, schedule.eval.raw_captures} {
      %zero = arith.constant 0 : i32
      cf.br ^head(%zero : i32)
    ^head(%i: i32):
      %limit = arith.constant 64 : i32
      %more = arith.cmpi ult, %i, %limit : i32
      cf.cond_br %more, ^body, ^exit
    ^body:
      %ar = simulation.ref.dyn_extract %r0 from %i : (!simulation.ref<!l64>, i32) -> !simulation.ref<!l1>
      %a = simulation.ref.load %ar : !simulation.ref<!l1> -> !l1
      %br = simulation.ref.dyn_extract %r1 from %i : (!simulation.ref<!l64>, i32) -> !simulation.ref<!l1>
      %b = simulation.ref.load %br : !simulation.ref<!l1> -> !l1
      %cr = simulation.ref.dyn_extract %r2 from %i : (!simulation.ref<!l64>, i32) -> !simulation.ref<!l1>
      %c = simulation.ref.load %cr : !simulation.ref<!l1> -> !l1
      %result = simulation.logic.reduction nand %a : !l1 -> !l1
      %dst = simulation.ref.dyn_extract %out from %i : (!simulation.ref<!l64>, i32) -> !simulation.ref<!l1>
      simulation.ref.store %result to %dst : !l1, !simulation.ref<!l1>
      %one = arith.constant 1 : i32
      %next = arith.addi %i, %one : i32
      cf.br ^head(%next : i32)
    ^exit:
      simulation.return
    }
    simulation.func @word_reduction_nor(%ctx: !simulation.context {simulation.capture_kind = 0 : i32}, %r0: !simulation.ref<!l64> {simulation.capture_kind = 3 : i32, simulation.descriptor_id = 0 : i64}, %r1: !simulation.ref<!l64> {simulation.capture_kind = 3 : i32, simulation.descriptor_id = 1 : i64}, %r2: !simulation.ref<!l64> {simulation.capture_kind = 3 : i32, simulation.descriptor_id = 2 : i64}, %out: !simulation.ref<!l64> {simulation.capture_kind = 3 : i32, simulation.descriptor_id = 4 : i64}) attributes {entry_kind = 8 : i32, code_unit_id = 45 : i64, schedule.eval.raw_captures} {
      %zero = arith.constant 0 : i32
      cf.br ^head(%zero : i32)
    ^head(%i: i32):
      %limit = arith.constant 64 : i32
      %more = arith.cmpi ult, %i, %limit : i32
      cf.cond_br %more, ^body, ^exit
    ^body:
      %ar = simulation.ref.dyn_extract %r0 from %i : (!simulation.ref<!l64>, i32) -> !simulation.ref<!l1>
      %a = simulation.ref.load %ar : !simulation.ref<!l1> -> !l1
      %br = simulation.ref.dyn_extract %r1 from %i : (!simulation.ref<!l64>, i32) -> !simulation.ref<!l1>
      %b = simulation.ref.load %br : !simulation.ref<!l1> -> !l1
      %cr = simulation.ref.dyn_extract %r2 from %i : (!simulation.ref<!l64>, i32) -> !simulation.ref<!l1>
      %c = simulation.ref.load %cr : !simulation.ref<!l1> -> !l1
      %result = simulation.logic.reduction nor %a : !l1 -> !l1
      %dst = simulation.ref.dyn_extract %out from %i : (!simulation.ref<!l64>, i32) -> !simulation.ref<!l1>
      simulation.ref.store %result to %dst : !l1, !simulation.ref<!l1>
      %one = arith.constant 1 : i32
      %next = arith.addi %i, %one : i32
      cf.br ^head(%next : i32)
    ^exit:
      simulation.return
    }
    simulation.func @word_reduction_xnor(%ctx: !simulation.context {simulation.capture_kind = 0 : i32}, %r0: !simulation.ref<!l64> {simulation.capture_kind = 3 : i32, simulation.descriptor_id = 0 : i64}, %r1: !simulation.ref<!l64> {simulation.capture_kind = 3 : i32, simulation.descriptor_id = 1 : i64}, %r2: !simulation.ref<!l64> {simulation.capture_kind = 3 : i32, simulation.descriptor_id = 2 : i64}, %out: !simulation.ref<!l64> {simulation.capture_kind = 3 : i32, simulation.descriptor_id = 4 : i64}) attributes {entry_kind = 8 : i32, code_unit_id = 47 : i64, schedule.eval.raw_captures} {
      %zero = arith.constant 0 : i32
      cf.br ^head(%zero : i32)
    ^head(%i: i32):
      %limit = arith.constant 64 : i32
      %more = arith.cmpi ult, %i, %limit : i32
      cf.cond_br %more, ^body, ^exit
    ^body:
      %ar = simulation.ref.dyn_extract %r0 from %i : (!simulation.ref<!l64>, i32) -> !simulation.ref<!l1>
      %a = simulation.ref.load %ar : !simulation.ref<!l1> -> !l1
      %br = simulation.ref.dyn_extract %r1 from %i : (!simulation.ref<!l64>, i32) -> !simulation.ref<!l1>
      %b = simulation.ref.load %br : !simulation.ref<!l1> -> !l1
      %cr = simulation.ref.dyn_extract %r2 from %i : (!simulation.ref<!l64>, i32) -> !simulation.ref<!l1>
      %c = simulation.ref.load %cr : !simulation.ref<!l1> -> !l1
      %result = simulation.logic.reduction xnor %a : !l1 -> !l1
      %dst = simulation.ref.dyn_extract %out from %i : (!simulation.ref<!l64>, i32) -> !simulation.ref<!l1>
      simulation.ref.store %result to %dst : !l1, !simulation.ref<!l1>
      %one = arith.constant 1 : i32
      %next = arith.addi %i, %one : i32
      cf.br ^head(%next : i32)
    ^exit:
      simulation.return
    }
    simulation.func @word_compare_eq(%ctx: !simulation.context {simulation.capture_kind = 0 : i32}, %r0: !simulation.ref<!l64> {simulation.capture_kind = 3 : i32, simulation.descriptor_id = 0 : i64}, %r1: !simulation.ref<!l64> {simulation.capture_kind = 3 : i32, simulation.descriptor_id = 1 : i64}, %r2: !simulation.ref<!l64> {simulation.capture_kind = 3 : i32, simulation.descriptor_id = 2 : i64}, %out: !simulation.ref<!l64> {simulation.capture_kind = 3 : i32, simulation.descriptor_id = 4 : i64}) attributes {entry_kind = 8 : i32, code_unit_id = 49 : i64, schedule.eval.raw_captures} {
      %zero = arith.constant 0 : i32
      cf.br ^head(%zero : i32)
    ^head(%i: i32):
      %limit = arith.constant 64 : i32
      %more = arith.cmpi ult, %i, %limit : i32
      cf.cond_br %more, ^body, ^exit
    ^body:
      %ar = simulation.ref.dyn_extract %r0 from %i : (!simulation.ref<!l64>, i32) -> !simulation.ref<!l1>
      %a = simulation.ref.load %ar : !simulation.ref<!l1> -> !l1
      %br = simulation.ref.dyn_extract %r1 from %i : (!simulation.ref<!l64>, i32) -> !simulation.ref<!l1>
      %b = simulation.ref.load %br : !simulation.ref<!l1> -> !l1
      %cr = simulation.ref.dyn_extract %r2 from %i : (!simulation.ref<!l64>, i32) -> !simulation.ref<!l1>
      %c = simulation.ref.load %cr : !simulation.ref<!l1> -> !l1
      %result = simulation.logic.compare eq %a, %b : (!l1, !l1) -> !l1
      %dst = simulation.ref.dyn_extract %out from %i : (!simulation.ref<!l64>, i32) -> !simulation.ref<!l1>
      simulation.ref.store %result to %dst : !l1, !simulation.ref<!l1>
      %one = arith.constant 1 : i32
      %next = arith.addi %i, %one : i32
      cf.br ^head(%next : i32)
    ^exit:
      simulation.return
    }
    simulation.func @word_compare_ne(%ctx: !simulation.context {simulation.capture_kind = 0 : i32}, %r0: !simulation.ref<!l64> {simulation.capture_kind = 3 : i32, simulation.descriptor_id = 0 : i64}, %r1: !simulation.ref<!l64> {simulation.capture_kind = 3 : i32, simulation.descriptor_id = 1 : i64}, %r2: !simulation.ref<!l64> {simulation.capture_kind = 3 : i32, simulation.descriptor_id = 2 : i64}, %out: !simulation.ref<!l64> {simulation.capture_kind = 3 : i32, simulation.descriptor_id = 4 : i64}) attributes {entry_kind = 8 : i32, code_unit_id = 51 : i64, schedule.eval.raw_captures} {
      %zero = arith.constant 0 : i32
      cf.br ^head(%zero : i32)
    ^head(%i: i32):
      %limit = arith.constant 64 : i32
      %more = arith.cmpi ult, %i, %limit : i32
      cf.cond_br %more, ^body, ^exit
    ^body:
      %ar = simulation.ref.dyn_extract %r0 from %i : (!simulation.ref<!l64>, i32) -> !simulation.ref<!l1>
      %a = simulation.ref.load %ar : !simulation.ref<!l1> -> !l1
      %br = simulation.ref.dyn_extract %r1 from %i : (!simulation.ref<!l64>, i32) -> !simulation.ref<!l1>
      %b = simulation.ref.load %br : !simulation.ref<!l1> -> !l1
      %cr = simulation.ref.dyn_extract %r2 from %i : (!simulation.ref<!l64>, i32) -> !simulation.ref<!l1>
      %c = simulation.ref.load %cr : !simulation.ref<!l1> -> !l1
      %result = simulation.logic.compare ne %a, %b : (!l1, !l1) -> !l1
      %dst = simulation.ref.dyn_extract %out from %i : (!simulation.ref<!l64>, i32) -> !simulation.ref<!l1>
      simulation.ref.store %result to %dst : !l1, !simulation.ref<!l1>
      %one = arith.constant 1 : i32
      %next = arith.addi %i, %one : i32
      cf.br ^head(%next : i32)
    ^exit:
      simulation.return
    }
    simulation.func @word_compare_case_eq(%ctx: !simulation.context {simulation.capture_kind = 0 : i32}, %r0: !simulation.ref<!l64> {simulation.capture_kind = 3 : i32, simulation.descriptor_id = 0 : i64}, %r1: !simulation.ref<!l64> {simulation.capture_kind = 3 : i32, simulation.descriptor_id = 1 : i64}, %r2: !simulation.ref<!l64> {simulation.capture_kind = 3 : i32, simulation.descriptor_id = 2 : i64}, %out: !simulation.ref<i64> {simulation.capture_kind = 3 : i32, simulation.descriptor_id = 6 : i64}) attributes {entry_kind = 8 : i32, code_unit_id = 53 : i64, schedule.eval.raw_captures} {
      %zero = arith.constant 0 : i32
      cf.br ^head(%zero : i32)
    ^head(%i: i32):
      %limit = arith.constant 64 : i32
      %more = arith.cmpi ult, %i, %limit : i32
      cf.cond_br %more, ^body, ^exit
    ^body:
      %ar = simulation.ref.dyn_extract %r0 from %i : (!simulation.ref<!l64>, i32) -> !simulation.ref<!l1>
      %a = simulation.ref.load %ar : !simulation.ref<!l1> -> !l1
      %br = simulation.ref.dyn_extract %r1 from %i : (!simulation.ref<!l64>, i32) -> !simulation.ref<!l1>
      %b = simulation.ref.load %br : !simulation.ref<!l1> -> !l1
      %cr = simulation.ref.dyn_extract %r2 from %i : (!simulation.ref<!l64>, i32) -> !simulation.ref<!l1>
      %c = simulation.ref.load %cr : !simulation.ref<!l1> -> !l1
      %result = simulation.logic.compare case_eq %a, %b : (!l1, !l1) -> i1
      %dst = simulation.ref.dyn_extract %out from %i : (!simulation.ref<i64>, i32) -> !simulation.ref<i1>
      simulation.ref.store %result to %dst : i1, !simulation.ref<i1>
      %one = arith.constant 1 : i32
      %next = arith.addi %i, %one : i32
      cf.br ^head(%next : i32)
    ^exit:
      simulation.return
    }
    simulation.func @word_compare_case_ne(%ctx: !simulation.context {simulation.capture_kind = 0 : i32}, %r0: !simulation.ref<!l64> {simulation.capture_kind = 3 : i32, simulation.descriptor_id = 0 : i64}, %r1: !simulation.ref<!l64> {simulation.capture_kind = 3 : i32, simulation.descriptor_id = 1 : i64}, %r2: !simulation.ref<!l64> {simulation.capture_kind = 3 : i32, simulation.descriptor_id = 2 : i64}, %out: !simulation.ref<i64> {simulation.capture_kind = 3 : i32, simulation.descriptor_id = 6 : i64}) attributes {entry_kind = 8 : i32, code_unit_id = 55 : i64, schedule.eval.raw_captures} {
      %zero = arith.constant 0 : i32
      cf.br ^head(%zero : i32)
    ^head(%i: i32):
      %limit = arith.constant 64 : i32
      %more = arith.cmpi ult, %i, %limit : i32
      cf.cond_br %more, ^body, ^exit
    ^body:
      %ar = simulation.ref.dyn_extract %r0 from %i : (!simulation.ref<!l64>, i32) -> !simulation.ref<!l1>
      %a = simulation.ref.load %ar : !simulation.ref<!l1> -> !l1
      %br = simulation.ref.dyn_extract %r1 from %i : (!simulation.ref<!l64>, i32) -> !simulation.ref<!l1>
      %b = simulation.ref.load %br : !simulation.ref<!l1> -> !l1
      %cr = simulation.ref.dyn_extract %r2 from %i : (!simulation.ref<!l64>, i32) -> !simulation.ref<!l1>
      %c = simulation.ref.load %cr : !simulation.ref<!l1> -> !l1
      %result = simulation.logic.compare case_ne %a, %b : (!l1, !l1) -> i1
      %dst = simulation.ref.dyn_extract %out from %i : (!simulation.ref<i64>, i32) -> !simulation.ref<i1>
      simulation.ref.store %result to %dst : i1, !simulation.ref<i1>
      %one = arith.constant 1 : i32
      %next = arith.addi %i, %one : i32
      cf.br ^head(%next : i32)
    ^exit:
      simulation.return
    }
    simulation.func @word_compare_ult(%ctx: !simulation.context {simulation.capture_kind = 0 : i32}, %r0: !simulation.ref<!l64> {simulation.capture_kind = 3 : i32, simulation.descriptor_id = 0 : i64}, %r1: !simulation.ref<!l64> {simulation.capture_kind = 3 : i32, simulation.descriptor_id = 1 : i64}, %r2: !simulation.ref<!l64> {simulation.capture_kind = 3 : i32, simulation.descriptor_id = 2 : i64}, %out: !simulation.ref<!l64> {simulation.capture_kind = 3 : i32, simulation.descriptor_id = 4 : i64}) attributes {entry_kind = 8 : i32, code_unit_id = 57 : i64, schedule.eval.raw_captures} {
      %zero = arith.constant 0 : i32
      cf.br ^head(%zero : i32)
    ^head(%i: i32):
      %limit = arith.constant 64 : i32
      %more = arith.cmpi ult, %i, %limit : i32
      cf.cond_br %more, ^body, ^exit
    ^body:
      %ar = simulation.ref.dyn_extract %r0 from %i : (!simulation.ref<!l64>, i32) -> !simulation.ref<!l1>
      %a = simulation.ref.load %ar : !simulation.ref<!l1> -> !l1
      %br = simulation.ref.dyn_extract %r1 from %i : (!simulation.ref<!l64>, i32) -> !simulation.ref<!l1>
      %b = simulation.ref.load %br : !simulation.ref<!l1> -> !l1
      %cr = simulation.ref.dyn_extract %r2 from %i : (!simulation.ref<!l64>, i32) -> !simulation.ref<!l1>
      %c = simulation.ref.load %cr : !simulation.ref<!l1> -> !l1
      %result = simulation.logic.compare ult %a, %b : (!l1, !l1) -> !l1
      %dst = simulation.ref.dyn_extract %out from %i : (!simulation.ref<!l64>, i32) -> !simulation.ref<!l1>
      simulation.ref.store %result to %dst : !l1, !simulation.ref<!l1>
      %one = arith.constant 1 : i32
      %next = arith.addi %i, %one : i32
      cf.br ^head(%next : i32)
    ^exit:
      simulation.return
    }
    simulation.func @word_compare_ule(%ctx: !simulation.context {simulation.capture_kind = 0 : i32}, %r0: !simulation.ref<!l64> {simulation.capture_kind = 3 : i32, simulation.descriptor_id = 0 : i64}, %r1: !simulation.ref<!l64> {simulation.capture_kind = 3 : i32, simulation.descriptor_id = 1 : i64}, %r2: !simulation.ref<!l64> {simulation.capture_kind = 3 : i32, simulation.descriptor_id = 2 : i64}, %out: !simulation.ref<!l64> {simulation.capture_kind = 3 : i32, simulation.descriptor_id = 4 : i64}) attributes {entry_kind = 8 : i32, code_unit_id = 59 : i64, schedule.eval.raw_captures} {
      %zero = arith.constant 0 : i32
      cf.br ^head(%zero : i32)
    ^head(%i: i32):
      %limit = arith.constant 64 : i32
      %more = arith.cmpi ult, %i, %limit : i32
      cf.cond_br %more, ^body, ^exit
    ^body:
      %ar = simulation.ref.dyn_extract %r0 from %i : (!simulation.ref<!l64>, i32) -> !simulation.ref<!l1>
      %a = simulation.ref.load %ar : !simulation.ref<!l1> -> !l1
      %br = simulation.ref.dyn_extract %r1 from %i : (!simulation.ref<!l64>, i32) -> !simulation.ref<!l1>
      %b = simulation.ref.load %br : !simulation.ref<!l1> -> !l1
      %cr = simulation.ref.dyn_extract %r2 from %i : (!simulation.ref<!l64>, i32) -> !simulation.ref<!l1>
      %c = simulation.ref.load %cr : !simulation.ref<!l1> -> !l1
      %result = simulation.logic.compare ule %a, %b : (!l1, !l1) -> !l1
      %dst = simulation.ref.dyn_extract %out from %i : (!simulation.ref<!l64>, i32) -> !simulation.ref<!l1>
      simulation.ref.store %result to %dst : !l1, !simulation.ref<!l1>
      %one = arith.constant 1 : i32
      %next = arith.addi %i, %one : i32
      cf.br ^head(%next : i32)
    ^exit:
      simulation.return
    }
    simulation.func @word_compare_ugt(%ctx: !simulation.context {simulation.capture_kind = 0 : i32}, %r0: !simulation.ref<!l64> {simulation.capture_kind = 3 : i32, simulation.descriptor_id = 0 : i64}, %r1: !simulation.ref<!l64> {simulation.capture_kind = 3 : i32, simulation.descriptor_id = 1 : i64}, %r2: !simulation.ref<!l64> {simulation.capture_kind = 3 : i32, simulation.descriptor_id = 2 : i64}, %out: !simulation.ref<!l64> {simulation.capture_kind = 3 : i32, simulation.descriptor_id = 4 : i64}) attributes {entry_kind = 8 : i32, code_unit_id = 61 : i64, schedule.eval.raw_captures} {
      %zero = arith.constant 0 : i32
      cf.br ^head(%zero : i32)
    ^head(%i: i32):
      %limit = arith.constant 64 : i32
      %more = arith.cmpi ult, %i, %limit : i32
      cf.cond_br %more, ^body, ^exit
    ^body:
      %ar = simulation.ref.dyn_extract %r0 from %i : (!simulation.ref<!l64>, i32) -> !simulation.ref<!l1>
      %a = simulation.ref.load %ar : !simulation.ref<!l1> -> !l1
      %br = simulation.ref.dyn_extract %r1 from %i : (!simulation.ref<!l64>, i32) -> !simulation.ref<!l1>
      %b = simulation.ref.load %br : !simulation.ref<!l1> -> !l1
      %cr = simulation.ref.dyn_extract %r2 from %i : (!simulation.ref<!l64>, i32) -> !simulation.ref<!l1>
      %c = simulation.ref.load %cr : !simulation.ref<!l1> -> !l1
      %result = simulation.logic.compare ugt %a, %b : (!l1, !l1) -> !l1
      %dst = simulation.ref.dyn_extract %out from %i : (!simulation.ref<!l64>, i32) -> !simulation.ref<!l1>
      simulation.ref.store %result to %dst : !l1, !simulation.ref<!l1>
      %one = arith.constant 1 : i32
      %next = arith.addi %i, %one : i32
      cf.br ^head(%next : i32)
    ^exit:
      simulation.return
    }
    simulation.func @word_compare_uge(%ctx: !simulation.context {simulation.capture_kind = 0 : i32}, %r0: !simulation.ref<!l64> {simulation.capture_kind = 3 : i32, simulation.descriptor_id = 0 : i64}, %r1: !simulation.ref<!l64> {simulation.capture_kind = 3 : i32, simulation.descriptor_id = 1 : i64}, %r2: !simulation.ref<!l64> {simulation.capture_kind = 3 : i32, simulation.descriptor_id = 2 : i64}, %out: !simulation.ref<!l64> {simulation.capture_kind = 3 : i32, simulation.descriptor_id = 4 : i64}) attributes {entry_kind = 8 : i32, code_unit_id = 63 : i64, schedule.eval.raw_captures} {
      %zero = arith.constant 0 : i32
      cf.br ^head(%zero : i32)
    ^head(%i: i32):
      %limit = arith.constant 64 : i32
      %more = arith.cmpi ult, %i, %limit : i32
      cf.cond_br %more, ^body, ^exit
    ^body:
      %ar = simulation.ref.dyn_extract %r0 from %i : (!simulation.ref<!l64>, i32) -> !simulation.ref<!l1>
      %a = simulation.ref.load %ar : !simulation.ref<!l1> -> !l1
      %br = simulation.ref.dyn_extract %r1 from %i : (!simulation.ref<!l64>, i32) -> !simulation.ref<!l1>
      %b = simulation.ref.load %br : !simulation.ref<!l1> -> !l1
      %cr = simulation.ref.dyn_extract %r2 from %i : (!simulation.ref<!l64>, i32) -> !simulation.ref<!l1>
      %c = simulation.ref.load %cr : !simulation.ref<!l1> -> !l1
      %result = simulation.logic.compare uge %a, %b : (!l1, !l1) -> !l1
      %dst = simulation.ref.dyn_extract %out from %i : (!simulation.ref<!l64>, i32) -> !simulation.ref<!l1>
      simulation.ref.store %result to %dst : !l1, !simulation.ref<!l1>
      %one = arith.constant 1 : i32
      %next = arith.addi %i, %one : i32
      cf.br ^head(%next : i32)
    ^exit:
      simulation.return
    }
    simulation.func @word_compare_slt(%ctx: !simulation.context {simulation.capture_kind = 0 : i32}, %r0: !simulation.ref<!l64> {simulation.capture_kind = 3 : i32, simulation.descriptor_id = 0 : i64}, %r1: !simulation.ref<!l64> {simulation.capture_kind = 3 : i32, simulation.descriptor_id = 1 : i64}, %r2: !simulation.ref<!l64> {simulation.capture_kind = 3 : i32, simulation.descriptor_id = 2 : i64}, %out: !simulation.ref<!l64> {simulation.capture_kind = 3 : i32, simulation.descriptor_id = 4 : i64}) attributes {entry_kind = 8 : i32, code_unit_id = 65 : i64, schedule.eval.raw_captures} {
      %zero = arith.constant 0 : i32
      cf.br ^head(%zero : i32)
    ^head(%i: i32):
      %limit = arith.constant 64 : i32
      %more = arith.cmpi ult, %i, %limit : i32
      cf.cond_br %more, ^body, ^exit
    ^body:
      %ar = simulation.ref.dyn_extract %r0 from %i : (!simulation.ref<!l64>, i32) -> !simulation.ref<!l1>
      %a = simulation.ref.load %ar : !simulation.ref<!l1> -> !l1
      %br = simulation.ref.dyn_extract %r1 from %i : (!simulation.ref<!l64>, i32) -> !simulation.ref<!l1>
      %b = simulation.ref.load %br : !simulation.ref<!l1> -> !l1
      %cr = simulation.ref.dyn_extract %r2 from %i : (!simulation.ref<!l64>, i32) -> !simulation.ref<!l1>
      %c = simulation.ref.load %cr : !simulation.ref<!l1> -> !l1
      %result = simulation.logic.compare slt %a, %b : (!l1, !l1) -> !l1
      %dst = simulation.ref.dyn_extract %out from %i : (!simulation.ref<!l64>, i32) -> !simulation.ref<!l1>
      simulation.ref.store %result to %dst : !l1, !simulation.ref<!l1>
      %one = arith.constant 1 : i32
      %next = arith.addi %i, %one : i32
      cf.br ^head(%next : i32)
    ^exit:
      simulation.return
    }
    simulation.func @word_compare_sle(%ctx: !simulation.context {simulation.capture_kind = 0 : i32}, %r0: !simulation.ref<!l64> {simulation.capture_kind = 3 : i32, simulation.descriptor_id = 0 : i64}, %r1: !simulation.ref<!l64> {simulation.capture_kind = 3 : i32, simulation.descriptor_id = 1 : i64}, %r2: !simulation.ref<!l64> {simulation.capture_kind = 3 : i32, simulation.descriptor_id = 2 : i64}, %out: !simulation.ref<!l64> {simulation.capture_kind = 3 : i32, simulation.descriptor_id = 4 : i64}) attributes {entry_kind = 8 : i32, code_unit_id = 67 : i64, schedule.eval.raw_captures} {
      %zero = arith.constant 0 : i32
      cf.br ^head(%zero : i32)
    ^head(%i: i32):
      %limit = arith.constant 64 : i32
      %more = arith.cmpi ult, %i, %limit : i32
      cf.cond_br %more, ^body, ^exit
    ^body:
      %ar = simulation.ref.dyn_extract %r0 from %i : (!simulation.ref<!l64>, i32) -> !simulation.ref<!l1>
      %a = simulation.ref.load %ar : !simulation.ref<!l1> -> !l1
      %br = simulation.ref.dyn_extract %r1 from %i : (!simulation.ref<!l64>, i32) -> !simulation.ref<!l1>
      %b = simulation.ref.load %br : !simulation.ref<!l1> -> !l1
      %cr = simulation.ref.dyn_extract %r2 from %i : (!simulation.ref<!l64>, i32) -> !simulation.ref<!l1>
      %c = simulation.ref.load %cr : !simulation.ref<!l1> -> !l1
      %result = simulation.logic.compare sle %a, %b : (!l1, !l1) -> !l1
      %dst = simulation.ref.dyn_extract %out from %i : (!simulation.ref<!l64>, i32) -> !simulation.ref<!l1>
      simulation.ref.store %result to %dst : !l1, !simulation.ref<!l1>
      %one = arith.constant 1 : i32
      %next = arith.addi %i, %one : i32
      cf.br ^head(%next : i32)
    ^exit:
      simulation.return
    }
    simulation.func @word_compare_sgt(%ctx: !simulation.context {simulation.capture_kind = 0 : i32}, %r0: !simulation.ref<!l64> {simulation.capture_kind = 3 : i32, simulation.descriptor_id = 0 : i64}, %r1: !simulation.ref<!l64> {simulation.capture_kind = 3 : i32, simulation.descriptor_id = 1 : i64}, %r2: !simulation.ref<!l64> {simulation.capture_kind = 3 : i32, simulation.descriptor_id = 2 : i64}, %out: !simulation.ref<!l64> {simulation.capture_kind = 3 : i32, simulation.descriptor_id = 4 : i64}) attributes {entry_kind = 8 : i32, code_unit_id = 69 : i64, schedule.eval.raw_captures} {
      %zero = arith.constant 0 : i32
      cf.br ^head(%zero : i32)
    ^head(%i: i32):
      %limit = arith.constant 64 : i32
      %more = arith.cmpi ult, %i, %limit : i32
      cf.cond_br %more, ^body, ^exit
    ^body:
      %ar = simulation.ref.dyn_extract %r0 from %i : (!simulation.ref<!l64>, i32) -> !simulation.ref<!l1>
      %a = simulation.ref.load %ar : !simulation.ref<!l1> -> !l1
      %br = simulation.ref.dyn_extract %r1 from %i : (!simulation.ref<!l64>, i32) -> !simulation.ref<!l1>
      %b = simulation.ref.load %br : !simulation.ref<!l1> -> !l1
      %cr = simulation.ref.dyn_extract %r2 from %i : (!simulation.ref<!l64>, i32) -> !simulation.ref<!l1>
      %c = simulation.ref.load %cr : !simulation.ref<!l1> -> !l1
      %result = simulation.logic.compare sgt %a, %b : (!l1, !l1) -> !l1
      %dst = simulation.ref.dyn_extract %out from %i : (!simulation.ref<!l64>, i32) -> !simulation.ref<!l1>
      simulation.ref.store %result to %dst : !l1, !simulation.ref<!l1>
      %one = arith.constant 1 : i32
      %next = arith.addi %i, %one : i32
      cf.br ^head(%next : i32)
    ^exit:
      simulation.return
    }
    simulation.func @word_compare_sge(%ctx: !simulation.context {simulation.capture_kind = 0 : i32}, %r0: !simulation.ref<!l64> {simulation.capture_kind = 3 : i32, simulation.descriptor_id = 0 : i64}, %r1: !simulation.ref<!l64> {simulation.capture_kind = 3 : i32, simulation.descriptor_id = 1 : i64}, %r2: !simulation.ref<!l64> {simulation.capture_kind = 3 : i32, simulation.descriptor_id = 2 : i64}, %out: !simulation.ref<!l64> {simulation.capture_kind = 3 : i32, simulation.descriptor_id = 4 : i64}) attributes {entry_kind = 8 : i32, code_unit_id = 71 : i64, schedule.eval.raw_captures} {
      %zero = arith.constant 0 : i32
      cf.br ^head(%zero : i32)
    ^head(%i: i32):
      %limit = arith.constant 64 : i32
      %more = arith.cmpi ult, %i, %limit : i32
      cf.cond_br %more, ^body, ^exit
    ^body:
      %ar = simulation.ref.dyn_extract %r0 from %i : (!simulation.ref<!l64>, i32) -> !simulation.ref<!l1>
      %a = simulation.ref.load %ar : !simulation.ref<!l1> -> !l1
      %br = simulation.ref.dyn_extract %r1 from %i : (!simulation.ref<!l64>, i32) -> !simulation.ref<!l1>
      %b = simulation.ref.load %br : !simulation.ref<!l1> -> !l1
      %cr = simulation.ref.dyn_extract %r2 from %i : (!simulation.ref<!l64>, i32) -> !simulation.ref<!l1>
      %c = simulation.ref.load %cr : !simulation.ref<!l1> -> !l1
      %result = simulation.logic.compare sge %a, %b : (!l1, !l1) -> !l1
      %dst = simulation.ref.dyn_extract %out from %i : (!simulation.ref<!l64>, i32) -> !simulation.ref<!l1>
      simulation.ref.store %result to %dst : !l1, !simulation.ref<!l1>
      %one = arith.constant 1 : i32
      %next = arith.addi %i, %one : i32
      cf.br ^head(%next : i32)
    ^exit:
      simulation.return
    }
    simulation.func @word_compare_wild_eq(%ctx: !simulation.context {simulation.capture_kind = 0 : i32}, %r0: !simulation.ref<!l64> {simulation.capture_kind = 3 : i32, simulation.descriptor_id = 0 : i64}, %r1: !simulation.ref<!l64> {simulation.capture_kind = 3 : i32, simulation.descriptor_id = 1 : i64}, %r2: !simulation.ref<!l64> {simulation.capture_kind = 3 : i32, simulation.descriptor_id = 2 : i64}, %out: !simulation.ref<!l64> {simulation.capture_kind = 3 : i32, simulation.descriptor_id = 4 : i64}) attributes {entry_kind = 8 : i32, code_unit_id = 73 : i64, schedule.eval.raw_captures} {
      %zero = arith.constant 0 : i32
      cf.br ^head(%zero : i32)
    ^head(%i: i32):
      %limit = arith.constant 64 : i32
      %more = arith.cmpi ult, %i, %limit : i32
      cf.cond_br %more, ^body, ^exit
    ^body:
      %ar = simulation.ref.dyn_extract %r0 from %i : (!simulation.ref<!l64>, i32) -> !simulation.ref<!l1>
      %a = simulation.ref.load %ar : !simulation.ref<!l1> -> !l1
      %br = simulation.ref.dyn_extract %r1 from %i : (!simulation.ref<!l64>, i32) -> !simulation.ref<!l1>
      %b = simulation.ref.load %br : !simulation.ref<!l1> -> !l1
      %cr = simulation.ref.dyn_extract %r2 from %i : (!simulation.ref<!l64>, i32) -> !simulation.ref<!l1>
      %c = simulation.ref.load %cr : !simulation.ref<!l1> -> !l1
      %result = simulation.logic.compare wild_eq %a, %b : (!l1, !l1) -> !l1
      %dst = simulation.ref.dyn_extract %out from %i : (!simulation.ref<!l64>, i32) -> !simulation.ref<!l1>
      simulation.ref.store %result to %dst : !l1, !simulation.ref<!l1>
      %one = arith.constant 1 : i32
      %next = arith.addi %i, %one : i32
      cf.br ^head(%next : i32)
    ^exit:
      simulation.return
    }
    simulation.func @word_compare_wild_ne(%ctx: !simulation.context {simulation.capture_kind = 0 : i32}, %r0: !simulation.ref<!l64> {simulation.capture_kind = 3 : i32, simulation.descriptor_id = 0 : i64}, %r1: !simulation.ref<!l64> {simulation.capture_kind = 3 : i32, simulation.descriptor_id = 1 : i64}, %r2: !simulation.ref<!l64> {simulation.capture_kind = 3 : i32, simulation.descriptor_id = 2 : i64}, %out: !simulation.ref<!l64> {simulation.capture_kind = 3 : i32, simulation.descriptor_id = 4 : i64}) attributes {entry_kind = 8 : i32, code_unit_id = 75 : i64, schedule.eval.raw_captures} {
      %zero = arith.constant 0 : i32
      cf.br ^head(%zero : i32)
    ^head(%i: i32):
      %limit = arith.constant 64 : i32
      %more = arith.cmpi ult, %i, %limit : i32
      cf.cond_br %more, ^body, ^exit
    ^body:
      %ar = simulation.ref.dyn_extract %r0 from %i : (!simulation.ref<!l64>, i32) -> !simulation.ref<!l1>
      %a = simulation.ref.load %ar : !simulation.ref<!l1> -> !l1
      %br = simulation.ref.dyn_extract %r1 from %i : (!simulation.ref<!l64>, i32) -> !simulation.ref<!l1>
      %b = simulation.ref.load %br : !simulation.ref<!l1> -> !l1
      %cr = simulation.ref.dyn_extract %r2 from %i : (!simulation.ref<!l64>, i32) -> !simulation.ref<!l1>
      %c = simulation.ref.load %cr : !simulation.ref<!l1> -> !l1
      %result = simulation.logic.compare wild_ne %a, %b : (!l1, !l1) -> !l1
      %dst = simulation.ref.dyn_extract %out from %i : (!simulation.ref<!l64>, i32) -> !simulation.ref<!l1>
      simulation.ref.store %result to %dst : !l1, !simulation.ref<!l1>
      %one = arith.constant 1 : i32
      %next = arith.addi %i, %one : i32
      cf.br ^head(%next : i32)
    ^exit:
      simulation.return
    }
    simulation.func @word_compare_casez_eq(%ctx: !simulation.context {simulation.capture_kind = 0 : i32}, %r0: !simulation.ref<!l64> {simulation.capture_kind = 3 : i32, simulation.descriptor_id = 0 : i64}, %r1: !simulation.ref<!l64> {simulation.capture_kind = 3 : i32, simulation.descriptor_id = 1 : i64}, %r2: !simulation.ref<!l64> {simulation.capture_kind = 3 : i32, simulation.descriptor_id = 2 : i64}, %out: !simulation.ref<i64> {simulation.capture_kind = 3 : i32, simulation.descriptor_id = 6 : i64}) attributes {entry_kind = 8 : i32, code_unit_id = 77 : i64, schedule.eval.raw_captures} {
      %zero = arith.constant 0 : i32
      cf.br ^head(%zero : i32)
    ^head(%i: i32):
      %limit = arith.constant 64 : i32
      %more = arith.cmpi ult, %i, %limit : i32
      cf.cond_br %more, ^body, ^exit
    ^body:
      %ar = simulation.ref.dyn_extract %r0 from %i : (!simulation.ref<!l64>, i32) -> !simulation.ref<!l1>
      %a = simulation.ref.load %ar : !simulation.ref<!l1> -> !l1
      %br = simulation.ref.dyn_extract %r1 from %i : (!simulation.ref<!l64>, i32) -> !simulation.ref<!l1>
      %b = simulation.ref.load %br : !simulation.ref<!l1> -> !l1
      %cr = simulation.ref.dyn_extract %r2 from %i : (!simulation.ref<!l64>, i32) -> !simulation.ref<!l1>
      %c = simulation.ref.load %cr : !simulation.ref<!l1> -> !l1
      %result = simulation.logic.compare casez_eq %a, %b : (!l1, !l1) -> i1
      %dst = simulation.ref.dyn_extract %out from %i : (!simulation.ref<i64>, i32) -> !simulation.ref<i1>
      simulation.ref.store %result to %dst : i1, !simulation.ref<i1>
      %one = arith.constant 1 : i32
      %next = arith.addi %i, %one : i32
      cf.br ^head(%next : i32)
    ^exit:
      simulation.return
    }
    simulation.func @word_compare_casexz_eq(%ctx: !simulation.context {simulation.capture_kind = 0 : i32}, %r0: !simulation.ref<!l64> {simulation.capture_kind = 3 : i32, simulation.descriptor_id = 0 : i64}, %r1: !simulation.ref<!l64> {simulation.capture_kind = 3 : i32, simulation.descriptor_id = 1 : i64}, %r2: !simulation.ref<!l64> {simulation.capture_kind = 3 : i32, simulation.descriptor_id = 2 : i64}, %out: !simulation.ref<i64> {simulation.capture_kind = 3 : i32, simulation.descriptor_id = 6 : i64}) attributes {entry_kind = 8 : i32, code_unit_id = 79 : i64, schedule.eval.raw_captures} {
      %zero = arith.constant 0 : i32
      cf.br ^head(%zero : i32)
    ^head(%i: i32):
      %limit = arith.constant 64 : i32
      %more = arith.cmpi ult, %i, %limit : i32
      cf.cond_br %more, ^body, ^exit
    ^body:
      %ar = simulation.ref.dyn_extract %r0 from %i : (!simulation.ref<!l64>, i32) -> !simulation.ref<!l1>
      %a = simulation.ref.load %ar : !simulation.ref<!l1> -> !l1
      %br = simulation.ref.dyn_extract %r1 from %i : (!simulation.ref<!l64>, i32) -> !simulation.ref<!l1>
      %b = simulation.ref.load %br : !simulation.ref<!l1> -> !l1
      %cr = simulation.ref.dyn_extract %r2 from %i : (!simulation.ref<!l64>, i32) -> !simulation.ref<!l1>
      %c = simulation.ref.load %cr : !simulation.ref<!l1> -> !l1
      %result = simulation.logic.compare casexz_eq %a, %b : (!l1, !l1) -> i1
      %dst = simulation.ref.dyn_extract %out from %i : (!simulation.ref<i64>, i32) -> !simulation.ref<i1>
      simulation.ref.store %result to %dst : i1, !simulation.ref<i1>
      %one = arith.constant 1 : i32
      %next = arith.addi %i, %one : i32
      cf.br ^head(%next : i32)
    ^exit:
      simulation.return
    }
    simulation.func @word_shift_left(%ctx: !simulation.context {simulation.capture_kind = 0 : i32}, %r0: !simulation.ref<!l64> {simulation.capture_kind = 3 : i32, simulation.descriptor_id = 0 : i64}, %r1: !simulation.ref<!l64> {simulation.capture_kind = 3 : i32, simulation.descriptor_id = 1 : i64}, %r2: !simulation.ref<!l64> {simulation.capture_kind = 3 : i32, simulation.descriptor_id = 2 : i64}, %out: !simulation.ref<!l64> {simulation.capture_kind = 3 : i32, simulation.descriptor_id = 4 : i64}) attributes {entry_kind = 8 : i32, code_unit_id = 81 : i64, schedule.eval.raw_captures} {
      %zero = arith.constant 0 : i32
      cf.br ^head(%zero : i32)
    ^head(%i: i32):
      %limit = arith.constant 64 : i32
      %more = arith.cmpi ult, %i, %limit : i32
      cf.cond_br %more, ^body, ^exit
    ^body:
      %ar = simulation.ref.dyn_extract %r0 from %i : (!simulation.ref<!l64>, i32) -> !simulation.ref<!l1>
      %a = simulation.ref.load %ar : !simulation.ref<!l1> -> !l1
      %br = simulation.ref.dyn_extract %r1 from %i : (!simulation.ref<!l64>, i32) -> !simulation.ref<!l1>
      %b = simulation.ref.load %br : !simulation.ref<!l1> -> !l1
      %cr = simulation.ref.dyn_extract %r2 from %i : (!simulation.ref<!l64>, i32) -> !simulation.ref<!l1>
      %c = simulation.ref.load %cr : !simulation.ref<!l1> -> !l1
      %result = simulation.logic.shift left %a by %b : (!l1, !l1) -> !l1
      %dst = simulation.ref.dyn_extract %out from %i : (!simulation.ref<!l64>, i32) -> !simulation.ref<!l1>
      simulation.ref.store %result to %dst : !l1, !simulation.ref<!l1>
      %one = arith.constant 1 : i32
      %next = arith.addi %i, %one : i32
      cf.br ^head(%next : i32)
    ^exit:
      simulation.return
    }
    simulation.func @word_shift_right(%ctx: !simulation.context {simulation.capture_kind = 0 : i32}, %r0: !simulation.ref<!l64> {simulation.capture_kind = 3 : i32, simulation.descriptor_id = 0 : i64}, %r1: !simulation.ref<!l64> {simulation.capture_kind = 3 : i32, simulation.descriptor_id = 1 : i64}, %r2: !simulation.ref<!l64> {simulation.capture_kind = 3 : i32, simulation.descriptor_id = 2 : i64}, %out: !simulation.ref<!l64> {simulation.capture_kind = 3 : i32, simulation.descriptor_id = 4 : i64}) attributes {entry_kind = 8 : i32, code_unit_id = 83 : i64, schedule.eval.raw_captures} {
      %zero = arith.constant 0 : i32
      cf.br ^head(%zero : i32)
    ^head(%i: i32):
      %limit = arith.constant 64 : i32
      %more = arith.cmpi ult, %i, %limit : i32
      cf.cond_br %more, ^body, ^exit
    ^body:
      %ar = simulation.ref.dyn_extract %r0 from %i : (!simulation.ref<!l64>, i32) -> !simulation.ref<!l1>
      %a = simulation.ref.load %ar : !simulation.ref<!l1> -> !l1
      %br = simulation.ref.dyn_extract %r1 from %i : (!simulation.ref<!l64>, i32) -> !simulation.ref<!l1>
      %b = simulation.ref.load %br : !simulation.ref<!l1> -> !l1
      %cr = simulation.ref.dyn_extract %r2 from %i : (!simulation.ref<!l64>, i32) -> !simulation.ref<!l1>
      %c = simulation.ref.load %cr : !simulation.ref<!l1> -> !l1
      %result = simulation.logic.shift right %a by %b : (!l1, !l1) -> !l1
      %dst = simulation.ref.dyn_extract %out from %i : (!simulation.ref<!l64>, i32) -> !simulation.ref<!l1>
      simulation.ref.store %result to %dst : !l1, !simulation.ref<!l1>
      %one = arith.constant 1 : i32
      %next = arith.addi %i, %one : i32
      cf.br ^head(%next : i32)
    ^exit:
      simulation.return
    }
    simulation.func @word_shift_right_arith(%ctx: !simulation.context {simulation.capture_kind = 0 : i32}, %r0: !simulation.ref<!l64> {simulation.capture_kind = 3 : i32, simulation.descriptor_id = 0 : i64}, %r1: !simulation.ref<!l64> {simulation.capture_kind = 3 : i32, simulation.descriptor_id = 1 : i64}, %r2: !simulation.ref<!l64> {simulation.capture_kind = 3 : i32, simulation.descriptor_id = 2 : i64}, %out: !simulation.ref<!l64> {simulation.capture_kind = 3 : i32, simulation.descriptor_id = 4 : i64}) attributes {entry_kind = 8 : i32, code_unit_id = 85 : i64, schedule.eval.raw_captures} {
      %zero = arith.constant 0 : i32
      cf.br ^head(%zero : i32)
    ^head(%i: i32):
      %limit = arith.constant 64 : i32
      %more = arith.cmpi ult, %i, %limit : i32
      cf.cond_br %more, ^body, ^exit
    ^body:
      %ar = simulation.ref.dyn_extract %r0 from %i : (!simulation.ref<!l64>, i32) -> !simulation.ref<!l1>
      %a = simulation.ref.load %ar : !simulation.ref<!l1> -> !l1
      %br = simulation.ref.dyn_extract %r1 from %i : (!simulation.ref<!l64>, i32) -> !simulation.ref<!l1>
      %b = simulation.ref.load %br : !simulation.ref<!l1> -> !l1
      %cr = simulation.ref.dyn_extract %r2 from %i : (!simulation.ref<!l64>, i32) -> !simulation.ref<!l1>
      %c = simulation.ref.load %cr : !simulation.ref<!l1> -> !l1
      %result = simulation.logic.shift right_arith %a by %b : (!l1, !l1) -> !l1
      %dst = simulation.ref.dyn_extract %out from %i : (!simulation.ref<!l64>, i32) -> !simulation.ref<!l1>
      simulation.ref.store %result to %dst : !l1, !simulation.ref<!l1>
      %one = arith.constant 1 : i32
      %next = arith.addi %i, %one : i32
      cf.br ^head(%next : i32)
    ^exit:
      simulation.return
    }
    simulation.func @word_power(%ctx: !simulation.context {simulation.capture_kind = 0 : i32}, %r0: !simulation.ref<!l64> {simulation.capture_kind = 3 : i32, simulation.descriptor_id = 0 : i64}, %r1: !simulation.ref<!l64> {simulation.capture_kind = 3 : i32, simulation.descriptor_id = 1 : i64}, %r2: !simulation.ref<!l64> {simulation.capture_kind = 3 : i32, simulation.descriptor_id = 2 : i64}, %out: !simulation.ref<!l64> {simulation.capture_kind = 3 : i32, simulation.descriptor_id = 4 : i64}) attributes {entry_kind = 8 : i32, code_unit_id = 87 : i64, schedule.eval.raw_captures} {
      %zero = arith.constant 0 : i32
      cf.br ^head(%zero : i32)
    ^head(%i: i32):
      %limit = arith.constant 64 : i32
      %more = arith.cmpi ult, %i, %limit : i32
      cf.cond_br %more, ^body, ^exit
    ^body:
      %ar = simulation.ref.dyn_extract %r0 from %i : (!simulation.ref<!l64>, i32) -> !simulation.ref<!l1>
      %a = simulation.ref.load %ar : !simulation.ref<!l1> -> !l1
      %br = simulation.ref.dyn_extract %r1 from %i : (!simulation.ref<!l64>, i32) -> !simulation.ref<!l1>
      %b = simulation.ref.load %br : !simulation.ref<!l1> -> !l1
      %cr = simulation.ref.dyn_extract %r2 from %i : (!simulation.ref<!l64>, i32) -> !simulation.ref<!l1>
      %c = simulation.ref.load %cr : !simulation.ref<!l1> -> !l1
      %result = simulation.logic.power %a, %b : (!l1, !l1) -> !l1
      %dst = simulation.ref.dyn_extract %out from %i : (!simulation.ref<!l64>, i32) -> !simulation.ref<!l1>
      simulation.ref.store %result to %dst : !l1, !simulation.ref<!l1>
      %one = arith.constant 1 : i32
      %next = arith.addi %i, %one : i32
      cf.br ^head(%next : i32)
    ^exit:
      simulation.return
    }
    simulation.func @word_mux(%ctx: !simulation.context {simulation.capture_kind = 0 : i32}, %r0: !simulation.ref<!l64> {simulation.capture_kind = 3 : i32, simulation.descriptor_id = 0 : i64}, %r1: !simulation.ref<!l64> {simulation.capture_kind = 3 : i32, simulation.descriptor_id = 1 : i64}, %r2: !simulation.ref<!l64> {simulation.capture_kind = 3 : i32, simulation.descriptor_id = 2 : i64}, %out: !simulation.ref<!l64> {simulation.capture_kind = 3 : i32, simulation.descriptor_id = 4 : i64}) attributes {entry_kind = 8 : i32, code_unit_id = 89 : i64, schedule.eval.raw_captures} {
      %zero = arith.constant 0 : i32
      cf.br ^head(%zero : i32)
    ^head(%i: i32):
      %limit = arith.constant 64 : i32
      %more = arith.cmpi ult, %i, %limit : i32
      cf.cond_br %more, ^body, ^exit
    ^body:
      %ar = simulation.ref.dyn_extract %r0 from %i : (!simulation.ref<!l64>, i32) -> !simulation.ref<!l1>
      %a = simulation.ref.load %ar : !simulation.ref<!l1> -> !l1
      %br = simulation.ref.dyn_extract %r1 from %i : (!simulation.ref<!l64>, i32) -> !simulation.ref<!l1>
      %b = simulation.ref.load %br : !simulation.ref<!l1> -> !l1
      %cr = simulation.ref.dyn_extract %r2 from %i : (!simulation.ref<!l64>, i32) -> !simulation.ref<!l1>
      %c = simulation.ref.load %cr : !simulation.ref<!l1> -> !l1
      %result = simulation.logic.mux %a ? %b : %c : (!l1, !l1, !l1) -> !l1
      %dst = simulation.ref.dyn_extract %out from %i : (!simulation.ref<!l64>, i32) -> !simulation.ref<!l1>
      simulation.ref.store %result to %dst : !l1, !simulation.ref<!l1>
      %one = arith.constant 1 : i32
      %next = arith.addi %i, %one : i32
      cf.br ^head(%next : i32)
    ^exit:
      simulation.return
    }
    simulation.func @word_is_true(%ctx: !simulation.context {simulation.capture_kind = 0 : i32}, %r0: !simulation.ref<!l64> {simulation.capture_kind = 3 : i32, simulation.descriptor_id = 0 : i64}, %r1: !simulation.ref<!l64> {simulation.capture_kind = 3 : i32, simulation.descriptor_id = 1 : i64}, %r2: !simulation.ref<!l64> {simulation.capture_kind = 3 : i32, simulation.descriptor_id = 2 : i64}, %out: !simulation.ref<i64> {simulation.capture_kind = 3 : i32, simulation.descriptor_id = 6 : i64}) attributes {entry_kind = 8 : i32, code_unit_id = 91 : i64, schedule.eval.raw_captures} {
      %zero = arith.constant 0 : i32
      cf.br ^head(%zero : i32)
    ^head(%i: i32):
      %limit = arith.constant 64 : i32
      %more = arith.cmpi ult, %i, %limit : i32
      cf.cond_br %more, ^body, ^exit
    ^body:
      %ar = simulation.ref.dyn_extract %r0 from %i : (!simulation.ref<!l64>, i32) -> !simulation.ref<!l1>
      %a = simulation.ref.load %ar : !simulation.ref<!l1> -> !l1
      %br = simulation.ref.dyn_extract %r1 from %i : (!simulation.ref<!l64>, i32) -> !simulation.ref<!l1>
      %b = simulation.ref.load %br : !simulation.ref<!l1> -> !l1
      %cr = simulation.ref.dyn_extract %r2 from %i : (!simulation.ref<!l64>, i32) -> !simulation.ref<!l1>
      %c = simulation.ref.load %cr : !simulation.ref<!l1> -> !l1
      %result = simulation.logic.is_true %a : !l1
      %dst = simulation.ref.dyn_extract %out from %i : (!simulation.ref<i64>, i32) -> !simulation.ref<i1>
      simulation.ref.store %result to %dst : i1, !simulation.ref<i1>
      %one = arith.constant 1 : i32
      %next = arith.addi %i, %one : i32
      cf.br ^head(%next : i32)
    ^exit:
      simulation.return
    }
    simulation.func @word_to_bits(%ctx: !simulation.context {simulation.capture_kind = 0 : i32}, %r0: !simulation.ref<!l64> {simulation.capture_kind = 3 : i32, simulation.descriptor_id = 0 : i64}, %r1: !simulation.ref<!l64> {simulation.capture_kind = 3 : i32, simulation.descriptor_id = 1 : i64}, %r2: !simulation.ref<!l64> {simulation.capture_kind = 3 : i32, simulation.descriptor_id = 2 : i64}, %out: !simulation.ref<i64> {simulation.capture_kind = 3 : i32, simulation.descriptor_id = 6 : i64}) attributes {entry_kind = 8 : i32, code_unit_id = 93 : i64, schedule.eval.raw_captures} {
      %zero = arith.constant 0 : i32
      cf.br ^head(%zero : i32)
    ^head(%i: i32):
      %limit = arith.constant 64 : i32
      %more = arith.cmpi ult, %i, %limit : i32
      cf.cond_br %more, ^body, ^exit
    ^body:
      %ar = simulation.ref.dyn_extract %r0 from %i : (!simulation.ref<!l64>, i32) -> !simulation.ref<!l1>
      %a = simulation.ref.load %ar : !simulation.ref<!l1> -> !l1
      %br = simulation.ref.dyn_extract %r1 from %i : (!simulation.ref<!l64>, i32) -> !simulation.ref<!l1>
      %b = simulation.ref.load %br : !simulation.ref<!l1> -> !l1
      %cr = simulation.ref.dyn_extract %r2 from %i : (!simulation.ref<!l64>, i32) -> !simulation.ref<!l1>
      %c = simulation.ref.load %cr : !simulation.ref<!l1> -> !l1
      %result = simulation.logic.to_bits %a : !l1 -> i1
      %dst = simulation.ref.dyn_extract %out from %i : (!simulation.ref<i64>, i32) -> !simulation.ref<i1>
      simulation.ref.store %result to %dst : i1, !simulation.ref<i1>
      %one = arith.constant 1 : i32
      %next = arith.addi %i, %one : i32
      cf.br ^head(%next : i32)
    ^exit:
      simulation.return
    }
    simulation.func @word_from_bits(%ctx: !simulation.context {simulation.capture_kind = 0 : i32}, %r0: !simulation.ref<!l64> {simulation.capture_kind = 3 : i32, simulation.descriptor_id = 0 : i64}, %r1: !simulation.ref<!l64> {simulation.capture_kind = 3 : i32, simulation.descriptor_id = 1 : i64}, %r2: !simulation.ref<!l64> {simulation.capture_kind = 3 : i32, simulation.descriptor_id = 2 : i64}, %out: !simulation.ref<!l64> {simulation.capture_kind = 3 : i32, simulation.descriptor_id = 4 : i64}) attributes {entry_kind = 8 : i32, code_unit_id = 95 : i64, schedule.eval.raw_captures} {
      %zero = arith.constant 0 : i32
      cf.br ^head(%zero : i32)
    ^head(%i: i32):
      %limit = arith.constant 64 : i32
      %more = arith.cmpi ult, %i, %limit : i32
      cf.cond_br %more, ^body, ^exit
    ^body:
      %ar = simulation.ref.dyn_extract %r0 from %i : (!simulation.ref<!l64>, i32) -> !simulation.ref<!l1>
      %a = simulation.ref.load %ar : !simulation.ref<!l1> -> !l1
      %ai = simulation.logic.to_bits %a : !l1 -> i1
      %br = simulation.ref.dyn_extract %r1 from %i : (!simulation.ref<!l64>, i32) -> !simulation.ref<!l1>
      %b = simulation.ref.load %br : !simulation.ref<!l1> -> !l1
      %bi = simulation.logic.to_bits %b : !l1 -> i1
      %cr = simulation.ref.dyn_extract %r2 from %i : (!simulation.ref<!l64>, i32) -> !simulation.ref<!l1>
      %c = simulation.ref.load %cr : !simulation.ref<!l1> -> !l1
      %ci = simulation.logic.to_bits %c : !l1 -> i1
      %result = simulation.logic.from_bits %ai : i1 -> !l1
      %dst = simulation.ref.dyn_extract %out from %i : (!simulation.ref<!l64>, i32) -> !simulation.ref<!l1>
      simulation.ref.store %result to %dst : !l1, !simulation.ref<!l1>
      %one = arith.constant 1 : i32
      %next = arith.addi %i, %one : i32
      cf.br ^head(%next : i32)
    ^exit:
      simulation.return
    }
    simulation.func @word_integer_andi(%ctx: !simulation.context {simulation.capture_kind = 0 : i32}, %r0: !simulation.ref<!l64> {simulation.capture_kind = 3 : i32, simulation.descriptor_id = 0 : i64}, %r1: !simulation.ref<!l64> {simulation.capture_kind = 3 : i32, simulation.descriptor_id = 1 : i64}, %r2: !simulation.ref<!l64> {simulation.capture_kind = 3 : i32, simulation.descriptor_id = 2 : i64}, %out: !simulation.ref<i64> {simulation.capture_kind = 3 : i32, simulation.descriptor_id = 6 : i64}) attributes {entry_kind = 8 : i32, code_unit_id = 97 : i64, schedule.eval.raw_captures} {
      %zero = arith.constant 0 : i32
      cf.br ^head(%zero : i32)
    ^head(%i: i32):
      %limit = arith.constant 64 : i32
      %more = arith.cmpi ult, %i, %limit : i32
      cf.cond_br %more, ^body, ^exit
    ^body:
      %ar = simulation.ref.dyn_extract %r0 from %i : (!simulation.ref<!l64>, i32) -> !simulation.ref<!l1>
      %a = simulation.ref.load %ar : !simulation.ref<!l1> -> !l1
      %ai = simulation.logic.to_bits %a : !l1 -> i1
      %br = simulation.ref.dyn_extract %r1 from %i : (!simulation.ref<!l64>, i32) -> !simulation.ref<!l1>
      %b = simulation.ref.load %br : !simulation.ref<!l1> -> !l1
      %bi = simulation.logic.to_bits %b : !l1 -> i1
      %cr = simulation.ref.dyn_extract %r2 from %i : (!simulation.ref<!l64>, i32) -> !simulation.ref<!l1>
      %c = simulation.ref.load %cr : !simulation.ref<!l1> -> !l1
      %ci = simulation.logic.to_bits %c : !l1 -> i1
      %result = arith.andi %ai, %bi : i1
      %dst = simulation.ref.dyn_extract %out from %i : (!simulation.ref<i64>, i32) -> !simulation.ref<i1>
      simulation.ref.store %result to %dst : i1, !simulation.ref<i1>
      %one = arith.constant 1 : i32
      %next = arith.addi %i, %one : i32
      cf.br ^head(%next : i32)
    ^exit:
      simulation.return
    }
    simulation.func @word_integer_ori(%ctx: !simulation.context {simulation.capture_kind = 0 : i32}, %r0: !simulation.ref<!l64> {simulation.capture_kind = 3 : i32, simulation.descriptor_id = 0 : i64}, %r1: !simulation.ref<!l64> {simulation.capture_kind = 3 : i32, simulation.descriptor_id = 1 : i64}, %r2: !simulation.ref<!l64> {simulation.capture_kind = 3 : i32, simulation.descriptor_id = 2 : i64}, %out: !simulation.ref<i64> {simulation.capture_kind = 3 : i32, simulation.descriptor_id = 6 : i64}) attributes {entry_kind = 8 : i32, code_unit_id = 99 : i64, schedule.eval.raw_captures} {
      %zero = arith.constant 0 : i32
      cf.br ^head(%zero : i32)
    ^head(%i: i32):
      %limit = arith.constant 64 : i32
      %more = arith.cmpi ult, %i, %limit : i32
      cf.cond_br %more, ^body, ^exit
    ^body:
      %ar = simulation.ref.dyn_extract %r0 from %i : (!simulation.ref<!l64>, i32) -> !simulation.ref<!l1>
      %a = simulation.ref.load %ar : !simulation.ref<!l1> -> !l1
      %ai = simulation.logic.to_bits %a : !l1 -> i1
      %br = simulation.ref.dyn_extract %r1 from %i : (!simulation.ref<!l64>, i32) -> !simulation.ref<!l1>
      %b = simulation.ref.load %br : !simulation.ref<!l1> -> !l1
      %bi = simulation.logic.to_bits %b : !l1 -> i1
      %cr = simulation.ref.dyn_extract %r2 from %i : (!simulation.ref<!l64>, i32) -> !simulation.ref<!l1>
      %c = simulation.ref.load %cr : !simulation.ref<!l1> -> !l1
      %ci = simulation.logic.to_bits %c : !l1 -> i1
      %result = arith.ori %ai, %bi : i1
      %dst = simulation.ref.dyn_extract %out from %i : (!simulation.ref<i64>, i32) -> !simulation.ref<i1>
      simulation.ref.store %result to %dst : i1, !simulation.ref<i1>
      %one = arith.constant 1 : i32
      %next = arith.addi %i, %one : i32
      cf.br ^head(%next : i32)
    ^exit:
      simulation.return
    }
    simulation.func @word_integer_xori(%ctx: !simulation.context {simulation.capture_kind = 0 : i32}, %r0: !simulation.ref<!l64> {simulation.capture_kind = 3 : i32, simulation.descriptor_id = 0 : i64}, %r1: !simulation.ref<!l64> {simulation.capture_kind = 3 : i32, simulation.descriptor_id = 1 : i64}, %r2: !simulation.ref<!l64> {simulation.capture_kind = 3 : i32, simulation.descriptor_id = 2 : i64}, %out: !simulation.ref<i64> {simulation.capture_kind = 3 : i32, simulation.descriptor_id = 6 : i64}) attributes {entry_kind = 8 : i32, code_unit_id = 101 : i64, schedule.eval.raw_captures} {
      %zero = arith.constant 0 : i32
      cf.br ^head(%zero : i32)
    ^head(%i: i32):
      %limit = arith.constant 64 : i32
      %more = arith.cmpi ult, %i, %limit : i32
      cf.cond_br %more, ^body, ^exit
    ^body:
      %ar = simulation.ref.dyn_extract %r0 from %i : (!simulation.ref<!l64>, i32) -> !simulation.ref<!l1>
      %a = simulation.ref.load %ar : !simulation.ref<!l1> -> !l1
      %ai = simulation.logic.to_bits %a : !l1 -> i1
      %br = simulation.ref.dyn_extract %r1 from %i : (!simulation.ref<!l64>, i32) -> !simulation.ref<!l1>
      %b = simulation.ref.load %br : !simulation.ref<!l1> -> !l1
      %bi = simulation.logic.to_bits %b : !l1 -> i1
      %cr = simulation.ref.dyn_extract %r2 from %i : (!simulation.ref<!l64>, i32) -> !simulation.ref<!l1>
      %c = simulation.ref.load %cr : !simulation.ref<!l1> -> !l1
      %ci = simulation.logic.to_bits %c : !l1 -> i1
      %result = arith.xori %ai, %bi : i1
      %dst = simulation.ref.dyn_extract %out from %i : (!simulation.ref<i64>, i32) -> !simulation.ref<i1>
      simulation.ref.store %result to %dst : i1, !simulation.ref<i1>
      %one = arith.constant 1 : i32
      %next = arith.addi %i, %one : i32
      cf.br ^head(%next : i32)
    ^exit:
      simulation.return
    }
    simulation.func @word_integer_addi(%ctx: !simulation.context {simulation.capture_kind = 0 : i32}, %r0: !simulation.ref<!l64> {simulation.capture_kind = 3 : i32, simulation.descriptor_id = 0 : i64}, %r1: !simulation.ref<!l64> {simulation.capture_kind = 3 : i32, simulation.descriptor_id = 1 : i64}, %r2: !simulation.ref<!l64> {simulation.capture_kind = 3 : i32, simulation.descriptor_id = 2 : i64}, %out: !simulation.ref<i64> {simulation.capture_kind = 3 : i32, simulation.descriptor_id = 6 : i64}) attributes {entry_kind = 8 : i32, code_unit_id = 103 : i64, schedule.eval.raw_captures} {
      %zero = arith.constant 0 : i32
      cf.br ^head(%zero : i32)
    ^head(%i: i32):
      %limit = arith.constant 64 : i32
      %more = arith.cmpi ult, %i, %limit : i32
      cf.cond_br %more, ^body, ^exit
    ^body:
      %ar = simulation.ref.dyn_extract %r0 from %i : (!simulation.ref<!l64>, i32) -> !simulation.ref<!l1>
      %a = simulation.ref.load %ar : !simulation.ref<!l1> -> !l1
      %ai = simulation.logic.to_bits %a : !l1 -> i1
      %br = simulation.ref.dyn_extract %r1 from %i : (!simulation.ref<!l64>, i32) -> !simulation.ref<!l1>
      %b = simulation.ref.load %br : !simulation.ref<!l1> -> !l1
      %bi = simulation.logic.to_bits %b : !l1 -> i1
      %cr = simulation.ref.dyn_extract %r2 from %i : (!simulation.ref<!l64>, i32) -> !simulation.ref<!l1>
      %c = simulation.ref.load %cr : !simulation.ref<!l1> -> !l1
      %ci = simulation.logic.to_bits %c : !l1 -> i1
      %result = arith.addi %ai, %bi : i1
      %dst = simulation.ref.dyn_extract %out from %i : (!simulation.ref<i64>, i32) -> !simulation.ref<i1>
      simulation.ref.store %result to %dst : i1, !simulation.ref<i1>
      %one = arith.constant 1 : i32
      %next = arith.addi %i, %one : i32
      cf.br ^head(%next : i32)
    ^exit:
      simulation.return
    }
    simulation.func @word_integer_subi(%ctx: !simulation.context {simulation.capture_kind = 0 : i32}, %r0: !simulation.ref<!l64> {simulation.capture_kind = 3 : i32, simulation.descriptor_id = 0 : i64}, %r1: !simulation.ref<!l64> {simulation.capture_kind = 3 : i32, simulation.descriptor_id = 1 : i64}, %r2: !simulation.ref<!l64> {simulation.capture_kind = 3 : i32, simulation.descriptor_id = 2 : i64}, %out: !simulation.ref<i64> {simulation.capture_kind = 3 : i32, simulation.descriptor_id = 6 : i64}) attributes {entry_kind = 8 : i32, code_unit_id = 105 : i64, schedule.eval.raw_captures} {
      %zero = arith.constant 0 : i32
      cf.br ^head(%zero : i32)
    ^head(%i: i32):
      %limit = arith.constant 64 : i32
      %more = arith.cmpi ult, %i, %limit : i32
      cf.cond_br %more, ^body, ^exit
    ^body:
      %ar = simulation.ref.dyn_extract %r0 from %i : (!simulation.ref<!l64>, i32) -> !simulation.ref<!l1>
      %a = simulation.ref.load %ar : !simulation.ref<!l1> -> !l1
      %ai = simulation.logic.to_bits %a : !l1 -> i1
      %br = simulation.ref.dyn_extract %r1 from %i : (!simulation.ref<!l64>, i32) -> !simulation.ref<!l1>
      %b = simulation.ref.load %br : !simulation.ref<!l1> -> !l1
      %bi = simulation.logic.to_bits %b : !l1 -> i1
      %cr = simulation.ref.dyn_extract %r2 from %i : (!simulation.ref<!l64>, i32) -> !simulation.ref<!l1>
      %c = simulation.ref.load %cr : !simulation.ref<!l1> -> !l1
      %ci = simulation.logic.to_bits %c : !l1 -> i1
      %result = arith.subi %ai, %bi : i1
      %dst = simulation.ref.dyn_extract %out from %i : (!simulation.ref<i64>, i32) -> !simulation.ref<i1>
      simulation.ref.store %result to %dst : i1, !simulation.ref<i1>
      %one = arith.constant 1 : i32
      %next = arith.addi %i, %one : i32
      cf.br ^head(%next : i32)
    ^exit:
      simulation.return
    }
    simulation.func @word_integer_muli(%ctx: !simulation.context {simulation.capture_kind = 0 : i32}, %r0: !simulation.ref<!l64> {simulation.capture_kind = 3 : i32, simulation.descriptor_id = 0 : i64}, %r1: !simulation.ref<!l64> {simulation.capture_kind = 3 : i32, simulation.descriptor_id = 1 : i64}, %r2: !simulation.ref<!l64> {simulation.capture_kind = 3 : i32, simulation.descriptor_id = 2 : i64}, %out: !simulation.ref<i64> {simulation.capture_kind = 3 : i32, simulation.descriptor_id = 6 : i64}) attributes {entry_kind = 8 : i32, code_unit_id = 107 : i64, schedule.eval.raw_captures} {
      %zero = arith.constant 0 : i32
      cf.br ^head(%zero : i32)
    ^head(%i: i32):
      %limit = arith.constant 64 : i32
      %more = arith.cmpi ult, %i, %limit : i32
      cf.cond_br %more, ^body, ^exit
    ^body:
      %ar = simulation.ref.dyn_extract %r0 from %i : (!simulation.ref<!l64>, i32) -> !simulation.ref<!l1>
      %a = simulation.ref.load %ar : !simulation.ref<!l1> -> !l1
      %ai = simulation.logic.to_bits %a : !l1 -> i1
      %br = simulation.ref.dyn_extract %r1 from %i : (!simulation.ref<!l64>, i32) -> !simulation.ref<!l1>
      %b = simulation.ref.load %br : !simulation.ref<!l1> -> !l1
      %bi = simulation.logic.to_bits %b : !l1 -> i1
      %cr = simulation.ref.dyn_extract %r2 from %i : (!simulation.ref<!l64>, i32) -> !simulation.ref<!l1>
      %c = simulation.ref.load %cr : !simulation.ref<!l1> -> !l1
      %ci = simulation.logic.to_bits %c : !l1 -> i1
      %result = arith.muli %ai, %bi : i1
      %dst = simulation.ref.dyn_extract %out from %i : (!simulation.ref<i64>, i32) -> !simulation.ref<i1>
      simulation.ref.store %result to %dst : i1, !simulation.ref<i1>
      %one = arith.constant 1 : i32
      %next = arith.addi %i, %one : i32
      cf.br ^head(%next : i32)
    ^exit:
      simulation.return
    }
    simulation.func @word_integer_select(%ctx: !simulation.context {simulation.capture_kind = 0 : i32}, %r0: !simulation.ref<!l64> {simulation.capture_kind = 3 : i32, simulation.descriptor_id = 0 : i64}, %r1: !simulation.ref<!l64> {simulation.capture_kind = 3 : i32, simulation.descriptor_id = 1 : i64}, %r2: !simulation.ref<!l64> {simulation.capture_kind = 3 : i32, simulation.descriptor_id = 2 : i64}, %out: !simulation.ref<i64> {simulation.capture_kind = 3 : i32, simulation.descriptor_id = 6 : i64}) attributes {entry_kind = 8 : i32, code_unit_id = 109 : i64, schedule.eval.raw_captures} {
      %zero = arith.constant 0 : i32
      cf.br ^head(%zero : i32)
    ^head(%i: i32):
      %limit = arith.constant 64 : i32
      %more = arith.cmpi ult, %i, %limit : i32
      cf.cond_br %more, ^body, ^exit
    ^body:
      %ar = simulation.ref.dyn_extract %r0 from %i : (!simulation.ref<!l64>, i32) -> !simulation.ref<!l1>
      %a = simulation.ref.load %ar : !simulation.ref<!l1> -> !l1
      %ai = simulation.logic.to_bits %a : !l1 -> i1
      %br = simulation.ref.dyn_extract %r1 from %i : (!simulation.ref<!l64>, i32) -> !simulation.ref<!l1>
      %b = simulation.ref.load %br : !simulation.ref<!l1> -> !l1
      %bi = simulation.logic.to_bits %b : !l1 -> i1
      %cr = simulation.ref.dyn_extract %r2 from %i : (!simulation.ref<!l64>, i32) -> !simulation.ref<!l1>
      %c = simulation.ref.load %cr : !simulation.ref<!l1> -> !l1
      %ci = simulation.logic.to_bits %c : !l1 -> i1
      %result = arith.select %ai, %bi, %ci : i1
      %dst = simulation.ref.dyn_extract %out from %i : (!simulation.ref<i64>, i32) -> !simulation.ref<i1>
      simulation.ref.store %result to %dst : i1, !simulation.ref<i1>
      %one = arith.constant 1 : i32
      %next = arith.addi %i, %one : i32
      cf.br ^head(%next : i32)
    ^exit:
      simulation.return
    }
    simulation.func @word_wide_constant_shift(%ctx: !simulation.context {simulation.capture_kind = 0 : i32}, %r0: !simulation.ref<!l64> {simulation.capture_kind = 3 : i32, simulation.descriptor_id = 0 : i64}, %r1: !simulation.ref<!l64> {simulation.capture_kind = 3 : i32, simulation.descriptor_id = 1 : i64}, %r2: !simulation.ref<!l64> {simulation.capture_kind = 3 : i32, simulation.descriptor_id = 2 : i64}, %out: !simulation.ref<!l64> {simulation.capture_kind = 3 : i32, simulation.descriptor_id = 4 : i64}) attributes {entry_kind = 8 : i32, code_unit_id = 111 : i64, schedule.eval.raw_captures} {
      %zero = arith.constant 0 : i32
      cf.br ^head(%zero : i32)
    ^head(%i: i32):
      %limit = arith.constant 64 : i32
      %more = arith.cmpi ult, %i, %limit : i32
      cf.cond_br %more, ^body, ^exit
    ^body:
      %ar = simulation.ref.dyn_extract %r0 from %i : (!simulation.ref<!l64>, i32) -> !simulation.ref<!l1>
      %a = simulation.ref.load %ar : !simulation.ref<!l1> -> !l1
      %br = simulation.ref.dyn_extract %r1 from %i : (!simulation.ref<!l64>, i32) -> !simulation.ref<!l1>
      %b = simulation.ref.load %br : !simulation.ref<!l1> -> !l1
      %cr = simulation.ref.dyn_extract %r2 from %i : (!simulation.ref<!l64>, i32) -> !simulation.ref<!l1>
      %c = simulation.ref.load %cr : !simulation.ref<!l1> -> !l1
      %amount = arith.constant 1 : i32
      %result = simulation.logic.shift left %a by %amount : (!l1, i32) -> !l1
      %dst = simulation.ref.dyn_extract %out from %i : (!simulation.ref<!l64>, i32) -> !simulation.ref<!l1>
      simulation.ref.store %result to %dst : !l1, !simulation.ref<!l1>
      %one = arith.constant 1 : i32
      %next = arith.addi %i, %one : i32
      cf.br ^head(%next : i32)
    ^exit:
      simulation.return
    }
  }
}
