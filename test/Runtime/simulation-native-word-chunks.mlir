// RUN: obelisk-opt %s --schedule-optimize-native-words -o %t.words
// RUN: FileCheck %s --check-prefix=WORDS < %t.words
// RUN: obelisk-opt %t.words --convert-obelisk-sim-processes-to-llvm-coroutines | obelisk-translate --mlir-to-llvmir | %llvm_dist/bin/opt -passes='coro-early,coro-split<reuse-storage>,coro-cleanup,default<O0>' | %llc -filetype=obj -relocation-model=pic -o %t.o
// RUN: %llvm_dist/bin/clang++ %t.o %native_support/libobelisk_rt.a %native_support/libc++.a %native_support/libc++abi.a %native_support/libunwind.a -nostdlib++ -lpthread -ldl -o %t.exe
// RUN: %t.exe | FileCheck %s

// LRM 11.5, 12.7.1: 65 backward iterations use five 13-bit groups. One group
// crosses the host's 64-bit storage boundary. Neighboring bits retain Z.
// A second copy checks 67 backward iterations as 64-bit and 3-bit regions.
// WORDS-LABEL: simulation.func @copy
// WORDS: cf.cond_br
// WORDS: schedule.word.region 13
// WORDS: simulation.ref.store {{.*}}!simulation.logic<13>
// WORDS-LABEL: simulation.func @copy_prime
// WORDS: schedule.word.region 64
// WORDS: schedule.word.region 3
// CHECK: chunks 1 1
!word = !simulation.logic<69>
!ref = !simulation.ref<!simulation.logic<69>>
!bit = !simulation.logic<1>
!bitref = !simulation.ref<!simulation.logic<1>>
module attributes {llvm.data_layout = "e-m:e-p:64:64-i64:64-n8:16:32:64-S128", llvm.target_triple = "x86_64-unknown-linux-gnu"} {
  simulation.design @chunks attributes {
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
    simulation.code_unit.decl 3 in 0 function hierarchy "copy_prime"
    simulation.storage.decl 0 in 0 : !word design
    simulation.storage.decl 1 in 0 : !word design
    simulation.func @root(%ctx: !simulation.context {simulation.capture_kind = 0 : i32}) attributes {entry_kind = 0 : i32, code_unit_id = 1 : i64} {
      %src = simulation.context.storage %ctx[0] : !ref
      %dst = simulation.context.storage %ctx[1] : !ref
      %input = simulation.logic.constant 196765270119568550570 : i69, 393530540239137101141 : i69 : !word
      %z = simulation.logic.constant -1 : i69, -1 : i69 : !word
      simulation.ref.store %input to %src : !word, !ref
      simulation.ref.store %z to %dst : !word, !ref
      simulation.call @copy(%ctx, %src, %dst) : (!simulation.context, !ref, !ref) -> ()
      %out = simulation.ref.load %dst : !ref -> !word
      %expected = simulation.logic.constant 565700151593759582891 : i69, 541104492828813514069 : i69 : !word
      %ok = simulation.logic.compare case_eq %out, %expected : (!word, !word) -> i1
      simulation.ref.store %z to %dst : !word, !ref
      simulation.call @copy_prime(%ctx, %src, %dst) : (!simulation.context, !ref, !ref) -> ()
      %tail_out = simulation.ref.load %dst : !ref -> !word
      %tail_expected = simulation.logic.constant 491913175298921376427 : i69, 393530540239137101141 : i69 : !word
      %tail_ok = simulation.logic.compare case_eq %tail_out, %tail_expected : (!word, !word) -> i1
      %fmt = simulation.bytes.constant "chunks %0d %0d"
      %channel = arith.constant 1 : i32
      simulation.display %ctx to %channel(%fmt, %ok, %tail_ok) newline = true radix = <decimal> flags = [0, 0, 0] : !simulation.bytes, i1, i1
      simulation.return
    }
    simulation.func @copy(
      %ctx: !simulation.context {simulation.capture_kind = 0 : i32},
      %src: !ref {simulation.capture_kind = 3 : i32, simulation.descriptor_id = 0 : i64},
      %dst: !ref {simulation.capture_kind = 3 : i32, simulation.descriptor_id = 1 : i64}) attributes {entry_kind = 8 : i32, code_unit_id = 2 : i64, schedule.eval.raw_captures} {
      %start = arith.constant 65 : i32
      cf.br ^head(%start : i32)
    ^head(%i: i32):
      %limit = arith.constant 1 : i32
      %more = arith.cmpi sge, %i, %limit : i32
      cf.cond_br %more, ^body, ^exit
    ^body:
      %in = simulation.ref.dyn_extract %src from %i : (!ref, i32) -> !bitref
      %out = simulation.ref.dyn_extract %dst from %i : (!ref, i32) -> !bitref
      %value = simulation.ref.load %in : !bitref -> !bit
      simulation.ref.store %value to %out : !bit, !bitref
      %one = arith.constant 1 : i32
      %next = arith.subi %i, %one : i32
      cf.br ^head(%next : i32)
    ^exit:
      simulation.return
    }
    simulation.func @copy_prime(
      %ctx: !simulation.context {simulation.capture_kind = 0 : i32},
      %src: !ref {simulation.capture_kind = 3 : i32, simulation.descriptor_id = 0 : i64},
      %dst: !ref {simulation.capture_kind = 3 : i32, simulation.descriptor_id = 1 : i64}) attributes {entry_kind = 8 : i32, code_unit_id = 3 : i64, schedule.eval.raw_captures} {
      %start = arith.constant 67 : i32
      cf.br ^head(%start : i32)
    ^head(%i: i32):
      %limit = arith.constant 1 : i32
      %more = arith.cmpi sge, %i, %limit : i32
      cf.cond_br %more, ^body, ^exit
    ^body:
      %in = simulation.ref.dyn_extract %src from %i : (!ref, i32) -> !bitref
      %out = simulation.ref.dyn_extract %dst from %i : (!ref, i32) -> !bitref
      %value = simulation.ref.load %in : !bitref -> !bit
      simulation.ref.store %value to %out : !bit, !bitref
      %one = arith.constant 1 : i32
      %next = arith.subi %i, %one : i32
      cf.br ^head(%next : i32)
    ^exit:
      simulation.return
    }
  }
}
