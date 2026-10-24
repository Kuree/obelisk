// RUN: obelisk-opt %s --pass-pipeline='builtin.module(test-obelisk-replicate,simulation.design(simulation.func(obelisk-sim-thread-process-cfg),obelisk-sim-build-compute-graph,obelisk-sim-verify-compute-graph,obelisk-sim-materialize-graph-regions,obelisk-sim-materialize-compute-fusion,obelisk-sim-specialize-static-state-nba,obelisk-sim-plan-static-superstep),encode-obelisk-sim-to-bytecode{vpi=off},convert-obelisk-sim-processes-to-llvm-coroutines)' 2>/dev/null | FileCheck %s

// test-obelisk-replicate expands counter# into 513 clocked counters, each a
// direct eval owner outside any ranked group. Beyond one 512-owner segment,
// the coordinator selects a segment function by `bit >> 9`; the segment runs
// the owner and returns OK to resume dispatch, or the status the coordinator
// returns. The four-state fallback flag is passed by address.

// CHECK-LABEL: llvm.func @__obelisk_eval_dispatch_v1(
// CHECK: %[[FALLBACK:.*]] = llvm.alloca %{{.*}} x i1
// CHECK: ^[[DISPATCH:bb[0-9]+]]:
// CHECK: llvm.switch %[[BIT:.*]] : i64, ^[[SEGMENTS:bb[0-9]+]] [
// CHECK-NEXT: ]
// CHECK: ^[[SEGMENTS]]:
// CHECK-NEXT: %[[SHIFT:.*]] = llvm.mlir.constant(9 : i64)
// CHECK-NEXT: %[[INDEX:.*]] = llvm.lshr %[[BIT]], %[[SHIFT]]
// CHECK-NEXT: llvm.switch %[[INDEX]] : i64, ^[[STOPPED:bb[0-9]+]] [
// CHECK-NEXT: 0: ^[[FIRST:bb[0-9]+]],
// CHECK-NEXT: 1: ^[[SECOND:bb[0-9]+]]
// CHECK-NEXT: ]
// CHECK: ^[[FIRST]]:
// CHECK-NEXT: %[[STATUS:.*]] = llvm.call @__obelisk_eval_dispatch_v1.segment_0(%arg1, %[[BIT]], %[[FALLBACK]])
// CHECK-NEXT: %[[OK:.*]] = llvm.mlir.constant(0 : i32)
// CHECK-NEXT: %[[RESUME:.*]] = llvm.icmp "eq" %[[STATUS]], %[[OK]]
// CHECK-NEXT: llvm.cond_br %[[RESUME]], ^[[DISPATCH]], ^[[FAILED:bb[0-9]+]](%[[STATUS]] : i32)
// CHECK: ^[[SECOND]]:
// CHECK-NEXT: llvm.call @__obelisk_eval_dispatch_v1.segment_1(%arg1, %[[BIT]], %[[FALLBACK]])

// CHECK-LABEL: llvm.func @__obelisk_eval_dispatch_v1.segment_0(%arg0: !llvm.ptr, %arg1: i64, %arg2: !llvm.ptr) -> i32
// CHECK: llvm.switch %arg1 : i64, ^[[NONE:bb[0-9]+]] [
// CHECK-NEXT: 0: ^
// CHECK: 511: ^
// CHECK-NEXT: ]

