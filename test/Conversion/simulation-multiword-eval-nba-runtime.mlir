// RUN: %python %S/Inputs/gen-multiword-ready-nba.py 65 --eval > %t.mlir
// RUN: obelisk-opt %t.mlir --pass-pipeline='builtin.module(simulation.design(simulation.func(obelisk-sim-thread-process-cfg),obelisk-sim-build-compute-graph,obelisk-sim-verify-compute-graph,obelisk-sim-materialize-graph-regions,obelisk-sim-materialize-compute-fusion,obelisk-sim-specialize-static-state-nba,obelisk-sim-plan-static-superstep),encode-obelisk-sim-to-bytecode{vpi=off},convert-obelisk-sim-processes-to-llvm-coroutines)' -o %t.llvm.mlir
// RUN: FileCheck %s --check-prefix=PLAN < %t.llvm.mlir

// Runtime behavior is checked in ../Runtime/simulation-multiword-eval-nba-runtime.test.

// LRM 4.5 / 4.6: generated Active work must drain across ALL owner words before
// NBA publication. Independent writers exercise the clock prefix, promotion,
// and a second activation after emptiness. The exact owners coexist with a
// complete clock-group owner; they must not invalidate its NBA proof.
// PLAN: llvm.mlir.global internal @__obelisk_aot_model_ingress_v1()
// PLAN-SAME: !llvm.array<3 x i64>
// PLAN: llvm.mlir.global internal @__obelisk_eval_promotion_pending_mask_v1()
// PLAN-SAME: !llvm.array<2 x i64>
// PLAN-LABEL: llvm.func @__obelisk_aot_schedule_run_v1(
// PLAN: llvm.call @obelisk_rt_v1_scheduler_prepare_periodic_aot
// PLAN-LABEL: llvm.func @__obelisk_eval_dispatch_v1(
// PLAN: llvm.switch
// PLAN: 64: ^
