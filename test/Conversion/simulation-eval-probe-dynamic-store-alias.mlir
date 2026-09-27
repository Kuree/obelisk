// RUN: obelisk-opt --pass-pipeline='builtin.module(simulation.design(simulation.func(obelisk-sim-thread-suspension),obelisk-sim-build-compute-graph,obelisk-sim-verify-compute-graph,obelisk-sim-materialize-graph-regions,obelisk-sim-materialize-compute-fusion,obelisk-sim-specialize-static-state-nba,obelisk-sim-plan-static-superstep),convert-obelisk-sim-processes-to-llvm-coroutines)' %s | FileCheck %s

// CHECK: module attributes {{.*}}schedule.eval.generated
// CHECK: llvm.func @work.__obelisk_eval_body_0.__obelisk_checkpoint_path

// A dynamic select is bounded by its fixed physical root. The adjacent overlap test checks
// that a store to another root can be discarded, while a possibly aliasing
// select on the same root still rejects generated evaluation.
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
    simulation.storage.decl 3 in 0 : !simulation.logic<16> design
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
      %dst = simulation.ref.extract %data from 0 : !simulation.ref<!simulation.logic<16>> -> !simulation.ref<!simulation.logic<8>>
      %seven = simulation.logic.constant 7 : i8, 0 : i8 : !simulation.logic<8>
      simulation.ref.store %seven to %dst : !simulation.logic<8>, !simulation.ref<!simulation.logic<8>>
      %index = simulation.context.storage %ctx[2] : !simulation.ref<i32>
      %low = simulation.ref.load %index : !simulation.ref<i32> -> i32
      %other = simulation.context.storage %ctx[3] : !simulation.ref<!simulation.logic<16>>
      %src = simulation.ref.dyn_extract %other from %low : (!simulation.ref<!simulation.logic<16>>, i32) -> !simulation.ref<!simulation.logic<8>>
      %read = simulation.ref.load %src : !simulation.ref<!simulation.logic<8>> -> !simulation.logic<8>
      %bad = simulation.logic.compare case_eq %read, %seven : (!simulation.logic<8>, !simulation.logic<8>) -> i1
      cf.cond_br %bad, ^cold, ^wait
    ^cold:
      %verbosity = arith.constant 0 : i32
      simulation.fatal %ctx, %verbosity
      simulation.return
    }
  }
}