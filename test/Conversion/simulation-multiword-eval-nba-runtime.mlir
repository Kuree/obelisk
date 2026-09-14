// RUN: %python %S/Inputs/gen-multiword-ready-nba.py 65 --eval > %t.mlir
// RUN: obelisk-opt %t.mlir --pass-pipeline='builtin.module(obelisk_sim.design(obelisk_sim.func(obelisk-sim-thread-process-cfg),obelisk-sim-build-compute-graph,obelisk-sim-verify-compute-graph,obelisk-sim-materialize-graph-regions,obelisk-sim-materialize-compute-fusion,obelisk-sim-specialize-static-state-nba,obelisk-sim-plan-static-superstep),encode-obelisk-sim-to-bytecode{vpi=off},convert-obelisk-sim-processes-to-llvm-coroutines)' -o %t.llvm.mlir
// RUN: FileCheck %s --check-prefix=PLAN < %t.llvm.mlir
// RUN: %python %S/Inputs/check-promotion-word-scan.py %t.llvm.mlir %t.proof mlir-translate %llvm_dist/bin %native_support --multiword
// RUN: mlir-translate --mlir-to-llvmir %t.llvm.mlir | %llvm_dist/bin/opt -passes='coro-early,coro-split<reuse-storage>,coro-cleanup,default<O2>' | %llvm_dist/bin/llc -filetype=obj -relocation-model=pic -o %t.o
// RUN: %llvm_dist/bin/clang++ %t.o %native_support/libobelisk_rt.a %native_support/libc++.a %native_support/libc++abi.a %native_support/libunwind.a -nostdlib++ -lpthread -ldl -o %t.exe
// RUN: %t.exe --execution-tier=native | FileCheck %s
// RUN: %t.exe --execution-tier=bytecode | FileCheck %s

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
// PLAN-LABEL: llvm.func @__obelisk_eval_fast_coordinator_v1(
// PLAN: llvm.switch
// PLAN: 64: ^
// CHECK: 00000000000000000
// CHECK-NEXT: 1ffffffffffffffff
// CHECK-NEXT: 00000000000000000