// CHECK-LABEL: llvm.func @__obelisk_eval_dispatch_v1.segment_1(%arg0: !llvm.ptr, %arg1: i64, %arg2: !llvm.ptr) -> i32
// CHECK-NEXT: llvm.switch %arg1 : i64, ^[[NONE:bb[0-9]+]] [
// CHECK-NEXT: 512: ^[[OWNER:bb[0-9]+]]
// CHECK-NEXT: ]
// CHECK-NEXT: ^[[CONTINUE:bb[0-9]+]]:
// CHECK-NEXT: %[[OK:.*]] = llvm.mlir.constant(0 : i32)
// CHECK-NEXT: llvm.return %[[OK]]
// CHECK-NEXT: ^[[FAIL:bb[0-9]+]](%[[CODE:.*]]: i32):
// CHECK-NEXT: llvm.return %[[CODE]]
// CHECK-NEXT: ^[[NONE]]:
// CHECK-NEXT: %[[UNAVAILABLE:.*]] = llvm.mlir.constant(13 : i32)
// CHECK-NEXT: llvm.return %[[UNAVAILABLE]]
// CHECK: llvm.store %{{.*}}, %arg2 {{.*}} : i1, !llvm.ptr
// CHECK-NEXT: llvm.call @__obelisk_direct_fragment_{{.*}}.__obelisk_execute(%arg0)
// The proven two-state call's trivial trusted wrapper is inlined, so the
// segment calls the owner's path dispatcher directly.
// CHECK: llvm.call @__obelisk_eval_path_dispatch_v1_{{[0-9]+}}(%arg0) {schedule.eval.direct_call}
// CHECK: llvm.cond_br %{{.*}}, ^{{bb[0-9]+}}, ^[[FAIL]](%{{.*}} : i32)
// CHECK: llvm.cond_br %{{.*}}, ^[[NONE]], ^[[CONTINUE]]

!clk = !simulation.ref<i1>
!int = !simulation.ref<i32>
module attributes {llvm.data_layout = "e-m:e-p:64:64-i64:64-n8:16:32:64-S128", llvm.target_triple = "x86_64-unknown-linux-gnu", schedule.native_scheduler = 3 : i32, native.closed_executable} {
  simulation.design @segments {
    simulation.scope.decl 0 hierarchy "t"
    simulation.code_unit.decl 1 in 0 root_initializer hierarchy "t.root"
    simulation.code_unit.decl 2 in 0 always hierarchy "t.clock"
    simulation.code_unit.decl 3 in 0 always hierarchy "t.counter#" {test.replicate = 513 : i64, test.replicate_stride = {id = 1 : i64}}
    simulation.storage.decl 0 in 0 : i1 design
    simulation.storage.decl 1 in 0 : i32 design {test.replicate = 513 : i64, test.replicate_stride = {id = 1 : i64}}
    simulation.func @root(%ctx: !simulation.context {simulation.capture_kind = 0 : i32}) attributes {entry_kind = 0 : i32, code_unit_id = 1 : i64} {
      %clk = simulation.context.storage %ctx[0] : !clk
      %false = arith.constant false
      simulation.ref.store %false to %clk : i1, !clk
      %c = simulation.spawn @clock(%ctx, %clk) : !simulation.context, !clk -> !simulation.process
      %n = simulation.context.storage %ctx[1] {test.replicate = 513 : i64, test.replicate_stride = {id = 1 : i64}} : !int
      %k = simulation.spawn @"counter#"(%ctx, %clk, %n) {test.replicate = 513 : i64} : !simulation.context, !clk, !int -> !simulation.process
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
    simulation.func @"counter#"(%ctx: !simulation.context {simulation.capture_kind = 0 : i32}, %clk: !clk {simulation.capture_kind = 3 : i32, simulation.descriptor_id = 0 : i64}, %n: !int {simulation.capture_kind = 3 : i32, simulation.descriptor_id = 1 : i64, test.replicate_stride = {simulation.descriptor_id = 1 : i64}}) attributes {entry_kind = 3 : i32, code_unit_id = 3 : i64, test.replicate = 513 : i64, test.replicate_stride = {code_unit_id = 1 : i64}} {
      cf.br ^wait
    ^wait:
      simulation.suspend.edge posedge %clk to ^body {schedule.procedural_event_wait, schedule.repeating_always_wait, site = #schedule.continuation<id = 2>} : !clk
    ^body:
      %old = simulation.ref.load %n : !int -> i32
      %one = arith.constant 1 : i32
      %new = arith.addi %old, %one : i32
      simulation.ref.store %new to %n : i32, !int
      %three = arith.constant 3 : i32
      %done = arith.cmpi eq, %new, %three : i32
      cf.cond_br %done, ^finish, ^wait
    ^finish:
      %status = arith.constant 1 : i32
      simulation.finish %ctx, %status
      cf.br ^wait
    }
  }
}
