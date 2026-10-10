// RUN: %python %S/Inputs/mutate-forward-segments.py external-split %S/simulation-ranked-group-external-clock.mlir > %t.input.mlir
// RUN: obelisk-opt %t.input.mlir --pass-pipeline='builtin.module(simulation.design(simulation.func(obelisk-sim-thread-process-cfg),obelisk-sim-build-compute-graph{vpi=full},obelisk-sim-verify-compute-graph,obelisk-sim-materialize-graph-regions,obelisk-sim-materialize-compute-fusion,obelisk-sim-specialize-static-state-nba,obelisk-sim-plan-static-superstep),encode-obelisk-sim-to-bytecode{vpi=full},convert-obelisk-sim-processes-to-llvm-coroutines)' -o %t.llvm.mlir
// RUN: FileCheck %s --check-prefix=PLAN < %t.llvm.mlir

// Runtime behavior is checked in ../Runtime/simulation-ranked-segment-external-clock.test.

// Twelve copy actors cross native size-budget splits. Writable VPI retains
// their fine ingress: dirty owners use the shared Tier 2/3 actor selector,
// while an unaffected owner still calls its original Tier 1 executor.
// PLAN-NOT: llvm.func @__obelisk_eval_ranked_group_
// PLAN-LABEL: llvm.func @__obelisk_eval_write_guard_0_v1(
// PLAN: llvm.call @obelisk_rt_v1_static_specialization_guard
// PLAN-LABEL: llvm.func @__obelisk_eval_write_guard_12_v1(
// PLAN: llvm.call @obelisk_rt_v1_static_specialization_guard
// PLAN-LABEL: llvm.func @__obelisk_eval_dispatch_v1(
// PLAN: llvm.call @__obelisk_eval_write_guard_0_v1(
// PLAN: llvm.call @__obelisk_eval_write_guard_12_v1(
// PLAN: llvm.call @obelisk_rt_v1_scheduler_execute_aot_actor(
