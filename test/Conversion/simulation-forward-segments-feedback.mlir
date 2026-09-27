// RUN: %python %S/Inputs/mutate-forward-segments.py feedback %S/simulation-forward-segments.mlir > %t.input.mlir
// RUN: obelisk-opt %t.input.mlir --pass-pipeline='builtin.module(obelisk_sim.design(obelisk_sim.func(obelisk-sim-thread-process-cfg),obelisk-sim-build-compute-graph,obelisk-sim-verify-compute-graph,obelisk-sim-materialize-graph-regions,obelisk-sim-materialize-compute-fusion,obelisk-sim-specialize-static-state-nba,obelisk-sim-plan-static-superstep),encode-obelisk-sim-to-bytecode{vpi=off},convert-obelisk-sim-processes-to-llvm-coroutines)' -o %t.llvm.mlir
// RUN: FileCheck %s --check-prefix=PLAN < %t.llvm.mlir

// Runtime behavior is checked in ../Runtime/simulation-forward-segments-feedback.test.

// The boundary owner is excluded; independent groups retain native work.
// PLAN-LABEL: llvm.func @__obelisk_eval_ranked_group_0(
// PLAN-SAME: schedule.eval.ranked_members = array<i32: 0, 1>
// PLAN-LABEL: llvm.func @__obelisk_eval_ranked_group_1(
// PLAN-SAME: schedule.eval.ranked_members = array<i32: 3, 4>
// PLAN-LABEL: llvm.func @__obelisk_eval_dispatch_v1(
// PLAN: llvm.call @__obelisk_eval_ranked_group_0
// PLAN: llvm.call @__obelisk_eval_ranked_group_1

// Only startup and the timed checker use node dispatch; clock work must
// execute through the native coordinator at both optimization levels.
