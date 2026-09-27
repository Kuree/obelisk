// RUN: not obelisk-opt %s -split-input-file --pass-pipeline='builtin.module(simulation.design(simulation.func(obelisk-sim-thread-suspension),obelisk-sim-build-compute-graph,obelisk-sim-verify-compute-graph,obelisk-sim-materialize-graph-regions,obelisk-sim-materialize-compute-fusion,obelisk-sim-specialize-static-state-nba,obelisk-sim-plan-static-superstep),convert-obelisk-sim-processes-to-llvm-coroutines)' 2>&1 | FileCheck %s
//
// Empty or inferred effect summaries are not proof that a function is safe to
// duplicate in a checkpoint predicate. Inspect the complete helper closure:
// state reads need an overlay, writes must not run twice, and recursion needs
// a conservative cycle break. Forced eval must reject these unguarded owners.

// CHECK: obelisk eval probe rejected: work.__obelisk_eval_body_0: unsupported effect (simulation.call)
// CHECK: an eval owner keeps an unguarded runtime leaf in work.__obelisk_eval_body_0
module attributes {
  llvm.data_layout = "e-m:e-p:64:64-i64:64-n8:16:32:64-S128",
  llvm.target_triple = "x86_64-unknown-linux-gnu",
  schedule.native_scheduler = 3 : i32, obelisk.debug.native_timing
} {
  simulation.design @state_read {
    simulation.scope.decl 0
    simulation.storage.decl 0 in 0 : i1 design
    simulation.code_unit.decl 1 in 0 root_initializer hierarchy "root"
    simulation.code_unit.decl 2 in 0 always hierarchy "clock"
    simulation.code_unit.decl 3 in 0 always hierarchy "work"
    simulation.code_unit.decl 4 in 0 function hierarchy "helper"
    simulation.func @root(%ctx: !simulation.context {simulation.capture_kind = 0 : i32})
        attributes {entry_kind = 0 : i32, code_unit_id = 1 : i64} {
      %clock = simulation.context.storage %ctx[0] : !simulation.ref<i1>
      %c = simulation.spawn @clock(%ctx, %clock) : !simulation.context, !simulation.ref<i1> -> !simulation.process
      %w = simulation.spawn @work(%ctx, %clock) : !simulation.context, !simulation.ref<i1> -> !simulation.process
      simulation.return
    }
    simulation.func @clock(
        %ctx: !simulation.context {simulation.capture_kind = 0 : i32},
        %clock: !simulation.ref<i1> {simulation.capture_kind = 3 : i32, simulation.descriptor_id = 0 : i64})
        attributes {entry_kind = 3 : i32, code_unit_id = 2 : i64} {
      cf.br ^wait
    ^wait:
      %delay = simulation.time.constant 1
      simulation.suspend.delay %delay to ^toggle
    ^toggle:
      %old = simulation.ref.load %clock : !simulation.ref<i1> -> i1
      %one = arith.constant true
      %next = arith.xori %old, %one : i1
      simulation.ref.store %next to %clock : i1, !simulation.ref<i1>
      cf.br ^wait
    }
    simulation.func @work(
        %ctx: !simulation.context {simulation.capture_kind = 0 : i32},
        %clock: !simulation.ref<i1> {simulation.capture_kind = 3 : i32, simulation.descriptor_id = 0 : i64})
        attributes {entry_kind = 3 : i32, code_unit_id = 3 : i64} {
      cf.br ^wait
    ^wait:
      simulation.suspend.change %clock to ^body : !simulation.ref<i1>
    ^body:
      %value = simulation.ref.load %clock : !simulation.ref<i1> -> i1
      %result = simulation.call @helper(%ctx, %value) : (!simulation.context, i1) -> i1
      %requested = simulation.termination.requested %ctx
      cf.cond_br %requested, ^done, ^choose
    ^choose:
      cf.cond_br %result, ^fatal, ^wait
    ^fatal:
      %zero = arith.constant 0 : i32
      simulation.fatal %ctx, %zero
      simulation.return
    ^done:
      simulation.return
    }
    simulation.func private @helper(
        %ctx: !simulation.context {simulation.capture_kind = 0 : i32},
        %value: i1 {simulation.capture_kind = 2 : i32}) -> i1
        attributes {entry_kind = 8 : i32, code_unit_id = 4 : i64} {
      %ref = simulation.context.storage %ctx[0] : !simulation.ref<i1>
      %result = simulation.ref.load %ref : !simulation.ref<i1> -> i1
      simulation.return %result : i1
    }
  }
}

