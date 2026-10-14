// RUN: obelisk-opt %s --schedule-optimize-native-words -o %t.words
// RUN: FileCheck %s --check-prefix=WORDS < %t.words
// RUN: obelisk-opt %t.words --convert-schedule-word-regions --convert-obelisk-sim-processes-to-llvm-coroutines | obelisk-translate --mlir-to-llvmir | %llvm_dist/bin/opt -passes='coro-early,coro-split<reuse-storage>,coro-cleanup,default<O0>' | %llc -filetype=obj -relocation-model=pic -o %t.o
// RUN: %llvm_dist/bin/clang++ %t.o %native_support/libobelisk_rt.a %native_support/libc++.a %native_support/libc++abi.a %native_support/libunwind.a -nostdlib++ -lpthread -ldl -o %t.exe
// RUN: %t.exe | FileCheck %s

// LRM 12.5: exact case selection includes the default arm. Exercise a dynamic
// selector widened from a known boolean, a constant matching selector, and a
// constant selector with no matching case. No scf.index_switch survives.
// WORDS-LABEL: simulation.func @dispatch
// WORDS: schedule.word.region 8
// WORDS-NOT: scf.index_switch
// WORDS-NOT: cf.switch
// WORDS-NOT: cf.cond_br
// WORDS: schedule.word.yield
// CHECK: dispatch 1 1 1
!bit = !simulation.logic<1>
!word = !simulation.logic<8>
!ref = !simulation.ref<!word>
!bitref = !simulation.ref<!bit>
module attributes {llvm.data_layout = "e-m:e-p:64:64-i64:64-n8:16:32:64-S128", llvm.target_triple = "x86_64-unknown-linux-gnu"} {
  simulation.design @dispatch attributes {
    schedule.nba.transient_observable = array<i64>,
    schedule.nba.private_influence = array<i64: 2, 3, 4>,
    compute_graph = #schedule.graph<version = 1, vpi = off, workers = 1,
      nodes = [], edges = [], regions = [
        #schedule.region<kind = active, groups = []>,
        #schedule.region<kind = nba, groups = []>,
        #schedule.region<kind = observed, groups = []>,
        #schedule.region<kind = reactive, groups = []>,
        #schedule.region<kind = postponed, groups = []>]>} {
    simulation.scope.decl 0
    simulation.code_unit.decl 1 in 0 root_initializer hierarchy "root"
    simulation.code_unit.decl 2 in 0 function hierarchy "dispatch"
    simulation.storage.decl 0 in 0 : !word design
    simulation.storage.decl 1 in 0 : !word design
    simulation.storage.decl 2 in 0 : !word design
    simulation.storage.decl 3 in 0 : !word design
    simulation.storage.decl 4 in 0 : !word design
    simulation.func @root(%ctx: !simulation.context {simulation.capture_kind = 0 : i32}) attributes {entry_kind = 0 : i32, code_unit_id = 1 : i64} {
      %a = simulation.context.storage %ctx[0] : !ref
      %b = simulation.context.storage %ctx[1] : !ref
      %d = simulation.context.storage %ctx[2] : !ref
      %h = simulation.context.storage %ctx[3] : !ref
      %m = simulation.context.storage %ctx[4] : !ref
      %av = simulation.logic.constant 170 : i8, 204 : i8 : !word
      %bv = simulation.logic.constant 85 : i8, 240 : i8 : !word
      simulation.ref.store %av to %a : !word, !ref
      simulation.ref.store %bv to %b : !word, !ref
      simulation.call @dispatch(%ctx, %a, %b, %d, %h, %m) : (!simulation.context, !ref, !ref, !ref, !ref, !ref) -> ()
      %dv = simulation.ref.load %d : !ref -> !word
      %hv = simulation.ref.load %h : !ref -> !word
      %mv = simulation.ref.load %m : !ref -> !word
      %de = simulation.logic.constant 87 : i8, 240 : i8 : !word
      %dok = simulation.logic.compare case_eq %dv, %de : (!word, !word) -> i1
      %hok = simulation.logic.compare case_eq %hv, %bv : (!word, !word) -> i1
      %mok = simulation.logic.compare case_eq %mv, %av : (!word, !word) -> i1
      %fmt = simulation.bytes.constant "dispatch %0d %0d %0d"
      %channel = arith.constant 1 : i32
      simulation.display %ctx to %channel(%fmt, %dok, %hok, %mok) newline = true radix = <decimal> flags = [0, 0, 0, 0] : !simulation.bytes, i1, i1, i1
      simulation.return
    }
    simulation.func @dispatch(
      %ctx: !simulation.context {simulation.capture_kind = 0 : i32},
      %a: !ref {simulation.capture_kind = 3 : i32, simulation.descriptor_id = 0 : i64},
      %b: !ref {simulation.capture_kind = 3 : i32, simulation.descriptor_id = 1 : i64},
      %d: !ref {simulation.capture_kind = 3 : i32, simulation.descriptor_id = 2 : i64},
      %h: !ref {simulation.capture_kind = 3 : i32, simulation.descriptor_id = 3 : i64},
      %m: !ref {simulation.capture_kind = 3 : i32, simulation.descriptor_id = 4 : i64}) attributes {entry_kind = 8 : i32, code_unit_id = 2 : i64, schedule.eval.raw_captures} {
      %zero = arith.constant 0 : i32
      %seven = arith.constant 7 : i32
      %nine = arith.constant 9 : i32
      %x = simulation.logic.constant 0 : i1, 1 : i1 : !bit
      cf.br ^head(%zero : i32)
    ^head(%i: i32):
      %limit = arith.constant 8 : i32
      %more = arith.cmpi ult, %i, %limit : i32
      cf.cond_br %more, ^body, ^exit
    ^body:
      %ar = simulation.ref.dyn_extract %a from %i : (!ref, i32) -> !bitref
      %br = simulation.ref.dyn_extract %b from %i : (!ref, i32) -> !bitref
      %dr = simulation.ref.dyn_extract %d from %i : (!ref, i32) -> !bitref
      %hr = simulation.ref.dyn_extract %h from %i : (!ref, i32) -> !bitref
      %mr = simulation.ref.dyn_extract %m from %i : (!ref, i32) -> !bitref
      %av = simulation.ref.load %ar : !bitref -> !bit
      %bv = simulation.ref.load %br : !bitref -> !bit
      %condition = simulation.logic.is_true %av : !bit
      %selector = arith.extui %condition : i1 to i32
      cf.switch %selector : i32, [default: ^bad_dynamic, 0: ^case_zero, 1: ^case_one]
    ^case_zero:
      cf.br ^joined(%bv : !bit)
    ^case_one:
      %inverted = simulation.logic.unary bit_not %bv : (!bit) -> !bit
      cf.br ^joined(%inverted : !bit)
    ^bad_dynamic:
      cf.br ^joined(%x : !bit)
    ^joined(%value: !bit):
      simulation.ref.store %value to %dr : !bit, !bitref
      cf.switch %seven : i32, [default: ^bad_hit, 7: ^hit]
    ^hit:
      simulation.ref.store %bv to %hr : !bit, !bitref
      cf.br ^second
    ^bad_hit:
      simulation.ref.store %x to %hr : !bit, !bitref
      cf.br ^second
    ^second:
      cf.switch %nine : i32, [default: ^miss, 7: ^bad_miss]
    ^miss:
      simulation.ref.store %av to %mr : !bit, !bitref
      cf.br ^advance
    ^bad_miss:
      simulation.ref.store %x to %mr : !bit, !bitref
      cf.br ^advance
    ^advance:
      %one = arith.constant 1 : i32
      %next = arith.addi %i, %one : i32
      cf.br ^head(%next : i32)
    ^exit:
      simulation.return
    }
  }
}
