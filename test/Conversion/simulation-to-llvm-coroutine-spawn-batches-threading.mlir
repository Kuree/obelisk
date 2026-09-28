// RUN: %python %S/Inputs/gen-spawn-batches.py > %t.mlir
// RUN: obelisk-opt %t.mlir --convert-obelisk-sim-processes-to-llvm-coroutines > %t.threaded
// RUN: obelisk-opt %t.mlir --mlir-disable-threading --convert-obelisk-sim-processes-to-llvm-coroutines > %t.serial
// RUN: diff -u %t.serial %t.threaded
// RUN: FileCheck %s --implicit-check-not=llvm.intr.coro --implicit-check-not=llvm.coro.destroy < %t.threaded

// CHECK-COUNT-33: llvm.call @obelisk_rt_v1_process_spawn_batch
// CHECK-NOT: llvm.call @obelisk_rt_v1_process_spawn_batch
