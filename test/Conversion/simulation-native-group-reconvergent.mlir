// RUN: %python %S/Inputs/mutate-forward-segments.py reconvergent %S/simulation-forward-segments.mlir > %t.input.mlir
// RUN: obelisk-opt %t.input.mlir --pass-pipeline='builtin.module(obelisk_sim.design(obelisk_sim.func(obelisk-sim-thread-process-cfg),obelisk-sim-build-compute-graph,obelisk-sim-verify-compute-graph,obelisk-sim-materialize-graph-regions,obelisk-sim-materialize-compute-fusion,obelisk-sim-specialize-static-state-nba,obelisk-sim-plan-static-superstep),encode-obelisk-sim-to-bytecode{vpi=off},convert-obelisk-sim-processes-to-llvm-coroutines)' -o %t.llvm.mlir
// RUN: FileCheck %s --check-prefix=PLAN < %t.llvm.mlir

// Runtime behavior is checked in ../Runtime/simulation-native-group-reconvergent.test.

// Owners 1 and 2 read the same producer but do not activate each other.
// Their adjacent owner numbers must not create a forward predicate scan.
// The reconvergent consumer still runs under its actual ready predicate.
// PLAN-LABEL: llvm.func @__obelisk_eval_ranked_group_0(
// PLAN-SAME: schedule.eval.ranked_members = array<i32: 0, 1, 2, 3, 4>
// PLAN-LABEL: llvm.func @__obelisk_eval_dispatch_v1(
// PLAN: llvm.call @__obelisk_eval_ranked_group_0

// Only startup and the timed checker use node dispatch; clock work must
// execute through the native coordinator at both optimization levels.
