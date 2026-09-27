// RUN: %python %S/Inputs/mutate-forward-segments.py budget %S/simulation-forward-segments.mlir > %t.input.mlir
// RUN: obelisk-opt %t.input.mlir --pass-pipeline='builtin.module(simulation.design(simulation.func(obelisk-sim-thread-process-cfg),obelisk-sim-build-compute-graph,obelisk-sim-verify-compute-graph,obelisk-sim-materialize-graph-regions,obelisk-sim-materialize-compute-fusion,obelisk-sim-specialize-static-state-nba,obelisk-sim-plan-static-superstep),encode-obelisk-sim-to-bytecode{vpi=off},convert-obelisk-sim-processes-to-llvm-coroutines)' -o %t.llvm.mlir
// RUN: FileCheck %s --check-prefix=PLAN < %t.llvm.mlir

// Runtime behavior is checked in ../Runtime/simulation-native-group-budget.test.

// PLAN-LABEL: llvm.func @__obelisk_eval_dispatch_v1(
// PLAN-NOT: llvm.call @__obelisk_eval_ranked_group_
// PLAN-NOT: schedule.eval.materialized_group_calls
// PLAN-NOT: schedule.eval.ssa_ready_words
// PLAN: llvm.call @__obelisk_direct_fragment_

// Only startup and the timed checker use node dispatch; clock work must
// execute through the native coordinator at both optimization levels.
