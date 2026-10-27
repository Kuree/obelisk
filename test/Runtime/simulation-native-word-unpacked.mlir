// RUN: talos-opt %s --schedule-optimize-native-words -o %t.words
// RUN: FileCheck %s --check-prefix=WORDS < %t.words
// RUN: talos-opt %t.words --convert-schedule-word-regions --convert-talos-sim-processes-to-llvm-coroutines | talos-translate --mlir-to-llvmir | %llvm_dist/bin/opt -passes='coro-early,coro-split<reuse-storage>,coro-cleanup,default<O0>' | %llc -filetype=obj -relocation-model=pic -o %t.o
// RUN: %target_cxx %t.o %runtime_link_flags -o %t.exe
// RUN: %t.exe | FileCheck %s

// Unpacked storage starts at the left declaration bound, unlike packed bits.
// Opposite declaration directions reverse physical lanes, including X and Z.
// WORDS-LABEL: simulation.func @copy
// WORDS: schedule.word.region 8
// WORDS: schedule.word.view
// WORDS: schedule.word.extract
// WORDS: schedule.word.reverse
// CHECK: unpacked 1 1 1 1 1 1 1 1
// CHECK: invalid view 1
// CHECK: invalid extract 1 1 1
!bit = !simulation.logic<1>
!up = !simulation.unpacked_array<-4 : 3 x !simulation.logic<1>>
!wrapped = !simulation.packed_struct<[#simulation.field<name = "bit", type = !simulation.logic<1>, ordinal = 0, packedOffset = 0>]>
!down = !simulation.unpacked_array<3 : -4 x !simulation.packed_struct<[#simulation.field<name = "bit", type = !simulation.logic<1>, ordinal = 0, packedOffset = 0>]>>
!upref = !simulation.ref<!up>
!downref = !simulation.ref<!down>
!bitref = !simulation.ref<!bit>
module attributes {llvm.data_layout = "e-m:e-p:64:64-i64:64-n8:16:32:64-S128", llvm.target_triple = "x86_64-unknown-linux-gnu"} {
  simulation.design @unpacked attributes {
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
    simulation.code_unit.decl 2 in 0 function hierarchy "copy"
    simulation.storage.decl 0 in 0 : !up design
    simulation.storage.decl 1 in 0 : !down design
    simulation.func @root(%ctx: !simulation.context {simulation.capture_kind = 0 : i32}) attributes {entry_kind = 0 : i32, code_unit_id = 1 : i64} {
      %src = simulation.context.storage %ctx[0] : !upref
      %dst = simulation.context.storage %ctx[1] : !downref
      %zero = simulation.logic.constant 0 : i1, 0 : i1 : !bit
      %one = simulation.logic.constant 1 : i1, 0 : i1 : !bit
      %x = simulation.logic.constant 0 : i1, 1 : i1 : !bit
      %z = simulation.logic.constant 1 : i1, 1 : i1 : !bit
      %input = simulation.aggregate.construct %z, %zero, %one, %x, %one, %z, %x, %zero : (!bit, !bit, !bit, !bit, !bit, !bit, !bit, !bit) -> !up
      simulation.ref.store %input to %src : !up, !upref
      // Physical offset 1 is a valid declaration index but an invalid start
      // for an eight-bit view of eight-bit storage. No partial store occurs.
      %low = arith.constant 1 : i65
      %invalid = schedule.word.view %src, %low : (!upref, i65) -> !simulation.ref<!simulation.logic<8>>
      %ones = simulation.logic.constant 255 : i8, 0 : i8 : !simulation.logic<8>
      simulation.ref.store %ones to %invalid : !simulation.logic<8>, !simulation.ref<!simulation.logic<8>>
      %bad = simulation.ref.load %invalid : !simulation.ref<!simulation.logic<8>> -> !simulation.logic<8>
      %allx = simulation.logic.constant 0 : i8, 255 : i8 : !simulation.logic<8>
      %invalid_ok = simulation.logic.compare case_eq %bad, %allx : (!simulation.logic<8>, !simulation.logic<8>) -> i1
      %bad_extract = schedule.word.extract %input from %low : (!up, i65) -> !simulation.logic<8>
      %extract_ok = simulation.logic.compare case_eq %bad_extract, %allx : (!simulation.logic<8>, !simulation.logic<8>) -> i1
      %negative = arith.constant -1 : i8
      %negative_extract = schedule.word.extract %input from %negative : (!up, i8) -> !simulation.logic<8>
      %negative_ok = simulation.logic.compare case_eq %negative_extract, %allx : (!simulation.logic<8>, !simulation.logic<8>) -> i1
      %huge = arith.constant 18446744073709551616 : i128
      %huge_extract = schedule.word.extract %input from %huge : (!up, i128) -> !simulation.logic<8>
      %huge_ok = simulation.logic.compare case_eq %huge_extract, %allx : (!simulation.logic<8>, !simulation.logic<8>) -> i1
      simulation.call @copy(%ctx, %src, %dst) : (!simulation.context, !upref, !downref) -> ()
      %out = simulation.ref.load %dst : !downref -> !down
      %w0 = simulation.aggregate.extract %out[0] : (!down) -> !wrapped
      %v0 = simulation.packed.flatten %w0 : (!wrapped) -> !bit
      %w1 = simulation.aggregate.extract %out[1] : (!down) -> !wrapped
      %v1 = simulation.packed.flatten %w1 : (!wrapped) -> !bit
      %w2 = simulation.aggregate.extract %out[2] : (!down) -> !wrapped
      %v2 = simulation.packed.flatten %w2 : (!wrapped) -> !bit
      %w3 = simulation.aggregate.extract %out[3] : (!down) -> !wrapped
      %v3 = simulation.packed.flatten %w3 : (!wrapped) -> !bit
      %w4 = simulation.aggregate.extract %out[4] : (!down) -> !wrapped
      %v4 = simulation.packed.flatten %w4 : (!wrapped) -> !bit
      %w5 = simulation.aggregate.extract %out[5] : (!down) -> !wrapped
      %v5 = simulation.packed.flatten %w5 : (!wrapped) -> !bit
      %w6 = simulation.aggregate.extract %out[6] : (!down) -> !wrapped
      %v6 = simulation.packed.flatten %w6 : (!wrapped) -> !bit
      %w7 = simulation.aggregate.extract %out[7] : (!down) -> !wrapped
      %v7 = simulation.packed.flatten %w7 : (!wrapped) -> !bit
      %ok0 = simulation.logic.compare case_eq %v0, %zero : (!bit, !bit) -> i1
      %ok1 = simulation.logic.compare case_eq %v1, %x : (!bit, !bit) -> i1
      %ok2 = simulation.logic.compare case_eq %v2, %z : (!bit, !bit) -> i1
      %ok3 = simulation.logic.compare case_eq %v3, %one : (!bit, !bit) -> i1
      %ok4 = simulation.logic.compare case_eq %v4, %x : (!bit, !bit) -> i1
      %ok5 = simulation.logic.compare case_eq %v5, %one : (!bit, !bit) -> i1
      %ok6 = simulation.logic.compare case_eq %v6, %zero : (!bit, !bit) -> i1
      %ok7 = simulation.logic.compare case_eq %v7, %z : (!bit, !bit) -> i1
      %fmt = simulation.bytes.constant "unpacked %0d %0d %0d %0d %0d %0d %0d %0d"
      %channel = arith.constant 1 : i32
      simulation.display %ctx to %channel(%fmt, %ok0, %ok1, %ok2, %ok3, %ok4, %ok5, %ok6, %ok7) newline = true radix = <decimal> flags = [0, 0, 0, 0, 0, 0, 0, 0, 0] : !simulation.bytes, i1, i1, i1, i1, i1, i1, i1, i1
      %invalid_fmt = simulation.bytes.constant "invalid view %0d"
      simulation.display %ctx to %channel(%invalid_fmt, %invalid_ok) newline = true radix = <decimal> flags = [0, 0] : !simulation.bytes, i1
      %extract_fmt = simulation.bytes.constant "invalid extract %0d %0d %0d"
      simulation.display %ctx to %channel(%extract_fmt, %extract_ok, %negative_ok, %huge_ok) newline = true radix = <decimal> flags = [0, 0, 0, 0] : !simulation.bytes, i1, i1, i1
      simulation.return
    }
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
  }
}
