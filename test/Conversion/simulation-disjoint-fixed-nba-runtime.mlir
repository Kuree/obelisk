// RUN: obelisk-opt %s --pass-pipeline='builtin.module(obelisk_sim.design(obelisk_sim.func(obelisk-sim-thread-process-cfg),obelisk-sim-build-compute-graph,obelisk-sim-verify-compute-graph,obelisk-sim-materialize-graph-regions,obelisk-sim-materialize-compute-fusion,obelisk-sim-specialize-static-state-nba,obelisk-sim-plan-static-superstep),encode-obelisk-sim-to-bytecode{vpi=off},convert-obelisk-sim-processes-to-llvm-coroutines)' -o %t.llvm.mlir
// RUN: FileCheck %s --check-prefix=PLAN < %t.llvm.mlir

// Runtime behavior is checked in ../Runtime/simulation-disjoint-fixed-nba-runtime.test.

// Three fixed packed words share a 96-bit root. Distinct source
// sites must not disable Eval when their exact physical slices are disjoint.
// Each keeps its own pending slot; samples straddle the first clock edge.
// PLAN-COUNT-3: llvm.mlir.global internal @__obelisk_eval_nba_valid_
// PLAN-LABEL: llvm.func @__obelisk_aot_schedule_run_v1(
// PLAN: llvm.call @obelisk_rt_v1_scheduler_prepare_periodic_aot

!word = !obelisk_sim.logic<32>
!words = !obelisk_sim.packed_array<2 : 0 x !word>
!clockref = !obelisk_sim.ref<!obelisk_sim.logic<1>>
!dataref = !obelisk_sim.ref<!words>

