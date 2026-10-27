// RUN: talos-opt %s --pass-pipeline='builtin.module(simulation.design(simulation.func(talos-sim-thread-process-cfg),talos-sim-build-compute-graph,talos-sim-verify-compute-graph,talos-sim-materialize-graph-regions,talos-sim-materialize-compute-fusion,talos-sim-specialize-static-state-nba,talos-sim-plan-static-superstep),encode-talos-sim-to-bytecode{vpi=off},convert-talos-sim-processes-to-llvm-coroutines)' | FileCheck %s --implicit-check-not='llvm.mlir.global internal @__talos_eval_nba_valid_'

// CHECK: module

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
      cf.cond_br %take, ^first, ^wait
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
