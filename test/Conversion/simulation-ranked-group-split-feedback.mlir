// RUN: talos-opt %S/simulation-ranked-group-routing.mlir '--test-talos-set-module-attributes=attrs="{schedule.native.max_inline_ops = 700 : i64}"' > %t.input.mlir
// RUN: talos-opt %t.input.mlir --pass-pipeline='builtin.module(simulation.design(simulation.func(talos-sim-thread-process-cfg),talos-sim-build-compute-graph,talos-sim-verify-compute-graph,talos-sim-materialize-graph-regions,talos-sim-materialize-compute-fusion,talos-sim-specialize-static-state-nba,talos-sim-plan-static-superstep),encode-talos-sim-to-bytecode{vpi=off},convert-talos-sim-processes-to-llvm-coroutines)' -o %t.llvm.mlir
// RUN: FileCheck %s --check-prefix=PLAN < %t.llvm.mlir

// A configured routing SCC crosses a native size-budget split. The shared
// dispatcher selects one finite segment. Its direct calls retain the bounded
// helpers and their predicated dataflow; backward publications remain pending
// for another segment activation, without a global dispatch at the size split.
// PLAN-LABEL: llvm.func @__talos_eval_ranked_group_0(
// PLAN-SAME: schedule.eval.predicated_dataflow
// PLAN-LABEL: llvm.func @__talos_eval_ranked_group_1(
// PLAN-SAME: schedule.eval.predicated_dataflow
// PLAN-LABEL: llvm.func @__talos_eval_ranked_group_0.segment(
// PLAN-SAME: schedule.eval.segment_helpers = [@__talos_eval_ranked_group_0, @__talos_eval_ranked_group_1]
// PLAN-DAG: llvm.call @__talos_eval_ranked_group_0(
// PLAN-DAG: llvm.call @__talos_eval_ranked_group_1(
// PLAN-LABEL: llvm.func @__talos_eval_dispatch_v1(
// PLAN: llvm.call @__talos_eval_ranked_group_0.segment(
// PLAN-NOT: llvm.call @__talos_eval_ranked_group_1(
