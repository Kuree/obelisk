// RUN: obelisk-opt %s --pass-pipeline='builtin.module(simulation.design(simulation.func(obelisk-sim-thread-process-cfg),obelisk-sim-build-compute-graph,obelisk-sim-verify-compute-graph,obelisk-sim-materialize-graph-regions,obelisk-sim-materialize-compute-fusion,obelisk-sim-specialize-static-state-nba,obelisk-sim-plan-static-superstep),encode-obelisk-sim-to-bytecode{vpi=off},convert-obelisk-sim-processes-to-llvm-coroutines)' \
// RUN:   | FileCheck %s

// Every clock kernel publishes into the one model-wide eval ingress. The
// followers below add a kernel per watched register, but a clock-driven owner
// still has exactly one ready bit, so the scheduler clears it once after the
// owner runs rather than once per kernel. Replicating the clear per kernel
// made the generated scheduler grow with owners x kernels, i.e.
// quadratically in the number of module instances.

// CHECK: llvm.mlir.global internal @__obelisk_aot_model_ingress_v1()
// CHECK-COUNT-4: llvm.mlir.global internal @__obelisk_aot_model_active_v1_{{[0-3]}}()
// CHECK-NOT: llvm.mlir.global internal @__obelisk_aot_model_active_v1_
// CHECK-LABEL: llvm.func @__obelisk_aot_schedule_run_v1(
// CHECK: llvm.call @__obelisk_direct_fragment_{{[0-9]+_[0-9]+}}.__obelisk_execute.two_state.__obelisk_trusted(
// CHECK-NEXT: llvm.mlir.addressof @__obelisk_aot_model_ingress_v1
// CHECK-NOT: llvm.mlir.addressof
// CHECK: llvm.br
// CHECK: llvm.call @__obelisk_direct_fragment_{{[0-9]+_[0-9]+}}.__obelisk_execute.two_state.__obelisk_trusted(
// CHECK-NEXT: llvm.mlir.addressof @__obelisk_aot_model_ingress_v1
// CHECK-NOT: llvm.mlir.addressof
// CHECK: llvm.br
// CHECK: llvm.call @__obelisk_direct_fragment_{{[0-9]+_[0-9]+}}.__obelisk_execute.two_state.__obelisk_trusted(
// CHECK-NEXT: llvm.mlir.addressof @__obelisk_aot_model_ingress_v1
// CHECK-NOT: llvm.mlir.addressof
// CHECK: llvm.br

