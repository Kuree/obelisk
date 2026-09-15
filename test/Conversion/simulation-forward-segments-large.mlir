// RUN: %python %S/Inputs/mutate-forward-segments.py large %S/simulation-forward-segments.mlir > %t.input.mlir
// RUN: obelisk-opt %t.input.mlir --pass-pipeline='builtin.module(obelisk_sim.design(obelisk_sim.func(obelisk-sim-thread-process-cfg),obelisk-sim-build-compute-graph,obelisk-sim-verify-compute-graph,obelisk-sim-materialize-graph-regions,obelisk-sim-materialize-compute-fusion,obelisk-sim-specialize-static-state-nba,obelisk-sim-plan-static-superstep),encode-obelisk-sim-to-bytecode{vpi=off},convert-obelisk-sim-processes-to-llvm-coroutines)' -o %t.llvm.mlir
// RUN: FileCheck %s --check-prefix=PLAN < %t.llvm.mlir
// RUN: mlir-translate --mlir-to-llvmir %t.llvm.mlir | %llvm_dist/bin/opt -passes='coro-early,coro-split<reuse-storage>,coro-cleanup,default<O3>' | %llvm_dist/bin/llc -filetype=obj -relocation-model=pic -o %t.o
// RUN: %llvm_dist/bin/clang++ %t.o %native_support/libobelisk_rt.a %native_support/libc++.a %native_support/libc++abi.a %native_support/libunwind.a -nostdlib++ -lpthread -ldl -o %t.exe
// RUN: env OBELISK_RT_SIGNAL_DIAGNOSTICS=1 %t.exe --execution-tier=native 2> %t.native.err | FileCheck %s
// RUN: FileCheck %s --check-prefix=DIAG < %t.native.err
// RUN: obelisk-opt %t.input.mlir --pass-pipeline='builtin.module(obelisk_sim.design(obelisk_sim.func(obelisk-sim-thread-process-cfg),obelisk-sim-build-compute-graph,obelisk-sim-verify-compute-graph,obelisk-sim-materialize-graph-regions,obelisk-sim-materialize-compute-fusion,obelisk-sim-specialize-static-state-nba,obelisk-sim-plan-static-superstep),encode-obelisk-sim-to-bytecode{vpi=off require-bytecode=true},convert-obelisk-sim-processes-to-llvm-coroutines)' -o %t.bytecode.mlir
// RUN: mlir-translate --mlir-to-llvmir %t.bytecode.mlir | %llvm_dist/bin/opt -passes='coro-early,coro-split<reuse-storage>,coro-cleanup,default<O3>' | %llvm_dist/bin/llc -filetype=obj -relocation-model=pic -o %t.bytecode.o
// RUN: %llvm_dist/bin/clang++ %t.bytecode.o %native_support/libobelisk_rt.a %native_support/libc++.a %native_support/libc++abi.a %native_support/libunwind.a -nostdlib++ -lpthread -ldl -o %t.bytecode.exe
// RUN: %t.bytecode.exe --execution-tier=bytecode 2> %t.bytecode.err | FileCheck %s

// RUN: mlir-translate --mlir-to-llvmir %t.llvm.mlir | %llvm_dist/bin/opt -passes='coro-early,coro-split<reuse-storage>,coro-cleanup,default<O0>' | %llvm_dist/bin/llc -filetype=obj -relocation-model=pic -o %t.o0
// RUN: %llvm_dist/bin/clang++ %t.o0 %native_support/libobelisk_rt.a %native_support/libc++.a %native_support/libc++abi.a %native_support/libunwind.a -nostdlib++ -lpthread -ldl -o %t.o0.exe
// RUN: env OBELISK_RT_SIGNAL_DIAGNOSTICS=1 %t.o0.exe --execution-tier=native 2> %t.native.err | FileCheck %s
// RUN: FileCheck %s --check-prefix=DIAG < %t.native.err
// RUN: mlir-translate --mlir-to-llvmir %t.bytecode.mlir | %llvm_dist/bin/opt -passes='coro-early,coro-split<reuse-storage>,coro-cleanup,default<O0>' | %llvm_dist/bin/llc -filetype=obj -relocation-model=pic -o %t.bytecode.o0
// RUN: %llvm_dist/bin/clang++ %t.bytecode.o0 %native_support/libobelisk_rt.a %native_support/libc++.a %native_support/libc++abi.a %native_support/libunwind.a -nostdlib++ -lpthread -ldl -o %t.bytecode.o0.exe
// RUN: %t.bytecode.o0.exe --execution-tier=bytecode 2> %t.bytecode.err | FileCheck %s


// PLAN-LABEL: llvm.func @__obelisk_eval_ranked_group_0(
// PLAN-SAME: obelisk.eval.cache_hint_words = 2 : i64
// PLAN-SAME: obelisk.eval.predicated_dataflow
// PLAN-SAME: obelisk.eval.ranked_members = array<i32: {{.*}}66, 68, 69,{{.*}}>
// PLAN-LABEL: llvm.func @__obelisk_eval_ranked_group_0.fallback(
// PLAN-SAME: obelisk.eval.ssa_ready_words = 3 : i64
// CHECK: chain 71
// CHECK-NEXT: chain 70

// Only startup and the timed checker use node dispatch; clock work must
// execute through the native coordinator at both optimization levels.
// DIAG: aot_node_executions=75
// DIAG-SAME: aot_fallbacks=0 aot_checkpoints=0 aot_terminal_checkpoints=0
