// RUN: obelisk-opt %s --split-input-file --pass-pipeline='builtin.module(simulation.design(simulation.func(obelisk-sim-thread-process-cfg),obelisk-sim-build-compute-graph,obelisk-sim-verify-compute-graph,obelisk-sim-materialize-graph-regions,obelisk-sim-materialize-compute-fusion,obelisk-sim-specialize-static-state-nba,obelisk-sim-plan-static-superstep),encode-obelisk-sim-to-bytecode{vpi=off},convert-obelisk-sim-processes-to-llvm-coroutines)' -o %t.mlir 2>&1 | FileCheck %s --check-prefix=PLAN
// RUN: FileCheck %s --check-prefix=LOWER < %t.mlir

// IEEE 1800-2023 4.4.2.3 and 4.5: a deferred kernel resumes in the Inactive
// region, so the eval coordinator runs it only after the edge's Active work.
// Deferred owners take the highest bits; the lowest pending bit is deferred
// only once no Active owner is pending.
//
// IEEE 1800-2023 9.2.3 and 20.2: after `$finish` no remaining event of the
// slot runs, including a deferred owner. Each coordinator reads the
// termination latch before one. The calendar coordinator cannot use its NBA
// cut for this: the cut reports "no barrier" with the same value.

// The finisher is a generated Active owner. The coordinator polls the latch
// when the selected bit is deferred and stops on a finish.

// PLAN: obelisk eval owner: bit=0 {{.*}}executor={{[^ ]*}}{{$}}
// PLAN-NEXT: obelisk eval owner: bit=1 {{.*}} deferred{{$}}

// LOWER-LABEL: llvm.func @__obelisk_eval_dispatch_v1(
// LOWER-NOT: prepare_eval_nba
// LOWER: %[[BIT:.*]] = llvm.add
// LOWER: %[[FIRST:.*]] = llvm.mlir.constant(1 : i64)
// LOWER-NEXT: %[[DEFERRED:.*]] = llvm.icmp "uge" %[[BIT]], %[[FIRST]]
// LOWER-NEXT: llvm.cond_br %[[DEFERRED]], ^[[POLL:bb[0-9]+]], ^[[SWITCH:bb[0-9]+]](%[[BIT]] : i64)
// LOWER-NEXT: ^[[SWITCH]](%[[OWNER:.*]]: i64):
// LOWER-NEXT: llvm.switch %[[OWNER]] : i64, ^[[STOPPED:bb[0-9]+]] [
// LOWER: ^[[POLL]]:
// LOWER-NEXT: %[[LATCH:.*]] = llvm.call @obelisk_rt_v1_scheduler_termination_requested(%arg1)
// LOWER-NEXT: %[[ZERO:.*]] = llvm.mlir.constant(0 : i32)
// LOWER-NEXT: %[[FINISHED:.*]] = llvm.icmp "ne" %[[LATCH]], %[[ZERO]]
// LOWER-NEXT: llvm.cond_br %[[FINISHED]], ^[[STOPPED]], ^[[SWITCH]](%[[BIT]] : i64)

