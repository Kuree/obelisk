// RUN: obelisk-opt %s --pass-pipeline='builtin.module(obelisk_sim.design(obelisk_sim.func(obelisk-sim-thread-process-cfg),obelisk-sim-build-compute-graph,obelisk-sim-verify-compute-graph,obelisk-sim-materialize-graph-regions,obelisk-sim-materialize-compute-fusion,obelisk-sim-specialize-static-state-nba,obelisk-sim-plan-static-superstep),encode-obelisk-sim-to-bytecode{vpi=off},convert-obelisk-sim-processes-to-llvm-coroutines)' -o %t.llvm.mlir
// RUN: FileCheck %s --check-prefix=PLAN < %t.llvm.mlir
// RUN: mlir-translate --mlir-to-llvmir %t.llvm.mlir | %llvm_dist/bin/opt -passes='coro-early,coro-split<reuse-storage>,coro-cleanup,default<O2>' | %llvm_dist/bin/llc -filetype=obj -relocation-model=pic -o %t.o
// RUN: %llvm_dist/bin/clang++ %t.o %native_support/libobelisk_rt.a %native_support/libc++.a %native_support/libc++abi.a %native_support/libunwind.a -nostdlib++ -lpthread -ldl -o %t.exe
// RUN: %t.exe --execution-tier=native 2> %t.native.err | FileCheck %s
// RUN: %t.exe --execution-tier=bytecode 2> %t.bytecode.err | FileCheck %s

