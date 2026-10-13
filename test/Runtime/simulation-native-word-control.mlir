// RUN: obelisk-opt %s --schedule-optimize-native-words -o %t.words
// RUN: FileCheck %s --check-prefix=WORDS < %t.words
// RUN: obelisk-opt %s --schedule-optimize-native-words='predication-operation-budget=1' -o %t.budget.mlir
// RUN: FileCheck %s --check-prefix=BUDGET < %t.budget.mlir
// RUN: obelisk-opt %t.words --convert-schedule-word-regions --convert-obelisk-sim-processes-to-llvm-coroutines | mlir-translate --mlir-to-llvmir | %llvm_dist/bin/opt -passes='coro-early,coro-split<reuse-storage>,coro-cleanup,default<O0>' | %llc -filetype=obj -relocation-model=pic -o %t.o
// RUN: %llvm_dist/bin/clang++ %t.o %native_support/libobelisk_rt.a %native_support/libc++.a %native_support/libc++abi.a %native_support/libunwind.a -nostdlib++ -lpthread -ldl -o %t.exe
// RUN: %t.exe | FileCheck %s

// Every triple of 0,1,X,Z appears in one of the 64 lanes.
// LRM 12.4: procedural X/Z conditions select the false edge. A packed phi
// preserves Z exactly; this differs from the ambiguous ?: expression rule.
// LRM 4.6: repeated writes retain statement order in each selected lane.
// WORDS-LABEL: simulation.func @control
// WORDS: schedule.word.region 64
// WORDS-NOT: cf.cond_br
// WORDS: schedule.word.yield
// Exhausting speculative compile work retains the scalar loop and its X/Z
// behavior. It introduces no runtime recovery or alternate execution path.
// BUDGET-LABEL: simulation.func @control
// BUDGET-NOT: schedule.word.region
// BUDGET: cf.cond_br
// BUDGET: simulation.ref.store
// BUDGET-NOT: schedule.word.region
// BUDGET: simulation.return
// CHECK: control exit 1 1
// CHECK: control 1 1 1
!bit = !simulation.logic<1>
!word = !simulation.logic<64>
!ref = !simulation.ref<!simulation.logic<64>>
!bitref = !simulation.ref<!simulation.logic<1>>
!packed = !simulation.packed_array<0 : 0 x !simulation.logic<1>>
module attributes {llvm.data_layout = "e-m:e-p:64:64-i64:64-n8:16:32:64-S128", llvm.target_triple = "x86_64-unknown-linux-gnu"} {
  simulation.design @control attributes {
    schedule.nba.transient_observable = array<i64>,
    schedule.nba.private_influence = array<i64: 3, 4, 5>,
    compute_graph = #schedule.graph<version = 1, vpi = off, workers = 1,
      nodes = [], edges = [], regions = [
        #schedule.region<kind = active, groups = []>,
        #schedule.region<kind = nba, groups = []>,
        #schedule.region<kind = observed, groups = []>,
        #schedule.region<kind = reactive, groups = []>,
        #schedule.region<kind = postponed, groups = []>]>} {
    simulation.scope.decl 0
    simulation.code_unit.decl 1 in 0 root_initializer hierarchy "root"
    simulation.code_unit.decl 2 in 0 function hierarchy "control"
    simulation.storage.decl 0 in 0 : !word design
    simulation.storage.decl 1 in 0 : !word design
    simulation.storage.decl 2 in 0 : !word design
    simulation.storage.decl 3 in 0 : !word design
    simulation.storage.decl 4 in 0 : !word design
    simulation.storage.decl 5 in 0 : !word design
    simulation.func @root(%ctx: !simulation.context {simulation.capture_kind = 0 : i32}) attributes {entry_kind = 0 : i32, code_unit_id = 1 : i64} {
      %a = simulation.context.storage %ctx[0] : !ref
      %b = simulation.context.storage %ctx[1] : !ref
      %c = simulation.context.storage %ctx[2] : !ref
      %p = simulation.context.storage %ctx[3] : !ref
      %m = simulation.context.storage %ctx[4] : !ref
      %o = simulation.context.storage %ctx[5] : !ref
      %av = simulation.logic.constant 12297829382473034410 : i64, 14757395258967641292 : i64 : !word
      %bv = simulation.logic.constant 17361641481138401520 : i64, 18374966859414961920 : i64 : !word
      %cv = simulation.logic.constant 18446462603027742720 : i64, 18446744069414584320 : i64 : !word
      %z = simulation.logic.constant -1 : i64, -1 : i64 : !word
      simulation.ref.store %av to %a : !word, !ref
      simulation.ref.store %bv to %b : !word, !ref
      simulation.ref.store %cv to %c : !word, !ref
      simulation.ref.store %z to %m : !word, !ref
      simulation.call @control(%ctx, %a, %b, %c, %p, %m, %o) : (!simulation.context, !ref, !ref, !ref, !ref, !ref, !ref) -> ()
      %pv = simulation.ref.load %p : !ref -> !word
      %mv = simulation.ref.load %m : !ref -> !word
      %ov = simulation.ref.load %o : !ref -> !word
      %pe = simulation.logic.constant 18301819786775830560 : i64, 18437173774747968000 : i64 : !word
      %me = simulation.logic.constant 18302063728033398269 : i64, 18437173778470272989 : i64 : !word
      %oe = simulation.logic.constant 18301819649336877056 : i64, 18446181111441662464 : i64 : !word
      %pok = simulation.logic.compare case_eq %pv, %pe : (!word, !word) -> i1
      %mok = simulation.logic.compare case_eq %mv, %me : (!word, !word) -> i1
      %ook = simulation.logic.compare case_eq %ov, %oe : (!word, !word) -> i1
      %fmt = simulation.bytes.constant "control %0d %0d %0d"
      %channel = arith.constant 1 : i32
      simulation.display %ctx to %channel(%fmt, %pok, %mok, %ook) newline = true radix = <decimal> flags = [0, 0, 0, 0] : !simulation.bytes, i1, i1, i1
      simulation.return
    }
    simulation.func @control(
      %ctx: !simulation.context {simulation.capture_kind = 0 : i32},
      %a: !ref {simulation.capture_kind = 3 : i32, simulation.descriptor_id = 0 : i64},
      %b: !ref {simulation.capture_kind = 3 : i32, simulation.descriptor_id = 1 : i64},
      %c: !ref {simulation.capture_kind = 3 : i32, simulation.descriptor_id = 2 : i64},
      %p: !ref {simulation.capture_kind = 3 : i32, simulation.descriptor_id = 3 : i64},
      %m: !ref {simulation.capture_kind = 3 : i32, simulation.descriptor_id = 4 : i64},
      %o: !ref {simulation.capture_kind = 3 : i32, simulation.descriptor_id = 5 : i64}) attributes {entry_kind = 8 : i32, code_unit_id = 2 : i64, schedule.eval.raw_captures} {
      %zero = arith.constant 0 : i32
      %z = simulation.logic.constant 1 : i1, 1 : i1 : !bit
      cf.br ^head(%z, %zero : !bit, i32)
    ^head(%saved: !bit, %i: i32):
      %limit = arith.constant 64 : i32
      %more = arith.cmpi ult, %i, %limit : i32
      cf.cond_br %more, ^body, ^exit(%i, %saved : i32, !bit)
    ^body:
      %ar = simulation.ref.dyn_extract %a from %i : (!ref, i32) -> !bitref
      %br = simulation.ref.dyn_extract %b from %i : (!ref, i32) -> !bitref
      %cr = simulation.ref.dyn_extract %c from %i : (!ref, i32) -> !bitref
      %pr = simulation.ref.dyn_extract %p from %i : (!ref, i32) -> !bitref
      %mr = simulation.ref.dyn_extract %m from %i : (!ref, i32) -> !bitref
      %orr = simulation.ref.dyn_extract %o from %i : (!ref, i32) -> !bitref
      %av = simulation.ref.load %ar : !bitref -> !bit
      %bv = simulation.ref.load %br : !bitref -> !bit
      %cv = simulation.ref.load %cr : !bitref -> !bit
      %condition = simulation.logic.is_true %av : !bit
      cf.cond_br %condition, ^then, ^else
    ^then:
      simulation.ref.store %bv to %mr : !bit, !bitref
      %bp = simulation.packed.unflatten %bv : (!bit) -> !packed
      cf.br ^join(%bp : !packed)
    ^else:
      %cp = simulation.packed.unflatten %cv : (!bit) -> !packed
      cf.br ^join(%cp : !packed)
    ^join(%value: !packed):
      // A known-condition packed select preserves both planes of its arm.
      %fallback = simulation.packed.unflatten %cv : (!bit) -> !packed
      %selected = arith.select %condition, %value, %fallback : !packed
      %flat = simulation.packed.flatten %selected : (!packed) -> !bit
      simulation.ref.store %flat to %pr : !bit, !bitref
      simulation.ref.store %flat to %orr : !bit, !bitref
      %second = simulation.logic.is_true %bv : !bit
      cf.cond_br %second, ^overwrite, ^advance
    ^overwrite:
      simulation.ref.store %cv to %orr : !bit, !bitref
      cf.br ^advance
    ^advance:
      %one = arith.constant 1 : i32
      %next = arith.addi %i, %one : i32
      cf.br ^head(%saved, %next : !bit, i32)
    ^exit(%last: i32, %ret: !bit):
      %expected = arith.constant 64 : i32
      %index_ok = arith.cmpi eq, %last, %expected : i32
      %saved_ok = simulation.logic.compare case_eq %ret, %z : (!bit, !bit) -> i1
      %fmt = simulation.bytes.constant "control exit %0d %0d"
      %channel = arith.constant 1 : i32
      simulation.display %ctx to %channel(%fmt, %index_ok, %saved_ok) newline = true radix = <decimal> flags = [0, 0, 0] : !simulation.bytes, i1, i1
      simulation.return
    }
  }
}
