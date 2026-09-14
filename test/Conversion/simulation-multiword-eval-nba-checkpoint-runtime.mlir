// RUN: %python %S/Inputs/gen-multiword-ready-nba.py 65 --eval --forwarded > %t.mlir
// RUN: obelisk-opt %t.mlir --pass-pipeline='builtin.module(obelisk_sim.design(obelisk_sim.func(obelisk-sim-thread-process-cfg),obelisk-sim-build-compute-graph,obelisk-sim-verify-compute-graph,obelisk-sim-materialize-graph-regions,obelisk-sim-materialize-compute-fusion,obelisk-sim-specialize-static-state-nba,obelisk-sim-plan-static-superstep),encode-obelisk-sim-to-bytecode{vpi=off},convert-obelisk-sim-processes-to-llvm-coroutines)' -o %t.llvm.mlir
// RUN: FileCheck %s --check-prefix=PLAN < %t.llvm.mlir
// RUN: mlir-translate --mlir-to-llvmir %t.llvm.mlir | %llvm_dist/bin/opt -passes='coro-early,coro-split<reuse-storage>,coro-cleanup,default<O2>' | %llvm_dist/bin/llc -filetype=obj -relocation-model=pic -o %t.o
// RUN: %llvm_dist/bin/clang++ %t.o %native_support/libobelisk_rt.a %native_support/libc++.a %native_support/libc++abi.a %native_support/libunwind.a -nostdlib++ -lpthread -ldl -o %t.exe
// RUN: %t.exe --execution-tier=native | FileCheck %s
// RUN: %t.exe --execution-tier=bytecode | FileCheck %s

// The copied clock is not certified periodic. Its wide NBA owners use ordered
// generated staging, not one-entry latches or actor checkpoints. All 65 writes
// survive queue growth and subsequent reuse after the first NBA barrier.
// PLAN-NOT: .ordered_nba_checkpoint
// PLAN: llvm.mlir.global internal @__obelisk_eval_ordered_nba_queue_v1
// PLAN: llvm.func @__obelisk_eval_fast_coordinator_v1
// CHECK: 00000000000000000
// CHECK-NEXT: 1ffffffffffffffff
// CHECK-NEXT: 00000000000000000
