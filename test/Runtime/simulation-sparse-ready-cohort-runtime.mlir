// RUN: %python %S/../Conversion/Inputs/gen-sparse-ready-cohort.py > %t.mlir
// RUN: obelisk-opt %t.mlir \
// RUN:   --pass-pipeline='builtin.module(obelisk_sim.design(obelisk_sim.func(obelisk-sim-thread-suspension),obelisk-sim-build-compute-graph,obelisk-sim-verify-compute-graph),convert-obelisk-sim-processes-to-llvm-coroutines)' \
// RUN:   | mlir-translate --mlir-to-llvmir \
// RUN:   | %llvm_dist/bin/opt -passes='coro-early,coro-split<reuse-storage>,coro-cleanup' \
// RUN:   | %llvm_dist/bin/llc -filetype=obj -relocation-model=pic -o %t.o
// RUN: %llvm_dist/bin/clang++ %t.o %native_support/libobelisk_rt.a \
// RUN:   %native_support/libc++.a %native_support/libc++abi.a \
// RUN:   %native_support/libunwind.a -nostdlib++ -lpthread -ldl -o %t.exe
// RUN: env OBELISK_RT_SIGNAL_DIAGNOSTICS=1 %t.exe --execution-tier=native > %t.log 2>&1
// RUN: %python %S/../Conversion/Inputs/gen-sparse-ready-cohort.py %t.log | FileCheck %s

// Reconstructing a small ready cohort must scale with that cohort, not all
// unrelated sleeping actors. Check deterministic scheduler work, not wall time.
// Every resubscription and publication must be retained.
// CHECK: sparse ready cohort: PASS
