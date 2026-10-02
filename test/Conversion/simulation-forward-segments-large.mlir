// RUN: %python %S/Inputs/mutate-forward-segments.py large %S/simulation-forward-segments.mlir > %t.input.mlir
// RUN: obelisk-opt %t.input.mlir --pass-pipeline='builtin.module(simulation.design(simulation.func(obelisk-sim-thread-process-cfg),obelisk-sim-build-compute-graph,obelisk-sim-verify-compute-graph,obelisk-sim-materialize-graph-regions,obelisk-sim-materialize-compute-fusion,obelisk-sim-specialize-static-state-nba,obelisk-sim-plan-static-superstep),encode-obelisk-sim-to-bytecode{vpi=off},convert-obelisk-sim-processes-to-llvm-coroutines)' -o %t.llvm.mlir
// RUN: FileCheck %s --check-prefix=PLAN < %t.llvm.mlir

// Runtime behavior is checked in ../Runtime/simulation-forward-segments-large.test.

// PLAN-LABEL: llvm.func @__obelisk_eval_ranked_group_0(
// PLAN-SAME: schedule.eval.ranked_members = array<i32: {{.*}}66, 68, 69,{{.*}}>
// PLAN-SAME: schedule.eval.ssa_value_ranges = 2 : i64
// PLAN-LABEL: llvm.func @__obelisk_eval_ranked_group_2(
// PLAN-SAME: schedule.eval.cache_hint_words = 2 : i64
// PLAN-SAME: schedule.eval.predicated_dataflow
// PLAN-LABEL: llvm.func @__obelisk_eval_ranked_group_2.fallback(
// PLAN-SAME: schedule.eval.ssa_ready_words = 3 : i64
// PLAN-LABEL: llvm.func @__obelisk_eval_ranked_group_0.segment(
// PLAN-SAME: schedule.eval.segment_helpers = [@__obelisk_eval_ranked_group_0, @__obelisk_eval_ranked_group_1, @__obelisk_eval_ranked_group_2]

// Only startup and the timed checker use node dispatch; clock work must
// execute through the native coordinator at both optimization levels.
