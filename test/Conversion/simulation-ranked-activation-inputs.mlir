// RUN: obelisk-opt %S/../Runtime/Inputs/ranked-activation-inputs.mlir --pass-pipeline='builtin.module(simulation.design(simulation.func(obelisk-sim-thread-suspension),obelisk-sim-build-compute-graph,obelisk-sim-verify-compute-graph,obelisk-sim-materialize-graph-regions,obelisk-sim-materialize-compute-fusion,obelisk-sim-specialize-static-state-nba,obelisk-sim-plan-static-superstep),encode-obelisk-sim-to-bytecode{vpi=off prune-native=true},convert-obelisk-sim-processes-to-llvm-coroutines)' -o %t.mlir 2> %t.trace
// RUN: FileCheck %s --check-prefix=TRACE < %t.trace
// RUN: FileCheck %s --check-prefix=GROUP < %t.mlir

// The read-after-write temporary joins the ranked sweep. A read-before-write
// value and a conditional definition retain the original executor boundary.
// Runtime behavior is compared with bytecode at both O0/O3.
// TRACE-DAG: obelisk ranked activation: captured_state valid=1 complete=1
// TRACE-DAG: obelisk ranked activation: partial_definition valid=1 complete=0
// TRACE-DAG: obelisk ranked activation: temporary valid=1 complete=1
// TRACE: obelisk ranked groups: candidates=1 groups=1 segments=0 members=1 clocked_members=0
// TRACE: obelisk eval dependency: owner=2 island={{[0-9]+}} body=temporary.__obelisk_eval_body_0 successors=
// GROUP-LABEL: llvm.func @__obelisk_eval_ranked_group_0(
// GROUP-SAME: schedule.eval.ranked_members = array<i32: 2>
