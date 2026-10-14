// RUN: obelisk-opt %s --test-obelisk-replicate --convert-obelisk-sim-processes-to-llvm-coroutines -o %t.mlir
// RUN: obelisk-opt %s --mlir-disable-threading --test-obelisk-replicate --convert-obelisk-sim-processes-to-llvm-coroutines | diff -u - %t.mlir
// RUN: FileCheck %s < %t.mlir
// RUN: FileCheck %s --check-prefix=CLOSED --implicit-check-not='llvm.func @child.__obelisk_spawn(' --implicit-check-not='llvm.func @parent_{{[0-9]+}}.__obelisk_spawn(' < %t.mlir
// RUN: mlir-translate --mlir-to-llvmir %t.mlir | opt -passes=verify -disable-output

// Thirty-two parents (parent_0 ... parent_31, from parent_#) each spawn the
// child twice. In a closed executable the parents spawn through process
// descriptors instead of per-process spawn functions; threaded and serial
// lowering must agree.
// CHECK-COUNT-33: llvm.call @obelisk_rt_v1_process_spawn_batch
// CHECK-NOT: llvm.call @obelisk_rt_v1_process_spawn_batch

// CLOSED-DAG: llvm.mlir.global external constant @child.__obelisk_process_descriptor
// CLOSED-DAG: llvm.mlir.global external constant @parent_0.__obelisk_process_descriptor
// CLOSED-DAG: llvm.mlir.global external constant @parent_31.__obelisk_process_descriptor
// CLOSED-LABEL: llvm.func @root.__obelisk_spawn(
// CLOSED: llvm.call @obelisk_rt_v1_process_spawn

module attributes {
  llvm.data_layout = "e-p:64:64-i64:64-i32:32-i16:16-i8:8",
  llvm.target_triple = "x86_64-unknown-linux-gnu",
  native.closed_executable
} {
  simulation.design @batches {
    simulation.scope.decl 0
    simulation.code_unit.decl 1 in 0 root_initializer hierarchy "root"
    simulation.code_unit.decl 2 in 0 initial hierarchy "child"
    simulation.code_unit.decl 3 in 0 initial hierarchy "parent_#" {test.replicate = 32 : i64, test.replicate_stride = {id = 1 : i64}}
    simulation.func @root(%ctx: !simulation.context {simulation.capture_kind = 0 : i32})
        attributes {entry_kind = 0 : i32, code_unit_id = 1 : i64} {
      %p = simulation.spawn @"parent_#"(%ctx) {test.replicate = 32 : i64} : !simulation.context -> !simulation.process
      simulation.return
    }
    simulation.func @"parent_#"(%ctx: !simulation.context {simulation.capture_kind = 0 : i32})
        attributes {entry_kind = 1 : i32, code_unit_id = 3 : i64, test.replicate = 32 : i64, test.replicate_stride = {code_unit_id = 1 : i64}} {
      %value = arith.constant {test.replicate_stride = {value = 1 : i64}} 0 : i64
      %a = simulation.spawn @child(%ctx, %value) : !simulation.context, i64 -> !simulation.process
      %b = simulation.spawn @child(%ctx, %value) : !simulation.context, i64 -> !simulation.process
      simulation.return
    }
    simulation.func @child(%ctx: !simulation.context {simulation.capture_kind = 0 : i32},
        %value: i64 {simulation.capture_kind = 2 : i32})
        attributes {entry_kind = 1 : i32, code_unit_id = 2 : i64} {
      simulation.return
    }
  }
}
