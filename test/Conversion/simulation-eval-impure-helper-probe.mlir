// RUN: not obelisk-opt %s -split-input-file --pass-pipeline='builtin.module(obelisk_sim.design(obelisk_sim.func(obelisk-sim-thread-suspension),obelisk-sim-build-compute-graph,obelisk-sim-verify-compute-graph,obelisk-sim-materialize-graph-regions,obelisk-sim-materialize-compute-fusion,obelisk-sim-specialize-static-state-nba,obelisk-sim-plan-static-superstep),convert-obelisk-sim-processes-to-llvm-coroutines)' 2>&1 | FileCheck %s
//
// Empty or inferred effect summaries are not proof that a function is safe to
// duplicate in a checkpoint predicate. Inspect the complete helper closure:
// state reads need an overlay, writes must not run twice, and recursion needs
// a conservative cycle break. Forced eval must reject these unguarded owners.

// CHECK: obelisk eval probe rejected: work.__obelisk_eval_body_0: unsupported effect (obelisk_sim.call)
// CHECK: an eval owner keeps an unguarded runtime leaf in work.__obelisk_eval_body_0
module attributes {
  llvm.data_layout = "e-m:e-p:64:64-i64:64-n8:16:32:64-S128",
  llvm.target_triple = "x86_64-unknown-linux-gnu",
  obelisk.native_scheduler = 3 : i32, obelisk.debug.native_timing
} {
  obelisk_sim.design @state_read {
    obelisk_sim.scope.decl 0
    obelisk_sim.storage.decl 0 in 0 : i1 design
    obelisk_sim.code_unit.decl 1 in 0 root_initializer hierarchy "root"
    obelisk_sim.code_unit.decl 2 in 0 always hierarchy "clock"
    obelisk_sim.code_unit.decl 3 in 0 always hierarchy "work"
    obelisk_sim.code_unit.decl 4 in 0 function hierarchy "helper"
    obelisk_sim.func @root(%ctx: !obelisk_sim.context {obelisk_sim.capture_kind = 0 : i32})
        attributes {entry_kind = 0 : i32, code_unit_id = 1 : i64} {
      %clock = obelisk_sim.context.storage %ctx[0] : !obelisk_sim.ref<i1>
      %c = obelisk_sim.spawn @clock(%ctx, %clock) : !obelisk_sim.context, !obelisk_sim.ref<i1> -> !obelisk_sim.process
      %w = obelisk_sim.spawn @work(%ctx, %clock) : !obelisk_sim.context, !obelisk_sim.ref<i1> -> !obelisk_sim.process
      obelisk_sim.return
    }
    obelisk_sim.func @clock(
        %ctx: !obelisk_sim.context {obelisk_sim.capture_kind = 0 : i32},
        %clock: !obelisk_sim.ref<i1> {obelisk_sim.capture_kind = 3 : i32, obelisk_sim.descriptor_id = 0 : i64})
        attributes {entry_kind = 3 : i32, code_unit_id = 2 : i64} {
      cf.br ^wait
    ^wait:
      %delay = obelisk_sim.time.constant 1
      obelisk_sim.suspend.delay %delay to ^toggle
    ^toggle:
      %old = obelisk_sim.ref.load %clock : !obelisk_sim.ref<i1> -> i1
      %one = arith.constant true
      %next = arith.xori %old, %one : i1
      obelisk_sim.ref.store %next to %clock : i1, !obelisk_sim.ref<i1>
      cf.br ^wait
    }
    obelisk_sim.func @work(
        %ctx: !obelisk_sim.context {obelisk_sim.capture_kind = 0 : i32},
        %clock: !obelisk_sim.ref<i1> {obelisk_sim.capture_kind = 3 : i32, obelisk_sim.descriptor_id = 0 : i64})
        attributes {entry_kind = 3 : i32, code_unit_id = 3 : i64} {
      cf.br ^wait
    ^wait:
      obelisk_sim.suspend.change %clock to ^body : !obelisk_sim.ref<i1>
    ^body:
      %value = obelisk_sim.ref.load %clock : !obelisk_sim.ref<i1> -> i1
      %result = obelisk_sim.call @helper(%ctx, %value) : (!obelisk_sim.context, i1) -> i1
      %requested = obelisk_sim.termination.requested %ctx
      cf.cond_br %requested, ^done, ^choose
    ^choose:
      cf.cond_br %result, ^fatal, ^wait
    ^fatal:
      %zero = arith.constant 0 : i32
      obelisk_sim.fatal %ctx, %zero
      obelisk_sim.return
    ^done:
      obelisk_sim.return
    }
    obelisk_sim.func private @helper(
        %ctx: !obelisk_sim.context {obelisk_sim.capture_kind = 0 : i32},
        %value: i1 {obelisk_sim.capture_kind = 2 : i32}) -> i1
        attributes {entry_kind = 8 : i32, code_unit_id = 4 : i64} {
      %ref = obelisk_sim.context.storage %ctx[0] : !obelisk_sim.ref<i1>
      %result = obelisk_sim.ref.load %ref : !obelisk_sim.ref<i1> -> i1
      obelisk_sim.return %result : i1
    }
  }
}