// -----

// CHECK: obelisk eval probe rejected: work.__obelisk_eval_body_0: unsupported effect (simulation.call)
// CHECK: an eval owner keeps an unguarded runtime leaf in work.__obelisk_eval_body_0
module attributes {
  llvm.data_layout = "e-m:e-p:64:64-i64:64-n8:16:32:64-S128",
  llvm.target_triple = "x86_64-unknown-linux-gnu",
  schedule.native_scheduler = 3 : i32, obelisk.debug.native_timing
} {
  simulation.design @state_write {
    simulation.scope.decl 0
    simulation.storage.decl 0 in 0 : i1 design
    simulation.code_unit.decl 1 in 0 root_initializer hierarchy "root"
    simulation.code_unit.decl 2 in 0 always hierarchy "clock"
    simulation.code_unit.decl 3 in 0 always hierarchy "work"
    simulation.code_unit.decl 4 in 0 function hierarchy "helper"
    simulation.func @root(%ctx: !simulation.context {simulation.capture_kind = 0 : i32})
        attributes {entry_kind = 0 : i32, code_unit_id = 1 : i64} {
      %clock = simulation.context.storage %ctx[0] : !simulation.ref<i1>
      %c = simulation.spawn @clock(%ctx, %clock) : !simulation.context, !simulation.ref<i1> -> !simulation.process
      %w = simulation.spawn @work(%ctx, %clock) : !simulation.context, !simulation.ref<i1> -> !simulation.process
      simulation.return
    }
    simulation.func @clock(
        %ctx: !simulation.context {simulation.capture_kind = 0 : i32},
        %clock: !simulation.ref<i1> {simulation.capture_kind = 3 : i32, simulation.descriptor_id = 0 : i64})
        attributes {entry_kind = 3 : i32, code_unit_id = 2 : i64} {
      cf.br ^wait
    ^wait:
      %delay = simulation.time.constant 1
      simulation.suspend.delay %delay to ^toggle
    ^toggle:
      %old = simulation.ref.load %clock : !simulation.ref<i1> -> i1
      %one = arith.constant true
      %next = arith.xori %old, %one : i1
      simulation.ref.store %next to %clock : i1, !simulation.ref<i1>
      cf.br ^wait
    }
    simulation.func @work(
        %ctx: !simulation.context {simulation.capture_kind = 0 : i32},
        %clock: !simulation.ref<i1> {simulation.capture_kind = 3 : i32, simulation.descriptor_id = 0 : i64})
        attributes {entry_kind = 3 : i32, code_unit_id = 3 : i64} {
      cf.br ^wait
    ^wait:
      simulation.suspend.change %clock to ^body : !simulation.ref<i1>
    ^body:
      %value = simulation.ref.load %clock : !simulation.ref<i1> -> i1
      %result = simulation.call @helper(%ctx, %value) : (!simulation.context, i1) -> i1
      %requested = simulation.termination.requested %ctx
      cf.cond_br %requested, ^done, ^choose
    ^choose:
      cf.cond_br %result, ^fatal, ^wait
    ^fatal:
      %zero = arith.constant 0 : i32
      simulation.fatal %ctx, %zero
      simulation.return
    ^done:
      simulation.return
    }
    simulation.func private @helper(
        %ctx: !simulation.context {simulation.capture_kind = 0 : i32},
        %value: i1 {simulation.capture_kind = 2 : i32}) -> i1
        attributes {entry_kind = 8 : i32, code_unit_id = 4 : i64} {
      %ref = simulation.context.storage %ctx[0] : !simulation.ref<i1>
      simulation.ref.store %value to %ref : i1, !simulation.ref<i1>
      simulation.return %value : i1
    }
  }
}

