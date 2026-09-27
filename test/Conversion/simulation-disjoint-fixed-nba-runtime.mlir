// RUN: obelisk-opt %s --pass-pipeline='builtin.module(simulation.design(simulation.func(obelisk-sim-thread-process-cfg),obelisk-sim-build-compute-graph,obelisk-sim-verify-compute-graph,obelisk-sim-materialize-graph-regions,obelisk-sim-materialize-compute-fusion,obelisk-sim-specialize-static-state-nba,obelisk-sim-plan-static-superstep),encode-obelisk-sim-to-bytecode{vpi=off},convert-obelisk-sim-processes-to-llvm-coroutines)' -o %t.llvm.mlir
// RUN: FileCheck %s --check-prefix=PLAN < %t.llvm.mlir

// Runtime behavior is checked in ../Runtime/simulation-disjoint-fixed-nba-runtime.test.

// Three fixed packed words share a 96-bit root. Distinct source
// sites must not disable Eval when their exact physical slices are disjoint.
// Each keeps its own pending slot; samples straddle the first clock edge.
// PLAN-COUNT-3: llvm.mlir.global internal @__obelisk_eval_nba_valid_
// PLAN-LABEL: llvm.func @__obelisk_aot_schedule_run_v1(
// PLAN: llvm.call @obelisk_rt_v1_scheduler_prepare_periodic_aot

!word = !simulation.logic<32>
!words = !simulation.packed_array<2 : 0 x !word>
!clockref = !simulation.ref<!simulation.logic<1>>
!dataref = !simulation.ref<!words>

module attributes {llvm.data_layout = "e-m:e-p:64:64-i64:64-n8:16:32:64-S128", llvm.target_triple = "x86_64-unknown-linux-gnu", schedule.native_scheduler = 3 : i32} {
  simulation.design @fixed_words {
    simulation.scope.decl 0 hierarchy "fixed_words"
    simulation.code_unit.decl 1 in 0 root_initializer hierarchy "fixed_words.root"
    simulation.code_unit.decl 2 in 0 always hierarchy "fixed_words.clock"
    simulation.code_unit.decl 3 in 0 always hierarchy "fixed_words.update"
    simulation.code_unit.decl 4 in 0 initial hierarchy "fixed_words.check"
    simulation.storage.decl 0 in 0 : !simulation.logic<1> design hierarchy "fixed_words.clk"
    simulation.storage.decl 1 in 0 : !words design hierarchy "fixed_words.data"

    simulation.func @root(%ctx: !simulation.context {simulation.capture_kind = 0 : i32}) attributes {entry_kind = 0 : i32, code_unit_id = 1 : i64} {
      %clk = simulation.context.storage %ctx[0] : !clockref
      %data = simulation.context.storage %ctx[1] : !dataref
      %zero = simulation.logic.constant 0 : i1, 0 : i1 : !simulation.logic<1>
      simulation.ref.store %zero to %clk : !simulation.logic<1>, !clockref
      %zero96 = simulation.logic.constant 0 : i96, 0 : i96 : !simulation.logic<96>
      %packed = simulation.packed.unflatten %zero96 : (!simulation.logic<96>) -> !words
      simulation.ref.store %packed to %data : !words, !dataref
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
      %c = simulation.ref.subelement %data[[2]] : !dataref -> !simulation.ref<!word>
      %x = simulation.logic.constant 17 : i32, 0 : i32 : !word
      %y = simulation.logic.constant 34 : i32, 0 : i32 : !word
      %z = simulation.logic.constant 51 : i32, 0 : i32 : !word
      simulation.nba.enqueue %x to %a : (!word, !simulation.ref<!word>) -> ()
      simulation.nba.enqueue %y to %b : (!word, !simulation.ref<!word>) -> ()
      simulation.nba.enqueue %z to %c : (!word, !simulation.ref<!word>) -> ()
      cf.br ^wait
    }

    simulation.func @check(%ctx: !simulation.context {simulation.capture_kind = 0 : i32}, %data: !dataref {simulation.capture_kind = 3 : i32, simulation.descriptor_id = 1 : i64}) attributes {entry_kind = 1 : i32, code_unit_id = 4 : i64} {
      %delay = simulation.time.constant 1
      simulation.suspend.delay %delay to ^before
    ^before:
      %first = simulation.ref.load %data : !dataref -> !words
      %firstBits = simulation.packed.flatten %first : (!words) -> !simulation.logic<96>
      %format = simulation.bytes.constant "%024h"
      %stdout = arith.constant 1 : i32
      simulation.display %ctx to %stdout(%format, %firstBits) newline = true radix = <decimal> flags = [0, 0] : !simulation.bytes, !simulation.logic<96>
      %two = simulation.time.constant 2
      simulation.suspend.delay %two to ^after
    ^after:
      %last = simulation.ref.load %data : !dataref -> !words
      %lastBits = simulation.packed.flatten %last : (!words) -> !simulation.logic<96>
      simulation.display %ctx to %stdout(%format, %lastBits) newline = true radix = <decimal> flags = [0, 0] : !simulation.bytes, !simulation.logic<96>
      %status = arith.constant 0 : i32
      simulation.finish %ctx, %status
      simulation.return
    }
  }
}
