// RUN: talos-opt %s --schedule-optimize-native-words -o %t.words
// RUN: FileCheck %s --check-prefix=WORD < %t.words
// RUN: talos-opt %t.words --convert-schedule-word-regions --convert-talos-sim-processes-to-llvm-coroutines | talos-translate --mlir-to-llvmir | %llvm_dist/bin/opt -passes='coro-early,coro-split<reuse-storage>,coro-cleanup,default<O0>' | %llc -filetype=obj -relocation-model=pic -o %t.o
// RUN: %target_cxx %t.o %runtime_link_flags -o %t.exe
// RUN: %t.exe | FileCheck %s

// All 16 combinations of 0/1/X/Z, plus a Z broadcast and exact copy.
// Nonzero induction start preserves the neighboring bits of every destination.
// LRM 11.4.8 truth tables are checked independently of the scalar reference.
// WORD-LABEL: simulation.func @word
// WORD: schedule.word.region 16
// WORD: simulation.logic.binary and {{.*}} : !simulation.logic<16>
// WORD: simulation.logic.binary or {{.*}} : !simulation.logic<16>
// WORD: simulation.logic.binary xor {{.*}} : !simulation.logic<16>
// WORD: simulation.logic.binary xnor {{.*}} : !simulation.logic<16>
// WORD: schedule.word.yield
// CHECK: word planes 1 1 1 1 1 1
module attributes {llvm.data_layout = "e-m:e-p:64:64-i64:64-n8:16:32:64-S128", llvm.target_triple = "x86_64-unknown-linux-gnu"} {
  simulation.design @word attributes {
    schedule.nba.transient_observable = array<i64>,
    schedule.nba.private_influence = array<i64: 2, 3, 4, 5, 6, 7>,
    compute_graph = #schedule.graph<version = 1, vpi = off, workers = 1,
      nodes = [], edges = [], regions = [
        #schedule.region<kind = active, groups = []>,
        #schedule.region<kind = nba, groups = []>,
        #schedule.region<kind = observed, groups = []>,
        #schedule.region<kind = reactive, groups = []>,
        #schedule.region<kind = postponed, groups = []>]>} {
    simulation.scope.decl 0
    simulation.code_unit.decl 2 in 0 root_initializer hierarchy "root"
    simulation.code_unit.decl 1 in 0 function hierarchy "word"
    simulation.storage.decl 0 in 0 : !simulation.logic<24> design
    simulation.storage.decl 1 in 0 : !simulation.logic<24> design
    simulation.storage.decl 2 in 0 : !simulation.logic<24> design
    simulation.storage.decl 3 in 0 : !simulation.logic<24> design
    simulation.storage.decl 4 in 0 : !simulation.logic<24> design
    simulation.storage.decl 5 in 0 : !simulation.logic<24> design
    simulation.storage.decl 6 in 0 : !simulation.logic<24> design
    simulation.storage.decl 7 in 0 : !simulation.logic<24> design
    simulation.func @root(%ctx: !simulation.context {simulation.capture_kind = 0 : i32}) attributes {entry_kind = 0 : i32, code_unit_id = 2 : i64} {
      %r0 = simulation.context.storage %ctx[0] : !simulation.ref<!simulation.logic<24>>
      %r1 = simulation.context.storage %ctx[1] : !simulation.ref<!simulation.logic<24>>
      %r2 = simulation.context.storage %ctx[2] : !simulation.ref<!simulation.logic<24>>
      %r3 = simulation.context.storage %ctx[3] : !simulation.ref<!simulation.logic<24>>
      %r4 = simulation.context.storage %ctx[4] : !simulation.ref<!simulation.logic<24>>
      %r5 = simulation.context.storage %ctx[5] : !simulation.ref<!simulation.logic<24>>
      %r6 = simulation.context.storage %ctx[6] : !simulation.ref<!simulation.logic<24>>
      %r7 = simulation.context.storage %ctx[7] : !simulation.ref<!simulation.logic<24>>
      %s0 = simulation.logic.constant 11472645 : i24, 1044480 : i24 : !simulation.logic<24>
      simulation.ref.store %s0 to %r0 : !simulation.logic<24>, !simulation.ref<!simulation.logic<24>>
      %s1 = simulation.logic.constant 11184805 : i24, 838848 : i24 : !simulation.logic<24>
      simulation.ref.store %s1 to %r1 : !simulation.logic<24>, !simulation.ref<!simulation.logic<24>>
      %initial = simulation.logic.constant 11184810 : i24, 0 : i24 : !simulation.logic<24>
      simulation.ref.store %initial to %r2 : !simulation.logic<24>, !simulation.ref<!simulation.logic<24>>
      simulation.ref.store %initial to %r3 : !simulation.logic<24>, !simulation.ref<!simulation.logic<24>>
      simulation.ref.store %initial to %r4 : !simulation.logic<24>, !simulation.ref<!simulation.logic<24>>
      simulation.ref.store %initial to %r5 : !simulation.logic<24>, !simulation.ref<!simulation.logic<24>>
      simulation.ref.store %initial to %r6 : !simulation.logic<24>, !simulation.ref<!simulation.logic<24>>
      simulation.ref.store %initial to %r7 : !simulation.logic<24>, !simulation.ref<!simulation.logic<24>>
      simulation.call @word(%ctx, %r0, %r1, %r2, %r3, %r4, %r5, %r6, %r7) : (!simulation.context, !simulation.ref<!simulation.logic<24>>, !simulation.ref<!simulation.logic<24>>, !simulation.ref<!simulation.logic<24>>, !simulation.ref<!simulation.logic<24>>, !simulation.ref<!simulation.logic<24>>, !simulation.ref<!simulation.logic<24>>, !simulation.ref<!simulation.logic<24>>, !simulation.ref<!simulation.logic<24>>) -> ()
      %v0 = simulation.ref.load %r2 : !simulation.ref<!simulation.logic<24>> -> !simulation.logic<24>
      %e0 = simulation.logic.constant 10486282 : i24, 977920 : i24 : !simulation.logic<24>
      %ok0 = simulation.logic.compare case_eq %v0, %e0 : (!simulation.logic<24>, !simulation.logic<24>) -> i1
      %v1 = simulation.ref.load %r3 : !simulation.ref<!simulation.logic<24>> -> !simulation.logic<24>
      %e1 = simulation.logic.constant 10628906 : i24, 905408 : i24 : !simulation.logic<24>
      %ok1 = simulation.logic.compare case_eq %v1, %e1 : (!simulation.logic<24>, !simulation.logic<24>) -> i1
      %v2 = simulation.ref.load %r4 : !simulation.ref<!simulation.logic<24>> -> !simulation.logic<24>
      %e2 = simulation.logic.constant 10486058 : i24, 1047744 : i24 : !simulation.logic<24>
      %ok2 = simulation.logic.compare case_eq %v2, %e2 : (!simulation.logic<24>, !simulation.logic<24>) -> i1
      %v3 = simulation.ref.load %r5 : !simulation.ref<!simulation.logic<24>> -> !simulation.logic<24>
      %e3 = simulation.logic.constant 10486298 : i24, 1047744 : i24 : !simulation.logic<24>
      %ok3 = simulation.logic.compare case_eq %v3, %e3 : (!simulation.logic<24>, !simulation.logic<24>) -> i1
      %v4 = simulation.ref.load %r6 : !simulation.ref<!simulation.logic<24>> -> !simulation.logic<24>
      %e4 = simulation.logic.constant 11472650 : i24, 1044480 : i24 : !simulation.logic<24>
      %ok4 = simulation.logic.compare case_eq %v4, %e4 : (!simulation.logic<24>, !simulation.logic<24>) -> i1
      %v5 = simulation.ref.load %r7 : !simulation.ref<!simulation.logic<24>> -> !simulation.logic<24>
      %e5 = simulation.logic.constant 11534330 : i24, 1048560 : i24 : !simulation.logic<24>
      %ok5 = simulation.logic.compare case_eq %v5, %e5 : (!simulation.logic<24>, !simulation.logic<24>) -> i1
      %fmt = simulation.bytes.constant "word planes %0d %0d %0d %0d %0d %0d"
      %channel = arith.constant 1 : i32
      simulation.display %ctx to %channel(%fmt, %ok0, %ok1, %ok2, %ok3, %ok4, %ok5) newline = true radix = <decimal> flags = [0, 0, 0, 0, 0, 0, 0] : !simulation.bytes, i1, i1, i1, i1, i1, i1
      simulation.return
    }
    simulation.func @word(%ctx: !simulation.context {simulation.capture_kind = 0 : i32},
        %r0: !simulation.ref<!simulation.logic<24>> {simulation.capture_kind = 3 : i32, simulation.descriptor_id = 0 : i64},
        %r1: !simulation.ref<!simulation.logic<24>> {simulation.capture_kind = 3 : i32, simulation.descriptor_id = 1 : i64},
        %r2: !simulation.ref<!simulation.logic<24>> {simulation.capture_kind = 3 : i32, simulation.descriptor_id = 2 : i64},
        %r3: !simulation.ref<!simulation.logic<24>> {simulation.capture_kind = 3 : i32, simulation.descriptor_id = 3 : i64},
        %r4: !simulation.ref<!simulation.logic<24>> {simulation.capture_kind = 3 : i32, simulation.descriptor_id = 4 : i64},
        %r5: !simulation.ref<!simulation.logic<24>> {simulation.capture_kind = 3 : i32, simulation.descriptor_id = 5 : i64},
        %r6: !simulation.ref<!simulation.logic<24>> {simulation.capture_kind = 3 : i32, simulation.descriptor_id = 6 : i64},
        %r7: !simulation.ref<!simulation.logic<24>> {simulation.capture_kind = 3 : i32, simulation.descriptor_id = 7 : i64}) attributes {entry_kind = 8 : i32, code_unit_id = 1 : i64, schedule.eval.raw_captures} {
      %start = arith.constant 4 : i32
      cf.br ^head(%start : i32)
    ^head(%i: i32):
      %limit = arith.constant 20 : i32
      %more = arith.cmpi ult, %i, %limit : i32
      cf.cond_br %more, ^body, ^exit
    ^body:
      %b0 = simulation.ref.dyn_extract %r0 from %i : (!simulation.ref<!simulation.logic<24>>, i32) -> !simulation.ref<!simulation.logic<1>>
      %b1 = simulation.ref.dyn_extract %r1 from %i : (!simulation.ref<!simulation.logic<24>>, i32) -> !simulation.ref<!simulation.logic<1>>
      %b2 = simulation.ref.dyn_extract %r2 from %i : (!simulation.ref<!simulation.logic<24>>, i32) -> !simulation.ref<!simulation.logic<1>>
      %b3 = simulation.ref.dyn_extract %r3 from %i : (!simulation.ref<!simulation.logic<24>>, i32) -> !simulation.ref<!simulation.logic<1>>
      %b4 = simulation.ref.dyn_extract %r4 from %i : (!simulation.ref<!simulation.logic<24>>, i32) -> !simulation.ref<!simulation.logic<1>>
      %b5 = simulation.ref.dyn_extract %r5 from %i : (!simulation.ref<!simulation.logic<24>>, i32) -> !simulation.ref<!simulation.logic<1>>
      %b6 = simulation.ref.dyn_extract %r6 from %i : (!simulation.ref<!simulation.logic<24>>, i32) -> !simulation.ref<!simulation.logic<1>>
      %b7 = simulation.ref.dyn_extract %r7 from %i : (!simulation.ref<!simulation.logic<24>>, i32) -> !simulation.ref<!simulation.logic<1>>
      %a0 = simulation.ref.load %b0 : !simulation.ref<!simulation.logic<1>> -> !simulation.logic<1>
      %a1 = simulation.ref.load %b1 : !simulation.ref<!simulation.logic<1>> -> !simulation.logic<1>
      %v0 = simulation.logic.binary and %a0, %a1 : !simulation.logic<1>
      simulation.ref.store %v0 to %b2 : !simulation.logic<1>, !simulation.ref<!simulation.logic<1>>
      %v1 = simulation.logic.binary or %a0, %a1 : !simulation.logic<1>
      simulation.ref.store %v1 to %b3 : !simulation.logic<1>, !simulation.ref<!simulation.logic<1>>
      %v2 = simulation.logic.binary xor %a0, %a1 : !simulation.logic<1>
      simulation.ref.store %v2 to %b4 : !simulation.logic<1>, !simulation.ref<!simulation.logic<1>>
      %v3 = simulation.logic.binary xnor %a0, %a1 : !simulation.logic<1>
      simulation.ref.store %v3 to %b5 : !simulation.logic<1>, !simulation.ref<!simulation.logic<1>>
      simulation.ref.store %a0 to %b6 : !simulation.logic<1>, !simulation.ref<!simulation.logic<1>>
      %z = simulation.logic.constant 1 : i1, 1 : i1 : !simulation.logic<1>
      simulation.ref.store %z to %b7 : !simulation.logic<1>, !simulation.ref<!simulation.logic<1>>
      %one = arith.constant 1 : i32
      %next = arith.addi %i, %one : i32
      cf.br ^head(%next : i32)
    ^exit:
      simulation.return
    }
  }
}
