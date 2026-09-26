// RUN: %python %S/Inputs/gen-multiword-ready-nba.py 65 > %t.mlir
// RUN: obelisk-opt %t.mlir --pass-pipeline='builtin.module(obelisk_sim.design(obelisk_sim.func(obelisk-sim-thread-process-cfg),obelisk-sim-build-compute-graph,obelisk-sim-verify-compute-graph,obelisk-sim-specialize-static-state-nba,obelisk-sim-plan-static-superstep),encode-obelisk-sim-to-bytecode{vpi=off},convert-obelisk-sim-processes-to-llvm-coroutines)' -o %t.llvm.mlir
// RUN: FileCheck %s --check-prefix=PLAN < %t.llvm.mlir

// Runtime behavior is checked in ../Runtime/simulation-multiword-ready-nba-runtime.test.

// LRM 4.5, 4.6, 4.9.4: all ready words must drain before the NBA barrier.
// A second clock transition exercises reactivation after cached emptiness.
// Keep fragments separate in this pass-specific test: fusion would hide the
// multiword runtime boundary. At least 100 nodes ensures it remains covered.
// PLAN: llvm.mlir.global internal constant @__obelisk_aot_schedule_nodes_v1()
// PLAN-SAME: !llvm.array<{{[1-9][0-9][0-9]+}} x struct<(i32, i32, i32)>>
// PLAN: llvm.call @obelisk_rt_v1_scheduler_run_aot_nodes
