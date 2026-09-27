// RUN: sed 's/module attributes {/module attributes {schedule.native.max_inline_ops = 700 : i64,/' %S/simulation-ranked-group-routing.mlir > %t.input.mlir
// RUN: obelisk-opt %t.input.mlir --pass-pipeline='builtin.module(simulation.design(simulation.func(obelisk-sim-thread-process-cfg),obelisk-sim-build-compute-graph,obelisk-sim-verify-compute-graph,obelisk-sim-materialize-graph-regions,obelisk-sim-materialize-compute-fusion,obelisk-sim-specialize-static-state-nba,obelisk-sim-plan-static-superstep),encode-obelisk-sim-to-bytecode{vpi=off},convert-obelisk-sim-processes-to-llvm-coroutines)' -o %t.llvm.mlir
// RUN: FileCheck %s --check-prefix=PLAN < %t.llvm.mlir

// Runtime behavior is checked in ../Runtime/simulation-ranked-group-split-feedback.test.

// A configured routing SCC crosses a native size-budget split. The shared
// dispatcher selects one finite segment. Its direct calls retain the bounded
// helpers and their predicated dataflow; backward publications remain pending
// for another segment activation, without a global dispatch at the size split.
// PLAN-LABEL: llvm.func @__obelisk_eval_ranked_group_0(
// PLAN-SAME: schedule.eval.predicated_dataflow
// PLAN-LABEL: llvm.func @__obelisk_eval_ranked_group_1(
// PLAN-SAME: schedule.eval.predicated_dataflow
// PLAN-LABEL: llvm.func @__obelisk_eval_ranked_group_0.segment(
// PLAN-SAME: schedule.eval.segment_helpers = [@__obelisk_eval_ranked_group_0, @__obelisk_eval_ranked_group_1]
// PLAN-DAG: llvm.call @__obelisk_eval_ranked_group_0(
// PLAN-DAG: llvm.call @__obelisk_eval_ranked_group_1(
// PLAN-LABEL: llvm.func @__obelisk_eval_dispatch_v1(
// PLAN: llvm.call @__obelisk_eval_ranked_group_0.segment(
// PLAN-NOT: llvm.call @__obelisk_eval_ranked_group_1(
