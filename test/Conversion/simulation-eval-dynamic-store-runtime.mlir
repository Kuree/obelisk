// RUN: obelisk-opt %s --pass-pipeline='builtin.module(simulation.design(simulation.func(obelisk-sim-thread-process-cfg),obelisk-sim-build-compute-graph,obelisk-sim-verify-compute-graph,obelisk-sim-materialize-graph-regions,obelisk-sim-materialize-compute-fusion,obelisk-sim-specialize-static-state-nba,obelisk-sim-plan-static-superstep),encode-obelisk-sim-to-bytecode{vpi=off},convert-obelisk-sim-processes-to-llvm-coroutines)' -o %t.llvm.mlir
// RUN: FileCheck %s --check-prefix=PLAN < %t.llvm.mlir

// Runtime behavior is checked in ../Runtime/simulation-eval-dynamic-store-runtime.test.

// Dynamic blocking stores clip to the packed root. Unknown payload bits are
// preserved; invalid writes must neither modify data nor wake change observers.
// LRM 11.5.1 and 9.4.2. Exercise the same input in native and bytecode modes.
// PLAN: llvm.func @__obelisk_eval_dispatch_v1
!wide = !simulation.logic<32>
!wref = !simulation.ref<!wide>
!ref = !simulation.ref<i1>
module attributes {llvm.data_layout = "e-m:e-p:64:64-i64:64-n8:16:32:64-S128", llvm.target_triple = "x86_64-unknown-linux-gnu", schedule.native_scheduler = 3 : i32} {
  simulation.design @dynamic_store {
    simulation.scope.decl 0
    simulation.storage.decl 0 in 0 : i1 design
    simulation.storage.decl 1 in 0 : !wide design
    simulation.storage.decl 2 in 0 : i1 design
    simulation.storage.decl 3 in 0 : !wide design
    simulation.code_unit.decl 1 in 0 root_initializer hierarchy "root"
    simulation.code_unit.decl 2 in 0 always hierarchy "clock"
    simulation.code_unit.decl 3 in 0 always hierarchy "publish"
    simulation.code_unit.decl 4 in 0 always hierarchy "change"
    simulation.code_unit.decl 6 in 0 initial hierarchy "check"
    simulation.func @root(%ctx: !simulation.context {simulation.capture_kind = 0 : i32}) attributes {entry_kind = 0 : i32, code_unit_id = 1 : i64} {
      %clk = simulation.context.storage %ctx[0] : !ref
      %data = simulation.context.storage %ctx[1] : !wref
      %change = simulation.context.storage %ctx[2] : !ref
      %index = simulation.context.storage %ctx[3] : !simulation.ref<!wide>
      %zero = arith.constant false
      %zeros = simulation.logic.constant 0 : i32, 0 : i32 : !wide
      simulation.ref.store %zero to %clk : i1, !ref
      simulation.ref.store %zeros to %data : !wide, !wref
      simulation.ref.store %zero to %change : i1, !ref
      %minusOne = simulation.logic.constant -1 : i32, 0 : i32 : !wide
      simulation.ref.store %minusOne to %index : !wide, !simulation.ref<!wide>
      %a = simulation.spawn @clock(%ctx, %clk) : !simulation.context, !ref -> !simulation.process
      %b = simulation.spawn @publish(%ctx, %clk, %data) : !simulation.context, !ref, !wref -> !simulation.process
      %c = simulation.spawn @change(%ctx, %data, %change) : !simulation.context, !wref, !ref -> !simulation.process
      %e = simulation.spawn @check(%ctx, %data, %change, %index) : !simulation.context, !wref, !ref, !simulation.ref<!wide> -> !simulation.process
      simulation.return
    }
    simulation.func @clock(%ctx: !simulation.context {simulation.capture_kind = 0 : i32}, %clk: !ref {simulation.capture_kind = 3 : i32, simulation.descriptor_id = 0 : i64}) attributes {entry_kind = 3 : i32, code_unit_id = 2 : i64} {
      cf.br ^wait
    ^wait:
      %delay = simulation.time.constant 2
      simulation.suspend.delay %delay to ^toggle {site = #schedule.continuation<id = 1>, timing = #schedule.timing_site<id = 0, kind = calendar>}
    ^toggle:
      %old = simulation.ref.load %clk : !ref -> i1
      %one = arith.constant true
      %new = arith.xori %old, %one : i1
      simulation.ref.store %new to %clk : i1, !ref
      cf.br ^wait
    }
    simulation.func @publish(%ctx: !simulation.context {simulation.capture_kind = 0 : i32}, %clk: !ref {simulation.capture_kind = 3 : i32, simulation.descriptor_id = 0 : i64}, %data: !wref {simulation.capture_kind = 3 : i32, simulation.descriptor_id = 1 : i64}) attributes {entry_kind = 3 : i32, code_unit_id = 3 : i64} {
      cf.br ^wait
    ^wait:
      simulation.suspend.change %clk to ^write {site = #schedule.continuation<id = 2>} : !ref
    ^write:
      %index = simulation.context.storage %ctx[3] : !simulation.ref<!wide>
      %low = simulation.ref.load %index : !simulation.ref<!wide> -> !wide
      %slice = simulation.ref.dyn_extract %data from %low : (!wref, !wide) -> !simulation.ref<!simulation.logic<2>>
      %payload = simulation.logic.constant 2 : i2, 1 : i2 : !simulation.logic<2>
      simulation.ref.store %payload to %slice : !simulation.logic<2>, !simulation.ref<!simulation.logic<2>>
      cf.br ^wait
    }
    simulation.func @change(%ctx: !simulation.context {simulation.capture_kind = 0 : i32}, %data: !wref {simulation.capture_kind = 3 : i32, simulation.descriptor_id = 1 : i64}, %flag: !ref {simulation.capture_kind = 3 : i32, simulation.descriptor_id = 2 : i64}) attributes {entry_kind = 3 : i32, code_unit_id = 4 : i64} {
      cf.br ^wait
    ^wait:
      simulation.suspend.change %data to ^changed {site = #schedule.continuation<id = 3>} : !wref
    ^changed:
      %one = arith.constant true
      %old = simulation.ref.load %flag : !ref -> i1
      %next = arith.xori %old, %one : i1
      simulation.ref.store %next to %flag : i1, !ref
      cf.br ^wait
    }
    simulation.func @check(%ctx: !simulation.context {simulation.capture_kind = 0 : i32}, %data: !wref {simulation.capture_kind = 3 : i32, simulation.descriptor_id = 1 : i64}, %change: !ref {simulation.capture_kind = 3 : i32, simulation.descriptor_id = 2 : i64}, %index: !simulation.ref<!wide> {simulation.capture_kind = 3 : i32, simulation.descriptor_id = 3 : i64}) attributes {entry_kind = 1 : i32, code_unit_id = 6 : i64} {
      %format = simulation.bytes.constant "%032b %b"
      %stdout = arith.constant 1 : i32
      %delay = simulation.time.constant 3
      simulation.suspend.delay %delay to ^first {site = #schedule.continuation<id = 5>, timing = #schedule.timing_site<id = 1, kind = calendar>}
    ^first:
      %v0 = simulation.ref.load %data : !wref -> !wide
      %c0 = simulation.ref.load %change : !ref -> i1
      simulation.display %ctx to %stdout(%format, %v0, %c0) newline = true radix = <decimal> flags = [0, 0, 0] : !simulation.bytes, !wide, i1
      %highIndex = simulation.logic.constant 31 : i32, 0 : i32 : !wide
      simulation.ref.store %highIndex to %index : !wide, !simulation.ref<!wide>
      %two = simulation.time.constant 2
      simulation.suspend.delay %two to ^second {site = #schedule.continuation<id = 6>, timing = #schedule.timing_site<id = 2, kind = calendar>}
    ^second:
      %v1 = simulation.ref.load %data : !wref -> !wide
      %c1 = simulation.ref.load %change : !ref -> i1
      simulation.display %ctx to %stdout(%format, %v1, %c1) newline = true radix = <decimal> flags = [0, 0, 0] : !simulation.bytes, !wide, i1
      %invalidIndex = simulation.logic.constant 32 : i32, 0 : i32 : !wide
      simulation.ref.store %invalidIndex to %index : !wide, !simulation.ref<!wide>
      simulation.suspend.delay %two to ^third {site = #schedule.continuation<id = 7>, timing = #schedule.timing_site<id = 3, kind = calendar>}
    ^third:
      %v2 = simulation.ref.load %data : !wref -> !wide
      %c2 = simulation.ref.load %change : !ref -> i1
      simulation.display %ctx to %stdout(%format, %v2, %c2) newline = true radix = <decimal> flags = [0, 0, 0] : !simulation.bytes, !wide, i1
      %unknownIndex = simulation.logic.constant 0 : i32, 1 : i32 : !wide
      simulation.ref.store %unknownIndex to %index : !wide, !simulation.ref<!wide>
      simulation.suspend.delay %two to ^fourth {site = #schedule.continuation<id = 8>, timing = #schedule.timing_site<id = 4, kind = calendar>}
    ^fourth:
      %v3 = simulation.ref.load %data : !wref -> !wide
      %c3 = simulation.ref.load %change : !ref -> i1
      simulation.display %ctx to %stdout(%format, %v3, %c3) newline = true radix = <decimal> flags = [0, 0, 0] : !simulation.bytes, !wide, i1
      %status = arith.constant 0 : i32
      simulation.finish %ctx, %status
      simulation.return
    }
  }
}
