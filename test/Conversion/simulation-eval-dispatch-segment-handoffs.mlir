// RUN: obelisk-opt %s --pass-pipeline='builtin.module(test-obelisk-edit-attributes{has=test.priority erase},test-obelisk-replicate,simulation.design(simulation.func(obelisk-sim-thread-process-cfg),obelisk-sim-build-compute-graph,obelisk-sim-verify-compute-graph,obelisk-sim-materialize-graph-regions,obelisk-sim-materialize-compute-fusion,obelisk-sim-specialize-static-state-nba,obelisk-sim-plan-static-superstep),encode-obelisk-sim-to-bytecode{vpi=off},convert-obelisk-sim-processes-to-llvm-coroutines)' -o %t.sparse.mlir 2> %t.sparse.trace
// RUN: FileCheck %s --check-prefix=SPARSE < %t.sparse.mlir
// RUN: FileCheck %s --check-prefix=SPARSE-PLAN < %t.sparse.trace
// RUN: obelisk-opt %s --pass-pipeline='builtin.module(test-obelisk-edit-attributes{has=test.priority erase},test-obelisk-edit-attributes{has=test.deferred set="{resume_region = 3 : i32}"},test-obelisk-replicate,simulation.design(simulation.func(obelisk-sim-thread-process-cfg),obelisk-sim-build-compute-graph,obelisk-sim-verify-compute-graph,obelisk-sim-materialize-graph-regions,obelisk-sim-materialize-compute-fusion,obelisk-sim-specialize-static-state-nba,obelisk-sim-plan-static-superstep),encode-obelisk-sim-to-bytecode{vpi=off},convert-obelisk-sim-processes-to-llvm-coroutines)' -o %t.deferred.mlir 2> %t.deferred.trace
// RUN: FileCheck %s --check-prefix=DEFERRED < %t.deferred.mlir
// RUN: FileCheck %s --check-prefix=DEFERRED-PLAN < %t.deferred.trace
// RUN: obelisk-opt %s --pass-pipeline='builtin.module(test-obelisk-replicate,simulation.design(simulation.func(obelisk-sim-thread-process-cfg),obelisk-sim-build-compute-graph,obelisk-sim-verify-compute-graph,obelisk-sim-materialize-graph-regions,obelisk-sim-materialize-compute-fusion,obelisk-sim-specialize-static-state-nba,obelisk-sim-plan-static-superstep),encode-obelisk-sim-to-bytecode{vpi=off},convert-obelisk-sim-processes-to-llvm-coroutines)' -o %t.priority.mlir 2> %t.priority.trace
// RUN: FileCheck %s --check-prefix=PRIORITY < %t.priority.mlir

// The counter owners cannot fuse because they may terminate. The pure `aaa`
// owner instead joins a ranked group; making its wait deferred moves it after
// all Active owners. A priority cancellation waiter disables local dispatch.
// These variants check the generated CFG, including the actual planner choices,
// rather than inferring coverage from the existence of a segment symbol.
// IEEE 1800-2023 4.4.2.2-4.4.2.4, 4.6 and 4.7: retain region boundaries and
// statement/NBA ordering; the particular order between Active owners is chosen
// by the implementation.

// SPARSE-PLAN: obelisk eval owner: bit=0 {{.*}} group=__obelisk_eval_ranked_group_0
// SPARSE-PLAN: obelisk eval owner: bit=513 {{.*}} group=-

// The outer coordinator owns bit zero. A local encounter with that bit must
// return OK without consuming it, while an invalid initial selection fails.
// SPARSE-LABEL: llvm.func @__obelisk_eval_dispatch_v1(
// SPARSE: llvm.call @__obelisk_eval_ranked_group_0(
// SPARSE-LABEL: llvm.func @__obelisk_eval_dispatch_v1.segment_0(
// SPARSE: llvm.switch %arg1 : i64, ^[[INVALID:bb[0-9]+]] [
// SPARSE-NEXT: 1: ^[[FIRST:bb[0-9]+]],
// SPARSE: 511:
// SPARSE-NEXT: ]
// SPARSE: ^[[LOCAL:bb[0-9]+]](%[[BIT:.*]]: i64):
// SPARSE: llvm.switch %[[BIT]] : i64, ^[[DONE:bb[0-9]+]] [
// SPARSE-NEXT: 1: ^[[FIRST]],
// SPARSE: 511:
// SPARSE-NEXT: ]
// SPARSE: ^[[INVALID]]:
// SPARSE: %[[UNAVAILABLE:.*]] = llvm.mlir.constant(13 : i32)
// SPARSE: llvm.br ^[[RETURN:bb[0-9]+]](%[[UNAVAILABLE]] : i32)
// SPARSE: "llvm.intr.cttz"
// SPARSE: llvm.br ^[[LOCAL]](
// SPARSE: ^[[DONE]]:
// SPARSE-NEXT: %[[OK:.*]] = llvm.mlir.constant(0 : i32)
// SPARSE-NEXT: llvm.br ^[[RETURN]](%[[OK]] : i32)
// SPARSE: ^[[RETURN]](%[[STATUS:.*]]: i32):
// SPARSE-NEXT: llvm.return %[[STATUS]]
// SPARSE-LABEL: llvm.func @__obelisk_eval_dispatch_v1.segment_1(