// Dynamic blocking stores clip to the packed root. Unknown payload bits are
// preserved; invalid writes must neither modify data nor wake change observers.
// LRM 11.5.1 and 9.4.2. Exercise the same input in native and bytecode modes.
// PLAN: llvm.func @__obelisk_eval_fast_coordinator_v1
// CHECK: 00000000000000000000000000000001 1
// CHECK-NEXT: x0000000000000000000000000000001 0
// CHECK-NEXT: x0000000000000000000000000000001 0
// CHECK-NEXT: x0000000000000000000000000000001 0
!wide = !obelisk_sim.logic<32>
!wref = !obelisk_sim.ref<!wide>
!ref = !obelisk_sim.ref<i1>
module attributes {llvm.data_layout = "e-m:e-p:64:64-i64:64-n8:16:32:64-S128", llvm.target_triple = "x86_64-unknown-linux-gnu", obelisk.native_scheduler = 3 : i32} {
  obelisk_sim.design @dynamic_store {
    obelisk_sim.scope.decl 0
    obelisk_sim.storage.decl 0 in 0 : i1 design
    obelisk_sim.storage.decl 1 in 0 : !wide design
    obelisk_sim.storage.decl 2 in 0 : i1 design
    obelisk_sim.storage.decl 3 in 0 : !wide design
    obelisk_sim.code_unit.decl 1 in 0 root_initializer hierarchy "root"
    obelisk_sim.code_unit.decl 2 in 0 always hierarchy "clock"
    obelisk_sim.code_unit.decl 3 in 0 always hierarchy "publish"
    obelisk_sim.code_unit.decl 4 in 0 always hierarchy "change"
    obelisk_sim.code_unit.decl 6 in 0 initial hierarchy "check"
    obelisk_sim.func @root(%ctx: !obelisk_sim.context {obelisk_sim.capture_kind = 0 : i32}) attributes {entry_kind = 0 : i32, code_unit_id = 1 : i64} {
      %clk = obelisk_sim.context.storage %ctx[0] : !ref
      %data = obelisk_sim.context.storage %ctx[1] : !wref
      %change = obelisk_sim.context.storage %ctx[2] : !ref
      %index = obelisk_sim.context.storage %ctx[3] : !obelisk_sim.ref<!wide>
      %zero = arith.constant false
      %zeros = obelisk_sim.logic.constant 0 : i32, 0 : i32 : !wide
      obelisk_sim.ref.store %zero to %clk : i1, !ref
      obelisk_sim.ref.store %zeros to %data : !wide, !wref
      obelisk_sim.ref.store %zero to %change : i1, !ref
      %minusOne = obelisk_sim.logic.constant -1 : i32, 0 : i32 : !wide
      obelisk_sim.ref.store %minusOne to %index : !wide, !obelisk_sim.ref<!wide>
      %a = obelisk_sim.spawn @clock(%ctx, %clk) : !obelisk_sim.context, !ref -> !obelisk_sim.process
      %b = obelisk_sim.spawn @publish(%ctx, %clk, %data) : !obelisk_sim.context, !ref, !wref -> !obelisk_sim.process
      %c = obelisk_sim.spawn @change(%ctx, %data, %change) : !obelisk_sim.context, !wref, !ref -> !obelisk_sim.process
      %e = obelisk_sim.spawn @check(%ctx, %data, %change, %index) : !obelisk_sim.context, !wref, !ref, !obelisk_sim.ref<!wide> -> !obelisk_sim.process
      obelisk_sim.return
    }
    obelisk_sim.func @clock(%ctx: !obelisk_sim.context {obelisk_sim.capture_kind = 0 : i32}, %clk: !ref {obelisk_sim.capture_kind = 3 : i32, obelisk_sim.descriptor_id = 0 : i64}) attributes {entry_kind = 3 : i32, code_unit_id = 2 : i64} {
      cf.br ^wait
    ^wait:
      %delay = obelisk_sim.time.constant 2
      obelisk_sim.suspend.delay %delay to ^toggle {site = #obelisk_sim.continuation<id = 1>, timing = #obelisk_sim.timing_site<id = 0, kind = calendar>}
    ^toggle:
      %old = obelisk_sim.ref.load %clk : !ref -> i1
      %one = arith.constant true
      %new = arith.xori %old, %one : i1
      obelisk_sim.ref.store %new to %clk : i1, !ref
      cf.br ^wait
    }
    obelisk_sim.func @publish(%ctx: !obelisk_sim.context {obelisk_sim.capture_kind = 0 : i32}, %clk: !ref {obelisk_sim.capture_kind = 3 : i32, obelisk_sim.descriptor_id = 0 : i64}, %data: !wref {obelisk_sim.capture_kind = 3 : i32, obelisk_sim.descriptor_id = 1 : i64}) attributes {entry_kind = 3 : i32, code_unit_id = 3 : i64} {
      cf.br ^wait
    ^wait:
      obelisk_sim.suspend.change %clk to ^write {site = #obelisk_sim.continuation<id = 2>} : !ref
    ^write:
      %index = obelisk_sim.context.storage %ctx[3] : !obelisk_sim.ref<!wide>
      %low = obelisk_sim.ref.load %index : !obelisk_sim.ref<!wide> -> !wide
      %slice = obelisk_sim.ref.dyn_extract %data from %low : (!wref, !wide) -> !obelisk_sim.ref<!obelisk_sim.logic<2>>
      %payload = obelisk_sim.logic.constant 2 : i2, 1 : i2 : !obelisk_sim.logic<2>
      obelisk_sim.ref.store %payload to %slice : !obelisk_sim.logic<2>, !obelisk_sim.ref<!obelisk_sim.logic<2>>
      cf.br ^wait
    }
    obelisk_sim.func @change(%ctx: !obelisk_sim.context {obelisk_sim.capture_kind = 0 : i32}, %data: !wref {obelisk_sim.capture_kind = 3 : i32, obelisk_sim.descriptor_id = 1 : i64}, %flag: !ref {obelisk_sim.capture_kind = 3 : i32, obelisk_sim.descriptor_id = 2 : i64}) attributes {entry_kind = 3 : i32, code_unit_id = 4 : i64} {
      cf.br ^wait
    ^wait:
      obelisk_sim.suspend.change %data to ^changed {site = #obelisk_sim.continuation<id = 3>} : !wref
    ^changed:
      %one = arith.constant true
      %old = obelisk_sim.ref.load %flag : !ref -> i1
      %next = arith.xori %old, %one : i1
      obelisk_sim.ref.store %next to %flag : i1, !ref
      cf.br ^wait
    }
    obelisk_sim.func @check(%ctx: !obelisk_sim.context {obelisk_sim.capture_kind = 0 : i32}, %data: !wref {obelisk_sim.capture_kind = 3 : i32, obelisk_sim.descriptor_id = 1 : i64}, %change: !ref {obelisk_sim.capture_kind = 3 : i32, obelisk_sim.descriptor_id = 2 : i64}, %index: !obelisk_sim.ref<!wide> {obelisk_sim.capture_kind = 3 : i32, obelisk_sim.descriptor_id = 3 : i64}) attributes {entry_kind = 1 : i32, code_unit_id = 6 : i64} {
      %format = obelisk_sim.bytes.constant "%032b %b"
      %stdout = arith.constant 1 : i32
      %delay = obelisk_sim.time.constant 3
      obelisk_sim.suspend.delay %delay to ^first {site = #obelisk_sim.continuation<id = 5>, timing = #obelisk_sim.timing_site<id = 1, kind = calendar>}
    ^first:
      %v0 = obelisk_sim.ref.load %data : !wref -> !wide
      %c0 = obelisk_sim.ref.load %change : !ref -> i1
      obelisk_sim.display %ctx to %stdout(%format, %v0, %c0) newline = true radix = 10 flags = [0, 0, 0] : !obelisk_sim.bytes, !wide, i1
      %highIndex = obelisk_sim.logic.constant 31 : i32, 0 : i32 : !wide
      obelisk_sim.ref.store %highIndex to %index : !wide, !obelisk_sim.ref<!wide>
      %two = obelisk_sim.time.constant 2
      obelisk_sim.suspend.delay %two to ^second {site = #obelisk_sim.continuation<id = 6>, timing = #obelisk_sim.timing_site<id = 2, kind = calendar>}
    ^second:
      %v1 = obelisk_sim.ref.load %data : !wref -> !wide
      %c1 = obelisk_sim.ref.load %change : !ref -> i1
      obelisk_sim.display %ctx to %stdout(%format, %v1, %c1) newline = true radix = 10 flags = [0, 0, 0] : !obelisk_sim.bytes, !wide, i1
      %invalidIndex = obelisk_sim.logic.constant 32 : i32, 0 : i32 : !wide
      obelisk_sim.ref.store %invalidIndex to %index : !wide, !obelisk_sim.ref<!wide>
      obelisk_sim.suspend.delay %two to ^third {site = #obelisk_sim.continuation<id = 7>, timing = #obelisk_sim.timing_site<id = 3, kind = calendar>}
    ^third:
      %v2 = obelisk_sim.ref.load %data : !wref -> !wide
      %c2 = obelisk_sim.ref.load %change : !ref -> i1
      obelisk_sim.display %ctx to %stdout(%format, %v2, %c2) newline = true radix = 10 flags = [0, 0, 0] : !obelisk_sim.bytes, !wide, i1
      %unknownIndex = obelisk_sim.logic.constant 0 : i32, 1 : i32 : !wide
      obelisk_sim.ref.store %unknownIndex to %index : !wide, !obelisk_sim.ref<!wide>
      obelisk_sim.suspend.delay %two to ^fourth {site = #obelisk_sim.continuation<id = 8>, timing = #obelisk_sim.timing_site<id = 4, kind = calendar>}
    ^fourth:
      %v3 = obelisk_sim.ref.load %data : !wref -> !wide
      %c3 = obelisk_sim.ref.load %change : !ref -> i1
      obelisk_sim.display %ctx to %stdout(%format, %v3, %c3) newline = true radix = 10 flags = [0, 0, 0] : !obelisk_sim.bytes, !wide, i1
      %status = arith.constant 0 : i32
      obelisk_sim.finish %ctx, %status
      obelisk_sim.return
    }
  }
}
