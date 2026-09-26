// RUN: %python %S/Inputs/mutate-forward-segments.py external-split %S/simulation-ranked-group-external-clock.mlir > %t.input.mlir
// RUN: obelisk-opt %t.input.mlir --pass-pipeline='builtin.module(obelisk_sim.design(obelisk_sim.func(obelisk-sim-thread-process-cfg),obelisk-sim-build-compute-graph{vpi=full},obelisk-sim-verify-compute-graph,obelisk-sim-materialize-graph-regions,obelisk-sim-materialize-compute-fusion,obelisk-sim-specialize-static-state-nba,obelisk-sim-plan-static-superstep),encode-obelisk-sim-to-bytecode{vpi=full},convert-obelisk-sim-processes-to-llvm-coroutines)' -o %t.llvm.mlir
// RUN: FileCheck %s --check-prefix=PLAN < %t.llvm.mlir

// Runtime behavior is checked in ../Runtime/simulation-ranked-segment-external-clock.test.

// Twelve copy actors cross native size-budget splits. VPI drives every clock
// and observes the final copy after the same shared-loop slot has settled.
// The segment must preserve timed callbacks, termination and startup counts.
// PLAN-LABEL: llvm.func @__obelisk_eval_ranked_group_0.segment(
// PLAN-SAME: obelisk.eval.segment_helpers
// PLAN: llvm.call @__obelisk_eval_ranked_group_0(
// PLAN: llvm.call @__obelisk_eval_ranked_group_1(
// PLAN: llvm.call @__obelisk_eval_ranked_group_2(
// PLAN: llvm.call @__obelisk_eval_ranked_group_3(
// PLAN-LABEL: llvm.func @__obelisk_eval_dispatch_v1(
// PLAN: llvm.call @__obelisk_eval_ranked_group_0.segment(
// Startup initializes fourteen actors and reactivates three early-registered
// relays. Subsequent external clock edges stay in native group execution.
