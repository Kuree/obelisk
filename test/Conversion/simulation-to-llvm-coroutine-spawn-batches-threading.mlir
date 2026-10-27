// RUN: talos-opt %s --test-talos-replicate --convert-talos-sim-processes-to-llvm-coroutines -o %t.mlir
// RUN: talos-opt %s --mlir-disable-threading --test-talos-replicate --convert-talos-sim-processes-to-llvm-coroutines | diff -u - %t.mlir
// RUN: FileCheck %s --implicit-check-not=llvm.intr.coro --implicit-check-not=llvm.coro.destroy < %t.mlir
// RUN: FileCheck %s --check-prefix=OPEN < %t.mlir

// Thirty-two independent parents (parent_0 ... parent_31, from parent_#)
// exercise module-global creation across worker bodies; threaded and serial
// lowering must agree. The closed-executable form is in
// simulation-to-llvm-coroutine-spawn-batches-closed.mlir.
// CHECK-COUNT-33: llvm.call @talos_rt_v1_process_spawn_batch
// CHECK-NOT: llvm.call @talos_rt_v1_process_spawn_batch

// OPEN-DAG: llvm.func @child.__talos_spawn(
// OPEN-DAG: llvm.func @parent_0.__talos_spawn(

module attributes {
  llvm.data_layout = "e-p:64:64-i64:64-i32:32-i16:16-i8:8",
  llvm.target_triple = "x86_64-unknown-linux-gnu"
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