// DEFERRED-PLAN: obelisk eval owner: bit=512 {{.*}} group=- {{.*}}{{$}}
// DEFERRED-PLAN-NEXT: obelisk eval owner: bit=513 {{.*}} deferred{{$}}

// The wholly Active segment can loop. The mixed segment must return after
// each owner so the outer coordinator checks the Inactive boundary again.
// DEFERRED-LABEL: llvm.func @__obelisk_eval_dispatch_v1(
// DEFERRED: %[[FIRST_DEFERRED:.*]] = llvm.mlir.constant(513 : i64)
// DEFERRED: llvm.icmp "uge" %{{.*}}, %[[FIRST_DEFERRED]]
// DEFERRED: llvm.call @obelisk_rt_v1_scheduler_termination_requested(
// DEFERRED-LABEL: llvm.func @__obelisk_eval_dispatch_v1.segment_0(
// DEFERRED: "llvm.intr.cttz"
// DEFERRED-LABEL: llvm.func @__obelisk_eval_dispatch_v1.segment_1(
// DEFERRED: llvm.switch %arg1 : i64,
// DEFERRED-NEXT: 512:
// DEFERRED-NEXT: 513:
// DEFERRED-NEXT: ]
// DEFERRED-NEXT: ^[[RESUME:bb[0-9]+]]:
// DEFERRED-NEXT: %[[OK:.*]] = llvm.mlir.constant(0 : i32)
// DEFERRED-NEXT: llvm.br ^[[RETURN:bb[0-9]+]](%[[OK]] : i32)
// DEFERRED-NOT: llvm.switch
// DEFERRED-NOT: llvm.intr.cttz
// DEFERRED: llvm.br ^[[RESUME]]
// DEFERRED-NOT: llvm.switch
// DEFERRED-NOT: llvm.intr.cttz
// DEFERRED: ^[[RETURN]](%[[STATUS:.*]]: i32):
// DEFERRED-NEXT: llvm.return %[[STATUS]]
// DEFERRED-NEXT: }

// Priority work is polled between owners, including owners in the same segment.
// PRIORITY-LABEL: llvm.func internal @__obelisk_eval_dispatch_v1.__obelisk_pending(
// PRIORITY: llvm.call @obelisk_rt_v1_scheduler_priority_signal_pending(
// PRIORITY-LABEL: llvm.func @__obelisk_eval_dispatch_v1.segment_0(
// PRIORITY: llvm.switch %arg1 : i64,
// PRIORITY-NOT: llvm.switch
// PRIORITY-NOT: llvm.intr.cttz
// PRIORITY-LABEL: llvm.func @__obelisk_eval_dispatch_v1.segment_1(
// PRIORITY: llvm.switch %arg1 : i64,
// PRIORITY-NOT: llvm.switch
// PRIORITY-NOT: llvm.intr.cttz
// PRIORITY: llvm.return
// PRIORITY-NEXT: }

