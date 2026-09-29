// RUN: %python %S/Inputs/gen-spawn-batches.py > %t.mlir
// RUN: obelisk-opt %t.mlir --convert-obelisk-sim-processes-to-llvm-coroutines > %t.threaded
// RUN: obelisk-opt %t.mlir --mlir-disable-threading --convert-obelisk-sim-processes-to-llvm-coroutines > %t.serial
// RUN: diff -u %t.serial %t.threaded
// RUN: FileCheck %s --implicit-check-not=llvm.intr.coro --implicit-check-not=llvm.coro.destroy < %t.threaded
// RUN: FileCheck %s --check-prefix=OPEN < %t.threaded
// RUN: sed 's/module attributes {/module attributes {obelisk.native.closed_executable,/' %t.mlir > %t.closed.mlir
// RUN: obelisk-opt %t.closed.mlir --convert-obelisk-sim-processes-to-llvm-coroutines > %t.closed
// RUN: obelisk-opt %t.closed.mlir --mlir-disable-threading --convert-obelisk-sim-processes-to-llvm-coroutines > %t.closed.serial
// RUN: diff -u %t.closed.serial %t.closed
// RUN: FileCheck %s < %t.closed
// RUN: FileCheck %s --check-prefix=CLOSED --implicit-check-not='llvm.func @child.__obelisk_spawn(' --implicit-check-not='llvm.func @parent_{{[0-9]+}}.__obelisk_spawn(' < %t.closed
// RUN: mlir-translate --mlir-to-llvmir %t.closed | opt -passes=verify -disable-output

// CHECK-COUNT-33: llvm.call @obelisk_rt_v1_process_spawn_batch
// CHECK-NOT: llvm.call @obelisk_rt_v1_process_spawn_batch

// OPEN-DAG: llvm.func @child.__obelisk_spawn(
// OPEN-DAG: llvm.func @parent_0.__obelisk_spawn(

// CLOSED-DAG: llvm.mlir.global external constant @child.__obelisk_process_descriptor
// CLOSED-DAG: llvm.mlir.global external constant @parent_0.__obelisk_process_descriptor
// CLOSED-DAG: llvm.mlir.global external constant @parent_31.__obelisk_process_descriptor
// CLOSED-LABEL: llvm.func @root.__obelisk_spawn(
// CLOSED: llvm.call @obelisk_rt_v1_process_spawn
