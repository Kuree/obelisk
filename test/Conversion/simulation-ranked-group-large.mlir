// RUN: %python %S/Inputs/mutate-forward-segments.py large-ssa %S/simulation-ranked-group-chain.mlir > %t.input.mlir
// RUN: obelisk-opt %t.input.mlir --pass-pipeline='builtin.module(simulation.design(simulation.func(obelisk-sim-thread-process-cfg),obelisk-sim-build-compute-graph,obelisk-sim-verify-compute-graph,obelisk-sim-materialize-graph-regions,obelisk-sim-materialize-compute-fusion,obelisk-sim-specialize-static-state-nba,obelisk-sim-plan-static-superstep),encode-obelisk-sim-to-bytecode{vpi=off},convert-obelisk-sim-processes-to-llvm-coroutines)' -o %t.llvm.mlir
// RUN: FileCheck %s --check-prefix=PLAN < %t.llvm.mlir

// Runtime behavior is checked in ../Runtime/simulation-ranked-group-large.test.

// PLAN-LABEL: llvm.func @__obelisk_eval_ranked_group_0(
// PLAN-SAME: schedule.eval.cache_hint_words = 2 : i64
// PLAN-SAME: schedule.eval.ranked_members = array<i32: 69, 0, 1, {{.*}}62, 63, 64, 65, 66, 67, 68>
// PLAN-NOT: llvm.call %
// PLAN-LABEL: llvm.func @__obelisk_eval_ranked_group_0.fallback(
// PLAN-SAME: schedule.eval.ssa_ready_words = 3 : i64
// PLAN-LABEL: llvm.func @__obelisk_eval_dispatch_v1(
// PLAN: llvm.call @__obelisk_eval_ranked_group_0

// Only startup and the timed checker use node dispatch; clock work must
// execute through the native coordinator at both optimization levels.
