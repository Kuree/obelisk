// RUN: obelisk-opt %s --pass-pipeline='builtin.module(obelisk_sim.design(obelisk_sim.func(obelisk-sim-thread-suspension),obelisk-sim-build-compute-graph,obelisk-sim-verify-compute-graph,obelisk-sim-materialize-graph-regions,obelisk-sim-materialize-compute-fusion,obelisk-sim-specialize-static-state-nba,obelisk-sim-plan-static-superstep),convert-obelisk-sim-processes-to-llvm-coroutines)' | FileCheck %s

// Preponed snapshots and sampled-history rings are runtime-owned. Each query
// must independently create a checkpoint; neither may silently enter a hot
// generated closure just because the surrounding output supports snapshots.
// CHECK: module attributes {{.*}}schedule.eval.generated
// CHECK-DAG: llvm.func @work.__obelisk_eval_body_0.__obelisk_checkpoint_path
// CHECK-DAG: llvm.call @obelisk_rt_v1_sampled_read
// CHECK-DAG: llvm.call @obelisk_rt_v1_sampled_history
module attributes {
  llvm.data_layout = "e-m:e-p:64:64-i64:64-n8:16:32:64-S128",
  llvm.target_triple = "x86_64-unknown-linux-gnu",
  schedule.native_scheduler = 3 : i32
} {
  obelisk_sim.design @probe_alias {
    obelisk_sim.scope.decl 0
    obelisk_sim.storage.decl 0 in 0 : i1 design
    obelisk_sim.storage.decl 1 in 0 : !obelisk_sim.logic<16> design
    obelisk_sim.storage.decl 2 in 0 : i32 design
    obelisk_sim.code_unit.decl 1 in 0 root_initializer hierarchy "root"
    obelisk_sim.code_unit.decl 2 in 0 always hierarchy "clock"
    obelisk_sim.code_unit.decl 3 in 0 always hierarchy "work"
    obelisk_sim.func @root(%ctx: !obelisk_sim.context {obelisk_sim.capture_kind = 0 : i32})
        attributes {entry_kind = 0 : i32, code_unit_id = 1 : i64} {
      %clock = obelisk_sim.context.storage %ctx[0] : !obelisk_sim.ref<i1>
      %data = obelisk_sim.context.storage %ctx[1] : !obelisk_sim.ref<!obelisk_sim.logic<16>>
      %c = obelisk_sim.spawn @clock(%ctx, %clock) : !obelisk_sim.context, !obelisk_sim.ref<i1> -> !obelisk_sim.process
      %w = obelisk_sim.spawn @work(%ctx, %clock, %data) : !obelisk_sim.context, !obelisk_sim.ref<i1>, !obelisk_sim.ref<!obelisk_sim.logic<16>> -> !obelisk_sim.process
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
        %clock: !obelisk_sim.ref<i1> {obelisk_sim.capture_kind = 3 : i32, obelisk_sim.descriptor_id = 0 : i64},
        %data: !obelisk_sim.ref<!obelisk_sim.logic<16>> {obelisk_sim.capture_kind = 3 : i32, obelisk_sim.descriptor_id = 1 : i64})
        attributes {entry_kind = 3 : i32, code_unit_id = 3 : i64} {
      cf.br ^wait
    ^wait:
      obelisk_sim.suspend.change %clock to ^body : !obelisk_sim.ref<i1>
    ^body:
      %index = obelisk_sim.context.storage %ctx[2] : !obelisk_sim.ref<i32>
      %select = obelisk_sim.ref.load %index : !obelisk_sim.ref<i32> -> i32
      %zero = arith.constant 0 : i32
      %one = arith.constant 1 : i32
      %read = arith.cmpi eq, %select, %zero : i32
      cf.cond_br %read, ^sampled, ^choose_history
    ^sampled:
      %sample = obelisk_sim.assert.sampled_read %ctx from %data : (!obelisk_sim.context, !obelisk_sim.ref<!obelisk_sim.logic<16>>) -> !obelisk_sim.logic<16>
      obelisk_sim.ref.store %sample to %data : !obelisk_sim.logic<16>, !obelisk_sim.ref<!obelisk_sim.logic<16>>
      cf.br ^wait
    ^choose_history:
      %history = arith.cmpi eq, %select, %one : i32
      cf.cond_br %history, ^past, ^wait
    ^past:
      %current = obelisk_sim.ref.load %data : !obelisk_sim.ref<!obelisk_sim.logic<16>> -> !obelisk_sim.logic<16>
      %enabled = arith.constant true
      %previous = obelisk_sim.assert.sampled_history %ctx from %current gate %enabled id 42 depth 1 : (!obelisk_sim.context, !obelisk_sim.logic<16>, i1) -> !obelisk_sim.logic<16>
      obelisk_sim.ref.store %previous to %data : !obelisk_sim.logic<16>, !obelisk_sim.ref<!obelisk_sim.logic<16>>
      cf.br ^wait
    }
  }
}
