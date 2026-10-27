// RUN: talos-opt %s --pass-pipeline='builtin.module(test-talos-replicate,simulation.design(simulation.func(talos-sim-thread-process-cfg),talos-sim-build-compute-graph,talos-sim-verify-compute-graph,talos-sim-materialize-graph-regions,talos-sim-materialize-compute-fusion,talos-sim-specialize-static-state-nba,talos-sim-plan-static-superstep),encode-talos-sim-to-bytecode{vpi=off},convert-talos-sim-processes-to-llvm-coroutines)' -o %t.llvm.mlir
// RUN: FileCheck %s --check-prefix=PLAN < %t.llvm.mlir

// test-talos-replicate expands the writer marked test.replicate into the
// 65 single-bit writers (writer0 ... writer64) that span two ready words.

// The copied clock is not certified periodic. Its wide NBA owners use ordered
// generated staging, not one-entry latches or actor checkpoints. All 65 writes
// survive queue growth and subsequent reuse after the first NBA barrier.
// PLAN-NOT: .ordered_nba_checkpoint
// PLAN: llvm.mlir.global internal @__talos_eval_ordered_nba_queue_v1
// PLAN: llvm.func @__talos_eval_dispatch_v1

!bit = !simulation.logic<1>
!clockref = !simulation.ref<!bit>
!data = !simulation.logic<65>
!dataref = !simulation.ref<!data>
module attributes {llvm.data_layout = "e-m:e-p:64:64-i64:64-n8:16:32:64-S128", llvm.target_triple = "x86_64-unknown-linux-gnu", schedule.native_scheduler = 3 : i32} {
  simulation.design @multiword {
    simulation.scope.decl 0 hierarchy "multiword"
    simulation.storage.decl 0 in 0 : !bit design
    simulation.storage.decl 1 in 0 : !data design
    simulation.code_unit.decl 1 in 0 root_initializer hierarchy "multiword.root"
    simulation.code_unit.decl 2 in 0 always hierarchy "multiword.clock"
    simulation.code_unit.decl 3 in 0 initial hierarchy "multiword.check"
    simulation.code_unit.decl 4 in 0 always hierarchy "multiword.writer#" {test.replicate = 65 : i64, test.replicate_stride = {id = 1 : i64}}
    simulation.storage.decl 2 in 0 : !bit design
    simulation.code_unit.decl 69 in 0 always hierarchy "multiword.forward"

    simulation.func @root(%ctx: !simulation.context {simulation.capture_kind = 0 : i32}) attributes {entry_kind = 0 : i32, code_unit_id = 1 : i64} {
      %clk = simulation.context.storage %ctx[0] : !clockref
      %data = simulation.context.storage %ctx[1] : !dataref
      %zero = simulation.logic.constant false, false : !bit
      %zeros = simulation.logic.constant 0 : i65, 0 : i65 : !data
      simulation.ref.store %zero to %clk : !bit, !clockref
      simulation.ref.store %zeros to %data : !data, !dataref
      %clock = simulation.spawn @clock(%ctx, %clk) : !simulation.context, !clockref -> !simulation.process
      %forwarded = simulation.context.storage %ctx[2] : !clockref
      %forward = simulation.spawn @forward(%ctx, %clk, %forwarded) : !simulation.context, !clockref, !clockref -> !simulation.process
      %writer = simulation.spawn @"writer#"(%ctx, %forwarded, %data) {test.replicate = 65 : i64} : !simulation.context, !clockref, !dataref -> !simulation.process
      %check = simulation.spawn @check(%ctx, %data) : !simulation.context, !dataref -> !simulation.process
      simulation.return
    }
    simulation.func @clock(%ctx: !simulation.context {simulation.capture_kind = 0 : i32}, %clk: !clockref {simulation.capture_kind = 3 : i32, simulation.descriptor_id = 0 : i64}) attributes {entry_kind = 3 : i32, code_unit_id = 2 : i64} {
      cf.br ^wait
    ^wait:
      %delay = simulation.time.constant 2
      simulation.suspend.delay %delay to ^toggle {site = #schedule.continuation<id = 1>, timing = #schedule.timing_site<id = 0, kind = calendar>}
    ^toggle:
      %old = simulation.ref.load %clk : !clockref -> !bit
      %new = simulation.logic.unary bit_not %old : (!bit) -> !bit
      simulation.ref.store %new to %clk : !bit, !clockref
      cf.br ^wait
    }
    simulation.func @forward(%ctx: !simulation.context {simulation.capture_kind = 0 : i32}, %clk: !clockref {simulation.capture_kind = 3 : i32, simulation.descriptor_id = 0 : i64}, %target: !clockref {simulation.capture_kind = 3 : i32, simulation.descriptor_id = 2 : i64}) attributes {entry_kind = 3 : i32, code_unit_id = 69 : i64} {
      cf.br ^wait
    ^wait:
      simulation.suspend.change %clk to ^copy {site = #schedule.continuation<id = 70>} : !clockref
    ^copy:
      %value = simulation.ref.load %clk : !clockref -> !bit
      simulation.ref.store %value to %target : !bit, !clockref
      cf.br ^wait
    }
    simulation.func @"writer#"(%ctx: !simulation.context {simulation.capture_kind = 0 : i32}, %clk: !clockref {simulation.capture_kind = 3 : i32, simulation.descriptor_id = 2 : i64}, %data: !dataref {simulation.capture_kind = 3 : i32, simulation.descriptor_id = 1 : i64}) attributes {entry_kind = 3 : i32, code_unit_id = 4 : i64, test.replicate = 65 : i64, test.replicate_stride = {code_unit_id = 1 : i64}} {
      cf.br ^wait
    ^wait:
      simulation.suspend.change %clk to ^update : !clockref
    ^update:
      %value = simulation.ref.load %clk : !clockref -> !bit
      %target = simulation.ref.extract %data from 0 {test.replicate_stride = {low_bit = 1 : i64}} : !dataref -> !clockref
      simulation.nba.enqueue %value to %target : (!bit, !clockref) -> ()
      cf.br ^wait
    }
    simulation.func @check(%ctx: !simulation.context {simulation.capture_kind = 0 : i32}, %data: !dataref {simulation.capture_kind = 3 : i32, simulation.descriptor_id = 1 : i64}) attributes {entry_kind = 1 : i32, code_unit_id = 3 : i64} {
      %format = simulation.bytes.constant "%017h"
      %stdout = arith.constant 1 : i32
      %one = simulation.time.constant 1
      %two = simulation.time.constant 2
      simulation.suspend.delay %one to ^before {site = #schedule.continuation<id = 67>, timing = #schedule.timing_site<id = 1, kind = calendar>}
    ^before:
      %before = simulation.ref.load %data : !dataref -> !data
      simulation.display %ctx to %stdout(%format, %before) newline = true radix = <decimal> flags = [0, 0] : !simulation.bytes, !data
      simulation.suspend.delay %two to ^after {site = #schedule.continuation<id = 68>, timing = #schedule.timing_site<id = 2, kind = calendar>}
    ^after:
      %after = simulation.ref.load %data : !dataref -> !data
      simulation.display %ctx to %stdout(%format, %after) newline = true radix = <decimal> flags = [0, 0] : !simulation.bytes, !data
      simulation.suspend.delay %two to ^again {site = #schedule.continuation<id = 69>, timing = #schedule.timing_site<id = 3, kind = calendar>}
    ^again:
      %again = simulation.ref.load %data : !dataref -> !data
      simulation.display %ctx to %stdout(%format, %again) newline = true radix = <decimal> flags = [0, 0] : !simulation.bytes, !data
      %status = arith.constant 0 : i32
      simulation.finish %ctx, %status
      simulation.return
    }
  }
}
