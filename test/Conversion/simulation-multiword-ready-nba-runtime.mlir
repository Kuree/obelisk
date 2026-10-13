// RUN: obelisk-opt %s --pass-pipeline='builtin.module(test-obelisk-replicate,simulation.design(simulation.func(obelisk-sim-thread-process-cfg),obelisk-sim-build-compute-graph,obelisk-sim-verify-compute-graph,obelisk-sim-specialize-static-state-nba,obelisk-sim-plan-static-superstep),encode-obelisk-sim-to-bytecode{vpi=off},convert-obelisk-sim-processes-to-llvm-coroutines)' -o %t.llvm.mlir
// RUN: FileCheck %s --check-prefix=PLAN < %t.llvm.mlir

// test-obelisk-replicate expands the writer marked test.replicate into
// writer0 ... writer63; writer64, the last of the 65 single-bit writers that
// span two ready words, also observes the old value.

// LRM 4.5, 4.6, 4.9.4: all ready words must drain before the NBA barrier.
// A second clock transition exercises reactivation after cached emptiness.
// Keep fragments separate in this pass-specific test: fusion would hide the
// multiword runtime boundary. At least 100 nodes ensures it remains covered.
// PLAN: llvm.mlir.global internal constant @__obelisk_aot_schedule_nodes_v1()
// PLAN-SAME: !llvm.array<{{[1-9][0-9][0-9]+}} x struct<(i32, i32, i32)>>
// PLAN: llvm.call @obelisk_rt_v1_scheduler_run_aot_nodes

!bit = !simulation.logic<1>
!clockref = !simulation.ref<!bit>
!data = !simulation.logic<65>
!dataref = !simulation.ref<!data>
module attributes {llvm.data_layout = "e-m:e-p:64:64-i64:64-n8:16:32:64-S128", llvm.target_triple = "x86_64-unknown-linux-gnu", schedule.native_scheduler = 2 : i32} {
  simulation.design @multiword {
    simulation.scope.decl 0 hierarchy "multiword"
    simulation.storage.decl 0 in 0 : !bit design
    simulation.storage.decl 1 in 0 : !data design
    simulation.code_unit.decl 1 in 0 root_initializer hierarchy "multiword.root"
    simulation.code_unit.decl 2 in 0 always hierarchy "multiword.clock"
    simulation.code_unit.decl 3 in 0 initial hierarchy "multiword.check"
    simulation.code_unit.decl 4 in 0 always hierarchy "multiword.writer#" {test.replicate = 64 : i64, test.replicate_stride = {id = 1 : i64}}
    simulation.code_unit.decl 68 in 0 always hierarchy "multiword.writer64"

    simulation.func @root(%ctx: !simulation.context {simulation.capture_kind = 0 : i32}) attributes {entry_kind = 0 : i32, code_unit_id = 1 : i64} {
      %clk = simulation.context.storage %ctx[0] : !clockref
      %data = simulation.context.storage %ctx[1] : !dataref
      %zero = simulation.logic.constant false, false : !bit
      %zeros = simulation.logic.constant 0 : i65, 0 : i65 : !data
      simulation.ref.store %zero to %clk : !bit, !clockref
      simulation.ref.store %zeros to %data : !data, !dataref
      %clock = simulation.spawn @clock(%ctx, %clk) : !simulation.context, !clockref -> !simulation.process
      %writer = simulation.spawn @"writer#"(%ctx, %clk, %data) {test.replicate = 64 : i64} : !simulation.context, !clockref, !dataref -> !simulation.process
      %writer64 = simulation.spawn @writer64(%ctx, %clk, %data) : !simulation.context, !clockref, !dataref -> !simulation.process
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
    simulation.func @"writer#"(%ctx: !simulation.context {simulation.capture_kind = 0 : i32}, %clk: !clockref {simulation.capture_kind = 3 : i32, simulation.descriptor_id = 0 : i64}, %data: !dataref {simulation.capture_kind = 3 : i32, simulation.descriptor_id = 1 : i64}) attributes {entry_kind = 3 : i32, code_unit_id = 4 : i64, test.replicate = 64 : i64, test.replicate_stride = {code_unit_id = 1 : i64}} {
      cf.br ^wait
    ^wait:
      simulation.suspend.change %clk to ^update : !clockref
    ^update:
      %value = simulation.ref.load %clk : !clockref -> !bit
      %target = simulation.ref.extract %data from 0 {test.replicate_stride = {low_bit = 1 : i64}} : !dataref -> !clockref
      simulation.nba.enqueue %value to %target : (!bit, !clockref) -> ()
      cf.br ^wait
    }
    simulation.func @writer64(%ctx: !simulation.context {simulation.capture_kind = 0 : i32}, %clk: !clockref {simulation.capture_kind = 3 : i32, simulation.descriptor_id = 0 : i64}, %data: !dataref {simulation.capture_kind = 3 : i32, simulation.descriptor_id = 1 : i64}) attributes {entry_kind = 3 : i32, code_unit_id = 68 : i64} {
      cf.br ^wait
    ^wait:
      simulation.suspend.change %clk to ^update : !clockref
    ^update:
      %value = simulation.ref.load %clk : !clockref -> !bit
      %target = simulation.ref.extract %data from 64 : !dataref -> !clockref
      simulation.nba.enqueue %value to %target : (!bit, !clockref) -> ()
      %old = simulation.ref.load %data : !dataref -> !data
      %format = simulation.bytes.constant "active %017h"
      %stdout = arith.constant 1 : i32
      simulation.display %ctx to %stdout(%format, %old) newline = true radix = <decimal> flags = [0, 0] : !simulation.bytes, !data

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