module attributes {llvm.data_layout = "e-m:e-p:64:64-i64:64-n8:16:32:64-S128", llvm.target_triple = "x86_64-unknown-linux-gnu", schedule.native_scheduler = 3 : i32} {
  obelisk_sim.design @fixed_words {
    obelisk_sim.scope.decl 0 hierarchy "fixed_words"
    obelisk_sim.code_unit.decl 1 in 0 root_initializer hierarchy "fixed_words.root"
    obelisk_sim.code_unit.decl 2 in 0 always hierarchy "fixed_words.clock"
    obelisk_sim.code_unit.decl 3 in 0 always hierarchy "fixed_words.update"
    obelisk_sim.code_unit.decl 4 in 0 initial hierarchy "fixed_words.check"
    obelisk_sim.storage.decl 0 in 0 : !obelisk_sim.logic<1> design hierarchy "fixed_words.clk"
    obelisk_sim.storage.decl 1 in 0 : !words design hierarchy "fixed_words.data"

    obelisk_sim.func @root(%ctx: !obelisk_sim.context {obelisk_sim.capture_kind = 0 : i32}) attributes {entry_kind = 0 : i32, code_unit_id = 1 : i64} {
      %clk = obelisk_sim.context.storage %ctx[0] : !clockref
      %data = obelisk_sim.context.storage %ctx[1] : !dataref
      %zero = obelisk_sim.logic.constant 0 : i1, 0 : i1 : !obelisk_sim.logic<1>
      obelisk_sim.ref.store %zero to %clk : !obelisk_sim.logic<1>, !clockref
      %zero96 = obelisk_sim.logic.constant 0 : i96, 0 : i96 : !obelisk_sim.logic<96>
      %packed = obelisk_sim.packed.unflatten %zero96 : (!obelisk_sim.logic<96>) -> !words
      obelisk_sim.ref.store %packed to %data : !words, !dataref
      %a = obelisk_sim.spawn @clock(%ctx, %clk) : !obelisk_sim.context, !clockref -> !obelisk_sim.process
      %b = obelisk_sim.spawn @update(%ctx, %clk, %data) : !obelisk_sim.context, !clockref, !dataref -> !obelisk_sim.process
      %c = obelisk_sim.spawn @check(%ctx, %data) : !obelisk_sim.context, !dataref -> !obelisk_sim.process
      obelisk_sim.return
    }

    obelisk_sim.func @clock(%ctx: !obelisk_sim.context {obelisk_sim.capture_kind = 0 : i32}, %clk: !clockref {obelisk_sim.capture_kind = 3 : i32, obelisk_sim.descriptor_id = 0 : i64}) attributes {entry_kind = 3 : i32, code_unit_id = 2 : i64} {
      cf.br ^wait
    ^wait:
      %delay = obelisk_sim.time.constant 2
      obelisk_sim.suspend.delay %delay to ^toggle {site = #schedule.continuation<id = 1>, timing = #schedule.timing_site<id = 0, kind = calendar>}
    ^toggle:
      %old = obelisk_sim.ref.load %clk : !clockref -> !obelisk_sim.logic<1>
      %new = obelisk_sim.logic.unary bit_not %old : (!obelisk_sim.logic<1>) -> !obelisk_sim.logic<1>
      obelisk_sim.ref.store %new to %clk : !obelisk_sim.logic<1>, !clockref
      cf.br ^wait
    }

    obelisk_sim.func @update(%ctx: !obelisk_sim.context {obelisk_sim.capture_kind = 0 : i32}, %clk: !clockref {obelisk_sim.capture_kind = 3 : i32, obelisk_sim.descriptor_id = 0 : i64}, %data: !dataref {obelisk_sim.capture_kind = 3 : i32, obelisk_sim.descriptor_id = 1 : i64}) attributes {entry_kind = 3 : i32, code_unit_id = 3 : i64} {
      cf.br ^wait
    ^wait:
      obelisk_sim.suspend.change %clk to ^update {site = #schedule.continuation<id = 2>} : !clockref
    ^update:
      %a = obelisk_sim.ref.subelement %data[[0]] : !dataref -> !obelisk_sim.ref<!word>
      %b = obelisk_sim.ref.subelement %data[[1]] : !dataref -> !obelisk_sim.ref<!word>
      %c = obelisk_sim.ref.subelement %data[[2]] : !dataref -> !obelisk_sim.ref<!word>
      %x = obelisk_sim.logic.constant 17 : i32, 0 : i32 : !word
      %y = obelisk_sim.logic.constant 34 : i32, 0 : i32 : !word
      %z = obelisk_sim.logic.constant 51 : i32, 0 : i32 : !word
      obelisk_sim.nba.enqueue %x to %a : (!word, !obelisk_sim.ref<!word>) -> ()
      obelisk_sim.nba.enqueue %y to %b : (!word, !obelisk_sim.ref<!word>) -> ()
      obelisk_sim.nba.enqueue %z to %c : (!word, !obelisk_sim.ref<!word>) -> ()
      cf.br ^wait
    }

    obelisk_sim.func @check(%ctx: !obelisk_sim.context {obelisk_sim.capture_kind = 0 : i32}, %data: !dataref {obelisk_sim.capture_kind = 3 : i32, obelisk_sim.descriptor_id = 1 : i64}) attributes {entry_kind = 1 : i32, code_unit_id = 4 : i64} {
      %delay = obelisk_sim.time.constant 1
      obelisk_sim.suspend.delay %delay to ^before
    ^before:
      %first = obelisk_sim.ref.load %data : !dataref -> !words
      %firstBits = obelisk_sim.packed.flatten %first : (!words) -> !obelisk_sim.logic<96>
      %format = obelisk_sim.bytes.constant "%024h"
      %stdout = arith.constant 1 : i32
      obelisk_sim.display %ctx to %stdout(%format, %firstBits) newline = true radix = 10 flags = [0, 0] : !obelisk_sim.bytes, !obelisk_sim.logic<96>
      %two = obelisk_sim.time.constant 2
      obelisk_sim.suspend.delay %two to ^after
    ^after:
      %last = obelisk_sim.ref.load %data : !dataref -> !words
      %lastBits = obelisk_sim.packed.flatten %last : (!words) -> !obelisk_sim.logic<96>
      obelisk_sim.display %ctx to %stdout(%format, %lastBits) newline = true radix = 10 flags = [0, 0] : !obelisk_sim.bytes, !obelisk_sim.logic<96>
      %status = arith.constant 0 : i32
      obelisk_sim.finish %ctx, %status
      obelisk_sim.return
    }
  }
}