// -----

// CHECK: obelisk eval probe rejected: work.__obelisk_eval_body_0: unsupported effect (obelisk_sim.call)
// CHECK: an eval owner keeps an unguarded runtime leaf in work.__obelisk_eval_body_0
module attributes {
  llvm.data_layout = "e-m:e-p:64:64-i64:64-n8:16:32:64-S128",
  llvm.target_triple = "x86_64-unknown-linux-gnu",
  obelisk.native_scheduler = 3 : i32, obelisk.debug.native_timing
} {
  obelisk_sim.design @state_write {
    obelisk_sim.scope.decl 0
    obelisk_sim.storage.decl 0 in 0 : i1 design
    obelisk_sim.code_unit.decl 1 in 0 root_initializer hierarchy "root"
    obelisk_sim.code_unit.decl 2 in 0 always hierarchy "clock"
    obelisk_sim.code_unit.decl 3 in 0 always hierarchy "work"
    obelisk_sim.code_unit.decl 4 in 0 function hierarchy "helper"
    obelisk_sim.func @root(%ctx: !obelisk_sim.context {obelisk_sim.capture_kind = 0 : i32})
        attributes {entry_kind = 0 : i32, code_unit_id = 1 : i64} {
      %clock = obelisk_sim.context.storage %ctx[0] : !obelisk_sim.ref<i1>
      %c = obelisk_sim.spawn @clock(%ctx, %clock) : !obelisk_sim.context, !obelisk_sim.ref<i1> -> !obelisk_sim.process
      %w = obelisk_sim.spawn @work(%ctx, %clock) : !obelisk_sim.context, !obelisk_sim.ref<i1> -> !obelisk_sim.process
      obelisk_sim.return
    }
    obelisk_sim.func @clock(
        %ctx: !obelisk_sim.context {obelisk_sim.capture_kind = 0 : i32},
        %clock: !obelisk_sim.ref<i1> {obelisk_sim.capture_kind = 3 : i32, obelisk_sim.descriptor_id = 0 : i64})
        attributes {entry_kind = 3 : i32, code_unit_id = 2 : i64} {
      cf.br ^wait
    ^wait:
      %delay = obelisk_sim.time.constant 1
      obelisk_sim.suspend.delay %delay to ^toggle
    ^toggle:
      %old = obelisk_sim.ref.load %clock : !obelisk_sim.ref<i1> -> i1
      %one = arith.constant true
      %next = arith.xori %old, %one : i1
      obelisk_sim.ref.store %next to %clock : i1, !obelisk_sim.ref<i1>
      cf.br ^wait
    }
    obelisk_sim.func @work(
        %ctx: !obelisk_sim.context {obelisk_sim.capture_kind = 0 : i32},
        %clock: !obelisk_sim.ref<i1> {obelisk_sim.capture_kind = 3 : i32, obelisk_sim.descriptor_id = 0 : i64})
        attributes {entry_kind = 3 : i32, code_unit_id = 3 : i64} {
      cf.br ^wait
    ^wait:
      obelisk_sim.suspend.change %clock to ^body : !obelisk_sim.ref<i1>
    ^body:
      %value = obelisk_sim.ref.load %clock : !obelisk_sim.ref<i1> -> i1
      %result = obelisk_sim.call @helper(%ctx, %value) : (!obelisk_sim.context, i1) -> i1
      %requested = obelisk_sim.termination.requested %ctx
      cf.cond_br %requested, ^done, ^choose
    ^choose:
      cf.cond_br %result, ^fatal, ^wait
    ^fatal:
      %zero = arith.constant 0 : i32
      obelisk_sim.fatal %ctx, %zero
      obelisk_sim.return
    ^done:
      obelisk_sim.return
    }
    obelisk_sim.func private @helper(
        %ctx: !obelisk_sim.context {obelisk_sim.capture_kind = 0 : i32},
        %value: i1 {obelisk_sim.capture_kind = 2 : i32}) -> i1
        attributes {entry_kind = 8 : i32, code_unit_id = 4 : i64} {
      %ref = obelisk_sim.context.storage %ctx[0] : !obelisk_sim.ref<i1>
      obelisk_sim.ref.store %value to %ref : i1, !obelisk_sim.ref<i1>
      obelisk_sim.return %value : i1
    }
  }
}

