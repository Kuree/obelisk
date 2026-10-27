// RUN: talos-opt %s --pass-pipeline='builtin.module(simulation.design(simulation.func(talos-sim-thread-suspension),talos-sim-build-compute-graph,talos-sim-verify-compute-graph,talos-sim-materialize-graph-regions,talos-sim-materialize-compute-fusion,talos-sim-specialize-static-state-nba,talos-sim-plan-static-superstep),convert-talos-sim-processes-to-llvm-coroutines)' | FileCheck %s --implicit-check-not='llvm.call @talos_rt_v1_scheduler_execute_aot_actor'

// A second hot edge exposes a private local allocation. Its loads cannot alias
// design state, so the dry-run predicate and activation stay native. Only the
// fatal branch needs a cold checkpoint (LRM 4.6 and 6.21).
// CHECK: module attributes {{.*}}schedule.eval.generated
// CHECK: llvm.func @work.__talos_eval_body_0.__talos_path_known_
// CHECK: llvm.func @work.__talos_eval_body_0.__talos_checkpoint_path_
// CHECK: llvm.func @__talos_direct_fragment_2_{{[0-9]+}}.__talos_execute(
// CHECK: llvm.call @__talos_eval_path_dispatch_v1_
// CHECK: llvm.func @__talos_eval_checkpoint_body_v1_
// CHECK: llvm.call @talos_rt_v1_scheduler_fatal

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
    simulation.code_unit.decl 4 in 0 function hierarchy "temporary_helper"
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
      cf.cond_br %bad, ^cold, ^after_cold
    ^cold:
      %verbosity = arith.constant 0 : i32
      simulation.fatal %ctx, %verbosity
      cf.br ^after_cold
    ^after_cold:
      %default = simulation.logic.constant 0 : i16, 0 : i16 : !simulation.logic<16>
      %local = simulation.ref.alloc %default : !simulation.logic<16> -> !simulation.ref<!simulation.logic<16>>
      %temporary = simulation.ref.load %local : !simulation.ref<!simulation.logic<16>> -> !simulation.logic<16>
      simulation.ref.store %temporary to %data : !simulation.logic<16>, !simulation.ref<!simulation.logic<16>>
      %slice = simulation.ref.extract %local from 4 : !simulation.ref<!simulation.logic<16>> -> !simulation.ref<!simulation.logic<8>>
      %part = simulation.ref.load %slice : !simulation.ref<!simulation.logic<8>> -> !simulation.logic<8>
      simulation.ref.store %part to %dst : !simulation.logic<8>, !simulation.ref<!simulation.logic<8>>
      %helper_value = simulation.call @temporary_helper(%ctx) : (!simulation.context) -> !simulation.logic<16>
      simulation.ref.store %helper_value to %data : !simulation.logic<16>, !simulation.ref<!simulation.logic<16>>
      cf.br ^wait
    }
    simulation.func private @temporary_helper(%ctx: !simulation.context {simulation.capture_kind = 0 : i32}) -> !simulation.logic<16>
        attributes {entry_kind = 8 : i32, code_unit_id = 4 : i64} {
      %default = simulation.logic.constant 0 : i16, 0 : i16 : !simulation.logic<16>
      %local = simulation.ref.alloc %default : !simulation.logic<16> -> !simulation.ref<!simulation.logic<16>>
      %value = simulation.ref.load %local : !simulation.ref<!simulation.logic<16>> -> !simulation.logic<16>
      simulation.return %value : !simulation.logic<16>
    }
  }
}