!clk = !simulation.ref<i1>
!int = !simulation.ref<i32>
module attributes {llvm.data_layout = "e-m:e-p:64:64-i64:64-n8:16:32:64-S128", llvm.target_triple = "x86_64-unknown-linux-gnu", schedule.native_scheduler = 3 : i32, native.closed_executable, debug.native_timing} {
  simulation.design @deferred_finish {
    simulation.scope.decl 0 hierarchy "t"
    simulation.code_unit.decl 1 in 0 root_initializer hierarchy "t.root"
    simulation.code_unit.decl 2 in 0 always hierarchy "t.clock"
    simulation.code_unit.decl 3 in 0 always hierarchy "t.kernel"
    simulation.code_unit.decl 4 in 0 always hierarchy "t.finisher"
    simulation.storage.decl 0 in 0 : i1 design
    simulation.storage.decl 1 in 0 : i32 design
    simulation.storage.decl 2 in 0 : i32 design
    simulation.func @root(%ctx: !simulation.context {simulation.capture_kind = 0 : i32}) attributes {entry_kind = 0 : i32, code_unit_id = 1 : i64} {
      %clk = simulation.context.storage %ctx[0] : !clk
      %q = simulation.context.storage %ctx[1] : !int
      %n = simulation.context.storage %ctx[2] : !int
      %false = arith.constant false
      %zero = arith.constant 0 : i32
      simulation.ref.store %false to %clk : i1, !clk
      simulation.ref.store %zero to %q : i32, !int
      simulation.ref.store %zero to %n : i32, !int
      %c = simulation.spawn @clock(%ctx, %clk) : !simulation.context, !clk -> !simulation.process
      %k = simulation.spawn @kernel(%ctx, %clk, %q) : !simulation.context, !clk, !int -> !simulation.process
      %f = simulation.spawn @finisher(%ctx, %clk, %n) : !simulation.context, !clk, !int -> !simulation.process
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
    // `always @(posedge clk) q <= q + 1` as a deferred clocked kernel.
    simulation.func @kernel(%ctx: !simulation.context {simulation.capture_kind = 0 : i32}, %clk: !clk {simulation.capture_kind = 3 : i32, simulation.descriptor_id = 0 : i64}, %q: !int {simulation.capture_kind = 3 : i32, simulation.descriptor_id = 1 : i64}) attributes {entry_kind = 3 : i32, code_unit_id = 3 : i64} {
      cf.br ^wait
    ^wait:
      simulation.suspend.edge posedge %clk to ^body {resume_region = 3 : i32, schedule.procedural_event_wait, schedule.repeating_always_wait, site = #schedule.continuation<id = 2>} : !clk
    ^body:
      %old = simulation.ref.load %q : !int -> i32
      %one = arith.constant 1 : i32
      %new = arith.addi %old, %one : i32
      simulation.ref.store %new to %q : i32, !int
      cf.br ^wait
    }
    // `always @(posedge clk) if (++n == 3) $finish` stays in Active.
    simulation.func @finisher(%ctx: !simulation.context {simulation.capture_kind = 0 : i32}, %clk: !clk {simulation.capture_kind = 3 : i32, simulation.descriptor_id = 0 : i64}, %n: !int {simulation.capture_kind = 3 : i32, simulation.descriptor_id = 2 : i64}) attributes {entry_kind = 3 : i32, code_unit_id = 4 : i64} {
      cf.br ^wait
    ^wait:
      simulation.suspend.edge posedge %clk to ^body {schedule.procedural_event_wait, schedule.repeating_always_wait, site = #schedule.continuation<id = 3>} : !clk
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

// -----

// The finisher is a runtime-owned `initial` loop, so the coordinator keeps a
// runtime calendar. A deferred bit first takes the NBA cut, which drains
// runtime Active and Inactive work. Once quiescent, the coordinator polls the
// latch and only then runs the lowest pending deferred owner.

// PLAN: obelisk eval owner: bit=0 {{.*}} deferred{{$}}

// LOWER-LABEL: llvm.func @__obelisk_eval_dispatch_v1(
// LOWER: llvm.call @obelisk_rt_v1_scheduler_prepare_eval_nba(
// LOWER: %[[EMPTY:.*]] = llvm.icmp "eq" %{{.*}}, %{{.*}} : i64
// LOWER-NEXT: llvm.cond_br %[[EMPTY]], ^[[CUT:bb[0-9]+]], ^[[SELECT:bb[0-9]+]]
// LOWER-NEXT: ^[[SELECT]]:
// LOWER: %[[DEFERRED:.*]] = llvm.icmp "uge"
// LOWER-NEXT: llvm.cond_br %[[DEFERRED]], ^[[CUT]], ^[[SWITCH:bb[0-9]+]](
// LOWER: llvm.call @obelisk_rt_v1_scheduler_termination_requested(%arg1)
// LOWER: llvm.cond_br %{{.*}}, ^{{bb[0-9]+}}, ^[[SCAN:bb[0-9]+]]
// LOWER-NEXT: ^[[SCAN]]:
// LOWER-NEXT: llvm.mlir.addressof @__obelisk_aot_model_ingress_v1
// LOWER: "llvm.intr.cttz"
// LOWER: llvm.br ^[[SWITCH]](

!clk = !simulation.ref<i1>
!int = !simulation.ref<i32>
module attributes {llvm.data_layout = "e-m:e-p:64:64-i64:64-n8:16:32:64-S128", llvm.target_triple = "x86_64-unknown-linux-gnu", schedule.native_scheduler = 3 : i32, native.closed_executable, debug.native_timing} {
  simulation.design @deferred_finish_runtime {
    simulation.scope.decl 0 hierarchy "t"
    simulation.code_unit.decl 1 in 0 root_initializer hierarchy "t.root"
    simulation.code_unit.decl 2 in 0 always hierarchy "t.clock"
    simulation.code_unit.decl 3 in 0 always hierarchy "t.kernel"
    simulation.code_unit.decl 4 in 0 initial hierarchy "t.finisher"
    simulation.storage.decl 0 in 0 : i1 design
    simulation.storage.decl 1 in 0 : i32 design
    simulation.storage.decl 2 in 0 : i32 design
    simulation.func @root(%ctx: !simulation.context {simulation.capture_kind = 0 : i32}) attributes {entry_kind = 0 : i32, code_unit_id = 1 : i64} {
      %clk = simulation.context.storage %ctx[0] : !clk
      %q = simulation.context.storage %ctx[1] : !int
      %n = simulation.context.storage %ctx[2] : !int
      %false = arith.constant false
      %zero = arith.constant 0 : i32
      simulation.ref.store %false to %clk : i1, !clk
      simulation.ref.store %zero to %q : i32, !int
      simulation.ref.store %zero to %n : i32, !int
      %c = simulation.spawn @clock(%ctx, %clk) : !simulation.context, !clk -> !simulation.process
      %k = simulation.spawn @kernel(%ctx, %clk, %q) : !simulation.context, !clk, !int -> !simulation.process
      %f = simulation.spawn @finisher(%ctx, %clk, %n) : !simulation.context, !clk, !int -> !simulation.process
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
    // `always @(posedge clk) q <= q + 1` as a deferred clocked kernel.
    simulation.func @kernel(%ctx: !simulation.context {simulation.capture_kind = 0 : i32}, %clk: !clk {simulation.capture_kind = 3 : i32, simulation.descriptor_id = 0 : i64}, %q: !int {simulation.capture_kind = 3 : i32, simulation.descriptor_id = 1 : i64}) attributes {entry_kind = 3 : i32, code_unit_id = 3 : i64} {
      cf.br ^wait
    ^wait:
      simulation.suspend.edge posedge %clk to ^body {resume_region = 3 : i32, schedule.procedural_event_wait, schedule.repeating_always_wait, site = #schedule.continuation<id = 2>} : !clk
    ^body:
      %old = simulation.ref.load %q : !int -> i32
      %one = arith.constant 1 : i32
      %new = arith.addi %old, %one : i32
      simulation.ref.store %new to %q : i32, !int
      cf.br ^wait
    }
    // `always @(posedge clk) if (++n == 3) $finish` stays in Active.
    simulation.func @finisher(%ctx: !simulation.context {simulation.capture_kind = 0 : i32}, %clk: !clk {simulation.capture_kind = 3 : i32, simulation.descriptor_id = 0 : i64}, %n: !int {simulation.capture_kind = 3 : i32, simulation.descriptor_id = 2 : i64}) attributes {entry_kind = 1 : i32, code_unit_id = 4 : i64} {
      cf.br ^wait
    ^wait:
      simulation.suspend.edge posedge %clk to ^body {schedule.procedural_event_wait, schedule.repeating_always_wait, site = #schedule.continuation<id = 3>} : !clk
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
