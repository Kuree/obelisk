// RUN: obelisk-opt %s --pass-pipeline='builtin.module(simulation.design(simulation.func(obelisk-sim-thread-process-cfg),obelisk-sim-build-compute-graph,obelisk-sim-verify-compute-graph,obelisk-sim-materialize-graph-regions,obelisk-sim-materialize-compute-fusion,obelisk-sim-specialize-static-state-nba,obelisk-sim-plan-static-superstep),encode-obelisk-sim-to-bytecode{vpi=off},convert-obelisk-sim-processes-to-llvm-coroutines)' -o %t.llvm.mlir
// RUN: FileCheck %s --check-prefix=PLAN < %t.llvm.mlir

// Runtime behavior is checked in ../Runtime/simulation-exclusive-fixed-nba-runtime.test.

// Two 34-bit words have alternative writes in exclusive CFG arms.
// Verify non-byte-aligned / cross-word slots and four-state publication. On
// the second edge only the OTHER two slots are valid; stale first-edge values
// must not be replayed over them. The runtime companion checks the resulting values.
// PLAN-COUNT-4: llvm.mlir.global internal @__obelisk_eval_nba_valid_{{[0-9]+}}()
// PLAN-LABEL: llvm.func @__obelisk_aot_schedule_run_v1(
// PLAN: llvm.call @obelisk_rt_v1_scheduler_prepare_periodic_aot

!word = !simulation.logic<34>
!words = !simulation.unpacked_array<0 : 1 x !word>
!clockref = !simulation.ref<!simulation.logic<1>>
!dataref = !simulation.ref<!words>

module attributes {llvm.data_layout = "e-m:e-p:64:64-i64:64-n8:16:32:64-S128", llvm.target_triple = "x86_64-unknown-linux-gnu", schedule.native_scheduler = 3 : i32} {
  simulation.design @exclusive_words {
    simulation.scope.decl 0 hierarchy "exclusive_words"
    simulation.code_unit.decl 1 in 0 root_initializer hierarchy "exclusive_words.root"
    simulation.code_unit.decl 2 in 0 always hierarchy "exclusive_words.clock"
    simulation.code_unit.decl 3 in 0 always hierarchy "exclusive_words.update"
    simulation.code_unit.decl 4 in 0 initial hierarchy "exclusive_words.check"
    simulation.storage.decl 0 in 0 : !simulation.logic<1> design hierarchy "exclusive_words.clk"
    simulation.storage.decl 1 in 0 : !words design hierarchy "exclusive_words.data"
    simulation.func @root(%ctx: !simulation.context {simulation.capture_kind = 0 : i32}) attributes {entry_kind = 0 : i32, code_unit_id = 1 : i64} {
      %clk = simulation.context.storage %ctx[0] : !clockref
      %data = simulation.context.storage %ctx[1] : !dataref
      %zero = simulation.logic.constant 0 : i1, 0 : i1 : !simulation.logic<1>
      simulation.ref.store %zero to %clk : !simulation.logic<1>, !clockref
      %a = simulation.spawn @clock(%ctx, %clk) : !simulation.context, !clockref -> !simulation.process
      %b = simulation.spawn @update(%ctx, %clk, %data) : !simulation.context, !clockref, !dataref -> !simulation.process
      %c = simulation.spawn @check(%ctx, %data) : !simulation.context, !dataref -> !simulation.process
      simulation.return
    }
    simulation.func @clock(%ctx: !simulation.context {simulation.capture_kind = 0 : i32}, %clk: !clockref {simulation.capture_kind = 3 : i32, simulation.descriptor_id = 0 : i64}) attributes {entry_kind = 3 : i32, code_unit_id = 2 : i64} {
      cf.br ^wait
    ^wait:
      %delay = simulation.time.constant 2
      simulation.suspend.delay %delay to ^toggle {site = #schedule.continuation<id = 1>, timing = #schedule.timing_site<id = 0, kind = calendar>}
    ^toggle:
      %old = simulation.ref.load %clk : !clockref -> !simulation.logic<1>
      %new = simulation.logic.unary bit_not %old : (!simulation.logic<1>) -> !simulation.logic<1>
      simulation.ref.store %new to %clk : !simulation.logic<1>, !clockref
      cf.br ^wait
    }
    simulation.func @update(%ctx: !simulation.context {simulation.capture_kind = 0 : i32}, %clk: !clockref {simulation.capture_kind = 3 : i32, simulation.descriptor_id = 0 : i64}, %data: !dataref {simulation.capture_kind = 3 : i32, simulation.descriptor_id = 1 : i64}) attributes {entry_kind = 3 : i32, code_unit_id = 3 : i64} {
      cf.br ^wait
    ^wait:
      simulation.suspend.change %clk to ^update {site = #schedule.continuation<id = 2>} : !clockref
    ^update:
      %a = simulation.ref.subelement %data[[0]] : !dataref -> !simulation.ref<!word>
      %b = simulation.ref.subelement %data[[1]] : !dataref -> !simulation.ref<!word>
      %clock = simulation.ref.load %clk : !clockref -> !simulation.logic<1>
      %take = simulation.logic.is_true %clock : !simulation.logic<1>
      cf.cond_br %take, ^first, ^second
    ^first:
      %x = simulation.logic.constant 17 : i34, 0 : i34 : !word
      %y = simulation.logic.constant 34 : i34, 0 : i34 : !word
      simulation.nba.enqueue %x to %a : (!word, !simulation.ref<!word>) -> ()
      simulation.nba.enqueue %y to %b : (!word, !simulation.ref<!word>) -> ()
      cf.br ^wait // first arm exits
    ^second:
      %z = simulation.logic.constant 85 : i34, 15 : i34 : !word
      %zero = simulation.logic.constant 0 : i34, 0 : i34 : !word
      simulation.nba.enqueue %z to %a : (!word, !simulation.ref<!word>) -> ()
      simulation.nba.enqueue %zero to %b : (!word, !simulation.ref<!word>) -> ()
      cf.br ^wait // second arm exits
    }
    simulation.func @check(%ctx: !simulation.context {simulation.capture_kind = 0 : i32}, %data: !dataref {simulation.capture_kind = 3 : i32, simulation.descriptor_id = 1 : i64}) attributes {entry_kind = 1 : i32, code_unit_id = 4 : i64} {
      %delay = simulation.time.constant 3
      simulation.suspend.delay %delay to ^first
    ^first:
      %a = simulation.ref.subelement %data[[0]] : !dataref -> !simulation.ref<!word>
      %b = simulation.ref.subelement %data[[1]] : !dataref -> !simulation.ref<!word>
      %x = simulation.ref.load %a : !simulation.ref<!word> -> !word
      %y = simulation.ref.load %b : !simulation.ref<!word> -> !word
      %format = simulation.bytes.constant "%09h %09h"
      %stdout = arith.constant 1 : i32
      simulation.display %ctx to %stdout(%format, %x, %y) newline = true radix = <decimal> flags = [0, 0, 0] : !simulation.bytes, !word, !word
      %two = simulation.time.constant 2
      simulation.suspend.delay %two to ^second
    ^second:
      %z = simulation.ref.load %a : !simulation.ref<!word> -> !word
      %w = simulation.ref.load %b : !simulation.ref<!word> -> !word
      simulation.display %ctx to %stdout(%format, %z, %w) newline = true radix = <decimal> flags = [0, 0, 0] : !simulation.bytes, !word, !word
      %status = arith.constant 0 : i32
      simulation.finish %ctx, %status
      simulation.return
    }
  }
}
