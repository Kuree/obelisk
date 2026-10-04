// RUN: %python %S/Inputs/mutate-forward-segments.py initial-edge %S/simulation-forward-segments.mlir > %t.input.mlir
// RUN: obelisk-opt %t.input.mlir --pass-pipeline='builtin.module(simulation.design(simulation.func(obelisk-sim-thread-process-cfg),obelisk-sim-build-compute-graph,obelisk-sim-verify-compute-graph,obelisk-sim-materialize-graph-regions,obelisk-sim-materialize-compute-fusion,obelisk-sim-specialize-static-state-nba,obelisk-sim-plan-static-superstep),encode-obelisk-sim-to-bytecode{vpi=off},convert-obelisk-sim-processes-to-llvm-coroutines)' -o %t.llvm.mlir
// RUN: FileCheck %s --check-prefix=PLAN < %t.llvm.mlir

// Runtime behavior is checked in ../Runtime/simulation-initial-edge-nba-order.test.

// The initial edge waiter observes pre-NBA state in Active even though its
// complete body is native compilable. The shared calendar owns the barrier.
// PLAN: llvm.call @obelisk_rt_v1_scheduler_drain_periodic_aot
