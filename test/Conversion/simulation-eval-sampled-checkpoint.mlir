// RUN: obelisk-opt %s --pass-pipeline='builtin.module(simulation.design(simulation.func(obelisk-sim-thread-suspension),obelisk-sim-build-compute-graph,obelisk-sim-verify-compute-graph,obelisk-sim-materialize-graph-regions,obelisk-sim-materialize-compute-fusion,obelisk-sim-specialize-static-state-nba,obelisk-sim-plan-static-superstep),convert-obelisk-sim-processes-to-llvm-coroutines)' | FileCheck %s

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
  simulation.design @probe_alias {
    simulation.scope.decl 0
    simulation.storage.decl 0 in 0 : i1 design
    simulation.storage.decl 1 in 0 : !simulation.logic<16> design
    simulation.storage.decl 2 in 0 : i32 design
    simulation.code_unit.decl 1 in 0 root_initializer hierarchy "root"
    simulation.code_unit.decl 2 in 0 always hierarchy "clock"
    simulation.code_unit.decl 3 in 0 always hierarchy "work"
    simulation.func @root(%ctx: !simulation.context {simulation.capture_kind = 0 : i32})
        attributes {entry_kind = 0 : i32, code_unit_id = 1 : i64} {
      %clock = simulation.context.storage %ctx[0] : !simulation.ref<i1>
      %data = simulation.context.storage %ctx[1] : !simulation.ref<!simulation.logic<16>>
      %c = simulation.spawn @clock(%ctx, %clock) : !simulation.context, !simulation.ref<i1> -> !simulation.process
      %w = simulation.spawn @work(%ctx, %clock, %data) : !simulation.context, !simulation.ref<i1>, !simulation.ref<!simulation.logic<16>> -> !simulation.process
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
        %clock: !simulation.ref<i1> {simulation.capture_kind = 3 : i32, simulation.descriptor_id = 0 : i64},
        %data: !simulation.ref<!simulation.logic<16>> {simulation.capture_kind = 3 : i32, simulation.descriptor_id = 1 : i64})
        attributes {entry_kind = 3 : i32, code_unit_id = 3 : i64} {
      cf.br ^wait
    ^wait:
      simulation.suspend.change %clock to ^body : !simulation.ref<i1>
    ^body:
      %index = simulation.context.storage %ctx[2] : !simulation.ref<i32>
      %select = simulation.ref.load %index : !simulation.ref<i32> -> i32
      %zero = arith.constant 0 : i32
      %one = arith.constant 1 : i32
      %read = arith.cmpi eq, %select, %zero : i32
      cf.cond_br %read, ^sampled, ^choose_history
    ^sampled:
      %sample = simulation.assert.sampled_read %ctx from %data : (!simulation.context, !simulation.ref<!simulation.logic<16>>) -> !simulation.logic<16>
      simulation.ref.store %sample to %data : !simulation.logic<16>, !simulation.ref<!simulation.logic<16>>
      cf.br ^wait
    ^choose_history:
      %history = arith.cmpi eq, %select, %one : i32
      cf.cond_br %history, ^past, ^wait
    ^past:
      %current = simulation.ref.load %data : !simulation.ref<!simulation.logic<16>> -> !simulation.logic<16>
      %enabled = arith.constant true
      %previous = simulation.assert.sampled_history %ctx from %current gate %enabled id 42 depth 1 : (!simulation.context, !simulation.logic<16>, i1) -> !simulation.logic<16>
      simulation.ref.store %previous to %data : !simulation.logic<16>, !simulation.ref<!simulation.logic<16>>
      cf.br ^wait
    }
  }
}