!bit = !simulation.logic<1>
!bitref = !simulation.ref<!bit>
module attributes {llvm.data_layout = "e-m:e-p:64:64-i64:64-n8:16:32:64-S128", llvm.target_triple = "x86_64-unknown-linux-gnu", schedule.native_scheduler = 3 : i32} {
  simulation.design @kernels {
    simulation.scope.decl 0 hierarchy "kernels"
    simulation.storage.decl 0 in 0 : !bit design
    simulation.storage.decl 1 in 0 : !bit design
    simulation.storage.decl 2 in 0 : !bit design
    simulation.storage.decl 3 in 0 : !bit design
    simulation.storage.decl 4 in 0 : !bit design
    simulation.storage.decl 5 in 0 : !bit design
    simulation.storage.decl 6 in 0 : !bit design
    simulation.code_unit.decl 1 in 0 root_initializer hierarchy "kernels.root"
    simulation.code_unit.decl 2 in 0 always hierarchy "kernels.clock"
    simulation.code_unit.decl 3 in 0 initial hierarchy "kernels.check"
    simulation.code_unit.decl 4 in 0 always hierarchy "kernels.writer0"
    simulation.code_unit.decl 5 in 0 always hierarchy "kernels.writer1"
    simulation.code_unit.decl 6 in 0 always hierarchy "kernels.writer2"
    simulation.code_unit.decl 7 in 0 always hierarchy "kernels.follower0"
    simulation.code_unit.decl 8 in 0 always hierarchy "kernels.follower1"
    simulation.code_unit.decl 9 in 0 always hierarchy "kernels.follower2"

    simulation.func @root(%ctx: !simulation.context {simulation.capture_kind = 0 : i32}) attributes {entry_kind = 0 : i32, code_unit_id = 1 : i64} {
      %clk = simulation.context.storage %ctx[0] : !bitref
      %q0 = simulation.context.storage %ctx[1] : !bitref
      %q1 = simulation.context.storage %ctx[2] : !bitref
      %q2 = simulation.context.storage %ctx[3] : !bitref
      %f0 = simulation.context.storage %ctx[4] : !bitref
      %f1 = simulation.context.storage %ctx[5] : !bitref
      %f2 = simulation.context.storage %ctx[6] : !bitref
      %zero = simulation.logic.constant false, false : !bit
      simulation.ref.store %zero to %clk : !bit, !bitref
      simulation.ref.store %zero to %q0 : !bit, !bitref
      simulation.ref.store %zero to %q1 : !bit, !bitref
      simulation.ref.store %zero to %q2 : !bit, !bitref
      %clock = simulation.spawn @clock(%ctx, %clk) : !simulation.context, !bitref -> !simulation.process
      %writer0 = simulation.spawn @writer0(%ctx, %clk, %q0) : !simulation.context, !bitref, !bitref -> !simulation.process
      %writer1 = simulation.spawn @writer1(%ctx, %clk, %q1) : !simulation.context, !bitref, !bitref -> !simulation.process
      %writer2 = simulation.spawn @writer2(%ctx, %clk, %q2) : !simulation.context, !bitref, !bitref -> !simulation.process
      %follower0 = simulation.spawn @follower0(%ctx, %q0, %f0) : !simulation.context, !bitref, !bitref -> !simulation.process
      %follower1 = simulation.spawn @follower1(%ctx, %q1, %f1) : !simulation.context, !bitref, !bitref -> !simulation.process
      %follower2 = simulation.spawn @follower2(%ctx, %q2, %f2) : !simulation.context, !bitref, !bitref -> !simulation.process
      %check = simulation.spawn @check(%ctx) : !simulation.context -> !simulation.process
      simulation.return
    }

    simulation.func @clock(%ctx: !simulation.context {simulation.capture_kind = 0 : i32}, %clk: !bitref {simulation.capture_kind = 3 : i32, simulation.descriptor_id = 0 : i64}) attributes {entry_kind = 3 : i32, code_unit_id = 2 : i64} {
      cf.br ^wait
    ^wait:
      %delay = simulation.time.constant 2
      simulation.suspend.delay %delay to ^toggle {site = #schedule.continuation<id = 1>, timing = #schedule.timing_site<id = 0, kind = calendar>}
    ^toggle:
      %old = simulation.ref.load %clk : !bitref -> !bit
      %new = simulation.logic.unary bit_not %old : (!bit) -> !bit
      simulation.ref.store %new to %clk : !bit, !bitref
      cf.br ^wait
    }

    simulation.func @writer0(%ctx: !simulation.context {simulation.capture_kind = 0 : i32}, %clk: !bitref {simulation.capture_kind = 3 : i32, simulation.descriptor_id = 0 : i64}, %q: !bitref {simulation.capture_kind = 3 : i32, simulation.descriptor_id = 1 : i64}) attributes {entry_kind = 3 : i32, code_unit_id = 4 : i64} {
      cf.br ^wait
    ^wait:
      simulation.suspend.change %clk to ^update {site = #schedule.continuation<id = 2>} : !bitref
    ^update:
      %value = simulation.ref.load %clk : !bitref -> !bit
      simulation.nba.enqueue %value to %q : (!bit, !bitref) -> ()
      cf.br ^wait
    }
    simulation.func @writer1(%ctx: !simulation.context {simulation.capture_kind = 0 : i32}, %clk: !bitref {simulation.capture_kind = 3 : i32, simulation.descriptor_id = 0 : i64}, %q: !bitref {simulation.capture_kind = 3 : i32, simulation.descriptor_id = 2 : i64}) attributes {entry_kind = 3 : i32, code_unit_id = 5 : i64} {
      cf.br ^wait
    ^wait:
      simulation.suspend.change %clk to ^update {site = #schedule.continuation<id = 3>} : !bitref
    ^update:
      %value = simulation.ref.load %clk : !bitref -> !bit
      simulation.nba.enqueue %value to %q : (!bit, !bitref) -> ()
      cf.br ^wait
    }
    simulation.func @writer2(%ctx: !simulation.context {simulation.capture_kind = 0 : i32}, %clk: !bitref {simulation.capture_kind = 3 : i32, simulation.descriptor_id = 0 : i64}, %q: !bitref {simulation.capture_kind = 3 : i32, simulation.descriptor_id = 3 : i64}) attributes {entry_kind = 3 : i32, code_unit_id = 6 : i64} {
      cf.br ^wait
    ^wait:
      simulation.suspend.change %clk to ^update {site = #schedule.continuation<id = 4>} : !bitref
    ^update:
      %value = simulation.ref.load %clk : !bitref -> !bit
      simulation.nba.enqueue %value to %q : (!bit, !bitref) -> ()
      cf.br ^wait
    }

    simulation.func @follower0(%ctx: !simulation.context {simulation.capture_kind = 0 : i32}, %q: !bitref {simulation.capture_kind = 3 : i32, simulation.descriptor_id = 1 : i64}, %f: !bitref {simulation.capture_kind = 3 : i32, simulation.descriptor_id = 4 : i64}) attributes {entry_kind = 3 : i32, code_unit_id = 7 : i64} {
      cf.br ^wait
    ^wait:
      simulation.suspend.change %q to ^copy {site = #schedule.continuation<id = 5>} : !bitref
    ^copy:
      %value = simulation.ref.load %q : !bitref -> !bit
      simulation.ref.store %value to %f : !bit, !bitref
      cf.br ^wait
    }
    simulation.func @follower1(%ctx: !simulation.context {simulation.capture_kind = 0 : i32}, %q: !bitref {simulation.capture_kind = 3 : i32, simulation.descriptor_id = 2 : i64}, %f: !bitref {simulation.capture_kind = 3 : i32, simulation.descriptor_id = 5 : i64}) attributes {entry_kind = 3 : i32, code_unit_id = 8 : i64} {
      cf.br ^wait
    ^wait:
      simulation.suspend.change %q to ^copy {site = #schedule.continuation<id = 6>} : !bitref
    ^copy:
      %value = simulation.ref.load %q : !bitref -> !bit
      simulation.ref.store %value to %f : !bit, !bitref
      cf.br ^wait
    }
    simulation.func @follower2(%ctx: !simulation.context {simulation.capture_kind = 0 : i32}, %q: !bitref {simulation.capture_kind = 3 : i32, simulation.descriptor_id = 3 : i64}, %f: !bitref {simulation.capture_kind = 3 : i32, simulation.descriptor_id = 6 : i64}) attributes {entry_kind = 3 : i32, code_unit_id = 9 : i64} {
      cf.br ^wait
    ^wait:
      simulation.suspend.change %q to ^copy {site = #schedule.continuation<id = 7>} : !bitref
    ^copy:
      %value = simulation.ref.load %q : !bitref -> !bit
      simulation.ref.store %value to %f : !bit, !bitref
      cf.br ^wait
    }

    simulation.func @check(%ctx: !simulation.context {simulation.capture_kind = 0 : i32}) attributes {entry_kind = 1 : i32, code_unit_id = 3 : i64} {
      %delay = simulation.time.constant 9
      simulation.suspend.delay %delay to ^done {site = #schedule.continuation<id = 8>, timing = #schedule.timing_site<id = 1, kind = calendar>}
    ^done:
      %status = arith.constant 0 : i32
      simulation.finish %ctx, %status
      simulation.return
    }
  }
}