!clk = !simulation.ref<i1>
!int = !simulation.ref<i32>
module attributes {llvm.data_layout = "e-m:e-p:64:64-i64:64-n8:16:32:64-S128", llvm.target_triple = "x86_64-unknown-linux-gnu", schedule.native_scheduler = 3 : i32, native.closed_executable, native.debug_timing} {
  simulation.design @segments {
    simulation.scope.decl 0 hierarchy "t"
    simulation.code_unit.decl 1 in 0 root_initializer hierarchy "t.root"
    simulation.code_unit.decl 2 in 0 always hierarchy "t.clock"
    simulation.code_unit.decl 4 in 0 always hierarchy "t.counter#" {test.replicate = 513 : i64, test.replicate_stride = {id = 1 : i64}}
    simulation.code_unit.decl 3 in 0 always hierarchy "t.aaa"
    simulation.storage.decl 1 in 0 : i32 design
    simulation.code_unit.decl 517 in 0 fork hierarchy "t.cancel" {test.priority}
    simulation.storage.decl 0 in 0 : i1 design
    simulation.storage.decl 2 in 0 : i32 design {test.replicate = 513 : i64, test.replicate_stride = {id = 1 : i64}}
    simulation.func @root(%ctx: !simulation.context {simulation.capture_kind = 0 : i32}) attributes {entry_kind = 0 : i32, code_unit_id = 1 : i64} {
      %clk = simulation.context.storage %ctx[0] : !clk
      %false = arith.constant false
      simulation.ref.store %false to %clk : i1, !clk
      %c = simulation.spawn @clock(%ctx, %clk) : !simulation.context, !clk -> !simulation.process
      %n = simulation.context.storage %ctx[2] {test.replicate = 513 : i64, test.replicate_stride = {id = 1 : i64}} : !int
      %k = simulation.spawn @"counter#"(%ctx, %clk, %n) {test.replicate = 513 : i64} : !simulation.context, !clk, !int -> !simulation.process
      %q = simulation.context.storage %ctx[1] : !int
      %a = simulation.spawn @"aaa"(%ctx, %clk, %q) : !simulation.context, !clk, !int -> !simulation.process
      %p = simulation.spawn @cancel(%ctx, %clk) {test.priority} : !simulation.context, !clk -> !simulation.process
      simulation.return
    }
    simulation.func @clock(%ctx: !simulation.context {simulation.capture_kind = 0 : i32}, %clk: !clk {simulation.capture_kind = 3 : i32, simulation.descriptor_id = 0 : i64}) attributes {entry_kind = 3 : i32, code_unit_id = 2 : i64} {
      cf.br ^wait
    ^wait:
      %delay = simulation.time.constant 5
      simulation.suspend.delay %delay to ^toggle {site = #schedule.continuation<id = 1>, timing = #schedule.timing_site<id = 0, kind = calendar>}
    ^toggle:
      %old = simulation.ref.load %clk : !clk -> i1
      %true = arith.constant true
      %new = arith.xori %old, %true : i1
      simulation.ref.store %new to %clk : i1, !clk
      cf.br ^wait
    }
    simulation.func @"counter#"(%ctx: !simulation.context {simulation.capture_kind = 0 : i32}, %clk: !clk {simulation.capture_kind = 3 : i32, simulation.descriptor_id = 0 : i64}, %n: !int {simulation.capture_kind = 3 : i32, simulation.descriptor_id = 2 : i64, test.replicate_stride = {simulation.descriptor_id = 1 : i64}}) attributes {entry_kind = 3 : i32, code_unit_id = 4 : i64, test.replicate = 513 : i64, test.replicate_stride = {code_unit_id = 1 : i64}} {
      cf.br ^wait
    ^wait:
      simulation.suspend.edge posedge %clk to ^body {schedule.procedural_event_wait, schedule.repeating_always_wait, site = #schedule.continuation<id = 2>} : !clk
    ^body:
      %old = simulation.ref.load %n : !int -> i32
      %one = arith.constant 1 : i32
      %new = arith.addi %old, %one : i32
      simulation.ref.store %new to %n : i32, !int
      %limit = arith.constant 100 : i32
      %done = arith.cmpi eq, %new, %limit : i32
      cf.cond_br %done, ^finish, ^wait
    ^finish:
      %status = arith.constant 1 : i32
      simulation.finish %ctx, %status
      cf.br ^wait
    }
    simulation.func @"aaa"(%ctx: !simulation.context {simulation.capture_kind = 0 : i32}, %clk: !clk {simulation.capture_kind = 3 : i32, simulation.descriptor_id = 0 : i64}, %q: !int {simulation.capture_kind = 3 : i32, simulation.descriptor_id = 1 : i64}) attributes {entry_kind = 3 : i32, code_unit_id = 3 : i64} {
      cf.br ^wait
    ^wait:
      simulation.suspend.edge posedge %clk to ^body {schedule.procedural_event_wait, schedule.repeating_always_wait, site = #schedule.continuation<id = 3>, test.deferred} : !clk
    ^body:
      %old = simulation.ref.load %q : !int -> i32
      %one = arith.constant 1 : i32
      %new = arith.addi %old, %one : i32
      simulation.nba.enqueue %new to %q : (i32, !int) -> ()
      cf.br ^wait
    }
    simulation.func private @cancel(%ctx: !simulation.context {simulation.capture_kind = 0 : i32}, %clk: !clk {simulation.capture_kind = 3 : i32, simulation.descriptor_id = 0 : i64}) attributes {entry_kind = 13 : i32, code_unit_id = 517 : i64, home_region = 10 : i32, internal, schedule.concurrent_cancel, schedule.detached_controls, schedule.priority_signal_resume, test.priority} {
      simulation.suspend.edge posedge %clk to ^resume {site = #schedule.continuation<id = 3>} : !clk
    ^resume:
      simulation.return
    }
  }
}