// -----

// CHECK: obelisk eval probe rejected: work.__obelisk_eval_body_0: unsupported effect (simulation.call)
// CHECK: an eval owner keeps an unguarded runtime leaf in work.__obelisk_eval_body_0
module attributes {
  llvm.data_layout = "e-m:e-p:64:64-i64:64-n8:16:32:64-S128",
  llvm.target_triple = "x86_64-unknown-linux-gnu",
  schedule.native_scheduler = 3 : i32, obelisk.debug.native_timing
} {
  simulation.design @recursive {
    simulation.scope.decl 0
    simulation.storage.decl 0 in 0 : i1 design
    simulation.code_unit.decl 1 in 0 root_initializer hierarchy "root"
    simulation.code_unit.decl 2 in 0 always hierarchy "clock"
    simulation.code_unit.decl 3 in 0 always hierarchy "work"
    simulation.code_unit.decl 4 in 0 function hierarchy "helper"
    simulation.func @root(%ctx: !simulation.context {simulation.capture_kind = 0 : i32})
        attributes {entry_kind = 0 : i32, code_unit_id = 1 : i64} {
      %clock = simulation.context.storage %ctx[0] : !simulation.ref<i1>
      %c = simulation.spawn @clock(%ctx, %clock) : !simulation.context, !simulation.ref<i1> -> !simulation.process
      %w = simulation.spawn @work(%ctx, %clock) : !simulation.context, !simulation.ref<i1> -> !simulation.process
      simulation.return
    }
    simulation.func @clock(
        %ctx: !simulation.context {simulation.capture_kind = 0 : i32},
        %clock: !simulation.ref<i1> {simulation.capture_kind = 3 : i32, simulation.descriptor_id = 0 : i64})
        attributes {entry_kind = 3 : i32, code_unit_id = 2 : i64} {
      cf.br ^wait
    ^wait:
      %delay = simulation.time.constant 1
      simulation.suspend.delay %delay to ^toggle
    ^toggle:
      %old = simulation.ref.load %clock : !simulation.ref<i1> -> i1
      %one = arith.constant true
      %next = arith.xori %old, %one : i1
      simulation.ref.store %next to %clock : i1, !simulation.ref<i1>
      cf.br ^wait
    }
    simulation.func @work(
        %ctx: !simulation.context {simulation.capture_kind = 0 : i32},
        %clock: !simulation.ref<i1> {simulation.capture_kind = 3 : i32, simulation.descriptor_id = 0 : i64})
        attributes {entry_kind = 3 : i32, code_unit_id = 3 : i64} {
      cf.br ^wait
    ^wait:
      simulation.suspend.change %clock to ^body : !simulation.ref<i1>
    ^body:
      %value = simulation.ref.load %clock : !simulation.ref<i1> -> i1
      %result = simulation.call @helper(%ctx, %value) : (!simulation.context, i1) -> i1
      %requested = simulation.termination.requested %ctx
      cf.cond_br %requested, ^done, ^choose
    ^choose:
      cf.cond_br %result, ^fatal, ^wait
    ^fatal:
      %zero = arith.constant 0 : i32
      simulation.fatal %ctx, %zero
      simulation.return
    ^done:
      simulation.return
    }
    simulation.func private @helper(
        %ctx: !simulation.context {simulation.capture_kind = 0 : i32},
        %value: i1 {simulation.capture_kind = 2 : i32}) -> i1
        attributes {entry_kind = 8 : i32, code_unit_id = 4 : i64} {
      %result = simulation.call @helper(%ctx, %value) : (!simulation.context, i1) -> i1
      simulation.return %result : i1
    }
  }
}
