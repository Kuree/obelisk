// RUN: obelisk-opt %s --pass-pipeline='builtin.module(obelisk_sim.design(obelisk_sim.func(obelisk-sim-thread-process-cfg),obelisk-sim-build-compute-graph,obelisk-sim-verify-compute-graph,obelisk-sim-materialize-graph-regions,obelisk-sim-materialize-compute-fusion,obelisk-sim-specialize-static-state-nba,obelisk-sim-plan-static-superstep),encode-obelisk-sim-to-bytecode{vpi=off},convert-obelisk-sim-processes-to-llvm-coroutines)' -o %t.llvm.mlir
// RUN: FileCheck %s --check-prefix=PLAN < %t.llvm.mlir

// Runtime behavior is checked in ../Runtime/simulation-exclusive-fixed-nba-runtime.test.

// Two 34-bit words have alternative writes in exclusive CFG arms.
// Verify non-byte-aligned / cross-word slots and four-state publication. On
// the second edge only the OTHER two slots are valid; stale first-edge values
// must not be replayed over them. The runtime companion checks the resulting values.
// PLAN-COUNT-4: llvm.mlir.global internal @__obelisk_eval_nba_valid_
// PLAN-LABEL: llvm.func @__obelisk_aot_schedule_run_v1(
// PLAN: llvm.call @obelisk_rt_v1_scheduler_prepare_periodic_aot

!word = !obelisk_sim.logic<34>
!words = !obelisk_sim.unpacked_array<0 : 1 x !word>
!clockref = !obelisk_sim.ref<!obelisk_sim.logic<1>>
!dataref = !obelisk_sim.ref<!words>