// -----

// CHECK: obelisk eval probe rejected: work.__obelisk_eval_body_0: unsupported effect (obelisk_sim.call)
// CHECK: an eval owner keeps an unguarded runtime leaf in work.__obelisk_eval_body_0
module attributes {
  llvm.data_layout = "e-m:e-p:64:64-i64:64-n8:16:32:64-S128",
  llvm.target_triple = "x86_64-unknown-linux-gnu",
  obelisk.native_scheduler = 3 : i32, obelisk.debug.native_timing
} {
  obelisk_sim.design @recursive {
    obelisk_sim.scope.decl 0
    obelisk_sim.storage.decl 0 in 0 : i1 design
    obelisk_sim.code_unit.decl 1 in 0 root_initializer hierarchy "root"
    obelisk_sim.code_unit.decl 2 in 0 always hierarchy "clock"
    obelisk_sim.code_unit.decl 3 in 0 always hierarchy "work"
    obelisk_sim.code_unit.decl 4 in 0 function hierarchy "helper"
    obelisk_sim.func @root(%ctx: !obelisk_sim.context {obelisk_sim.capture_kind = 0 : i32})
        attributes {entry_kind = 0 : i32, code_unit_id = 1 : i64} {
      %clock = obelisk_sim.context.storage %ctx[0] : !obelisk_sim.ref<i1>
      %c = obelisk_sim.spawn @clock(%ctx, %clock) : !obelisk_sim.context, !obelisk_sim.ref<i1> -> !obelisk_sim.process
      %w = obelisk_sim.spawn @work(%ctx, %clock) : !obelisk_sim.context, !obelisk_sim.ref<i1> -> !obelisk_sim.process
      obelisk_sim.return
    }
    obelisk_sim.func @clock(
        %ctx: !obelisk_sim.context {obelisk_sim.capture_kind = 0 : i32},
        %clock: !obelisk_sim.ref<i1> {obelisk_sim.capture_kind = 3 : i32, obelisk_sim.descriptor_id = 0 : i64})
        attributes {entry_kind = 3 : i32, code_unit_id = 2 : i64} {
      cf.br ^wait
    ^wait:
      %delay = obelisk_sim.time.constant 1
      obelisk_sim.suspend.delay %delay to ^toggle
    ^toggle:
      %old = obelisk_sim.ref.load %clock : !obelisk_sim.ref<i1> -> i1
      %one = arith.constant true
      %next = arith.xori %old, %one : i1
      obelisk_sim.ref.store %next to %clock : i1, !obelisk_sim.ref<i1>
      cf.br ^wait
    }
    obelisk_sim.func @work(
        %ctx: !obelisk_sim.context {obelisk_sim.capture_kind = 0 : i32},
        %clock: !obelisk_sim.ref<i1> {obelisk_sim.capture_kind = 3 : i32, obelisk_sim.descriptor_id = 0 : i64})
        attributes {entry_kind = 3 : i32, code_unit_id = 3 : i64} {
      cf.br ^wait
    ^wait:
      obelisk_sim.suspend.change %clock to ^body : !obelisk_sim.ref<i1>
    ^body:
      %value = obelisk_sim.ref.load %clock : !obelisk_sim.ref<i1> -> i1
      %result = obelisk_sim.call @helper(%ctx, %value) : (!obelisk_sim.context, i1) -> i1
      %requested = obelisk_sim.termination.requested %ctx
      cf.cond_br %requested, ^done, ^choose
    ^choose:
      cf.cond_br %result, ^fatal, ^wait
    ^fatal:
      %zero = arith.constant 0 : i32
      obelisk_sim.fatal %ctx, %zero
      obelisk_sim.return
    ^done:
      obelisk_sim.return
    }
    obelisk_sim.func private @helper(
        %ctx: !obelisk_sim.context {obelisk_sim.capture_kind = 0 : i32},
        %value: i1 {obelisk_sim.capture_kind = 2 : i32}) -> i1
        attributes {entry_kind = 8 : i32, code_unit_id = 4 : i64} {
      %result = obelisk_sim.call @helper(%ctx, %value) : (!obelisk_sim.context, i1) -> i1
      obelisk_sim.return %result : i1
    }
  }
}