module attributes {llvm.data_layout = "e-m:e-p:64:64-i64:64-n8:16:32:64-S128", llvm.target_triple = "x86_64-unknown-linux-gnu", obelisk.native_scheduler = 3 : i32} {
  obelisk_sim.design @exclusive_words {
    obelisk_sim.scope.decl 0 hierarchy "exclusive_words"
    obelisk_sim.code_unit.decl 1 in 0 root_initializer hierarchy "exclusive_words.root"
    obelisk_sim.code_unit.decl 2 in 0 always hierarchy "exclusive_words.clock"
    obelisk_sim.code_unit.decl 3 in 0 always hierarchy "exclusive_words.update"
    obelisk_sim.code_unit.decl 4 in 0 initial hierarchy "exclusive_words.check"
    obelisk_sim.storage.decl 0 in 0 : !obelisk_sim.logic<1> design hierarchy "exclusive_words.clk"
    obelisk_sim.storage.decl 1 in 0 : !words design hierarchy "exclusive_words.data"
    obelisk_sim.func @root(%ctx: !obelisk_sim.context {obelisk_sim.capture_kind = 0 : i32}) attributes {entry_kind = 0 : i32, code_unit_id = 1 : i64} {
      %clk = obelisk_sim.context.storage %ctx[0] : !clockref
      %data = obelisk_sim.context.storage %ctx[1] : !dataref
      %zero = obelisk_sim.logic.constant 0 : i1, 0 : i1 : !obelisk_sim.logic<1>
      obelisk_sim.ref.store %zero to %clk : !obelisk_sim.logic<1>, !clockref
      %a = obelisk_sim.spawn @clock(%ctx, %clk) : !obelisk_sim.context, !clockref -> !obelisk_sim.process
      %b = obelisk_sim.spawn @update(%ctx, %clk, %data) : !obelisk_sim.context, !clockref, !dataref -> !obelisk_sim.process
      %c = obelisk_sim.spawn @check(%ctx, %data) : !obelisk_sim.context, !dataref -> !obelisk_sim.process
      obelisk_sim.return
    }
    obelisk_sim.func @clock(%ctx: !obelisk_sim.context {obelisk_sim.capture_kind = 0 : i32}, %clk: !clockref {obelisk_sim.capture_kind = 3 : i32, obelisk_sim.descriptor_id = 0 : i64}) attributes {entry_kind = 3 : i32, code_unit_id = 2 : i64} {
      cf.br ^wait
    ^wait:
      %delay = obelisk_sim.time.constant 2
      obelisk_sim.suspend.delay %delay to ^toggle {site = #obelisk_sim.continuation<id = 1>, timing = #obelisk_sim.timing_site<id = 0, kind = calendar>}
    ^toggle:
      %old = obelisk_sim.ref.load %clk : !clockref -> !obelisk_sim.logic<1>
      %new = obelisk_sim.logic.unary bit_not %old : (!obelisk_sim.logic<1>) -> !obelisk_sim.logic<1>
      obelisk_sim.ref.store %new to %clk : !obelisk_sim.logic<1>, !clockref
      cf.br ^wait
    }
    obelisk_sim.func @update(%ctx: !obelisk_sim.context {obelisk_sim.capture_kind = 0 : i32}, %clk: !clockref {obelisk_sim.capture_kind = 3 : i32, obelisk_sim.descriptor_id = 0 : i64}, %data: !dataref {obelisk_sim.capture_kind = 3 : i32, obelisk_sim.descriptor_id = 1 : i64}) attributes {entry_kind = 3 : i32, code_unit_id = 3 : i64} {
      cf.br ^wait
    ^wait:
      obelisk_sim.suspend.change %clk to ^update {site = #obelisk_sim.continuation<id = 2>} : !clockref
    ^update:
      %a = obelisk_sim.ref.subelement %data[[0]] : !dataref -> !obelisk_sim.ref<!word>
      %b = obelisk_sim.ref.subelement %data[[1]] : !dataref -> !obelisk_sim.ref<!word>
      %clock = obelisk_sim.ref.load %clk : !clockref -> !obelisk_sim.logic<1>
      %take = obelisk_sim.logic.is_true %clock : !obelisk_sim.logic<1>
      cf.cond_br %take, ^first, ^second
    ^first:
      %x = obelisk_sim.logic.constant 17 : i34, 0 : i34 : !word
      %y = obelisk_sim.logic.constant 34 : i34, 0 : i34 : !word
      obelisk_sim.nba.enqueue %x to %a : (!word, !obelisk_sim.ref<!word>) -> ()
      obelisk_sim.nba.enqueue %y to %b : (!word, !obelisk_sim.ref<!word>) -> ()
      cf.br ^wait // first arm exits
    ^second:
      %z = obelisk_sim.logic.constant 85 : i34, 15 : i34 : !word
      %zero = obelisk_sim.logic.constant 0 : i34, 0 : i34 : !word
      obelisk_sim.nba.enqueue %z to %a : (!word, !obelisk_sim.ref<!word>) -> ()
      obelisk_sim.nba.enqueue %zero to %b : (!word, !obelisk_sim.ref<!word>) -> ()
      cf.br ^wait // second arm exits
    }
    obelisk_sim.func @check(%ctx: !obelisk_sim.context {obelisk_sim.capture_kind = 0 : i32}, %data: !dataref {obelisk_sim.capture_kind = 3 : i32, obelisk_sim.descriptor_id = 1 : i64}) attributes {entry_kind = 1 : i32, code_unit_id = 4 : i64} {
      %delay = obelisk_sim.time.constant 3
      obelisk_sim.suspend.delay %delay to ^first
    ^first:
      %a = obelisk_sim.ref.subelement %data[[0]] : !dataref -> !obelisk_sim.ref<!word>
      %b = obelisk_sim.ref.subelement %data[[1]] : !dataref -> !obelisk_sim.ref<!word>
      %x = obelisk_sim.ref.load %a : !obelisk_sim.ref<!word> -> !word
      %y = obelisk_sim.ref.load %b : !obelisk_sim.ref<!word> -> !word
      %format = obelisk_sim.bytes.constant "%09h %09h"
      %stdout = arith.constant 1 : i32
      obelisk_sim.display %ctx to %stdout(%format, %x, %y) newline = true radix = 10 flags = [0, 0, 0] : !obelisk_sim.bytes, !word, !word
      %two = obelisk_sim.time.constant 2
      obelisk_sim.suspend.delay %two to ^second
    ^second:
      %z = obelisk_sim.ref.load %a : !obelisk_sim.ref<!word> -> !word
      %w = obelisk_sim.ref.load %b : !obelisk_sim.ref<!word> -> !word
      obelisk_sim.display %ctx to %stdout(%format, %z, %w) newline = true radix = 10 flags = [0, 0, 0] : !obelisk_sim.bytes, !word, !word
      %status = arith.constant 0 : i32
      obelisk_sim.finish %ctx, %status
      obelisk_sim.return
    }
  }
}
