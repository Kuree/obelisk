// RUN: obelisk-opt %s --convert-obelisk-sim-processes-to-llvm-coroutines | FileCheck %s
// RUN: obelisk-opt %s --obelisk-sim-plan-native-partitions --convert-obelisk-sim-processes-to-llvm-coroutines | mlir-translate --mlir-to-llvmir | opt -passes=verify -disable-output
// RUN: sed 's/module attributes {/module attributes {obelisk.native.closed_executable,/' %s > %t.closed.mlir
// RUN: obelisk-opt %t.closed.mlir --convert-obelisk-sim-processes-to-llvm-coroutines | FileCheck %s

module attributes {
  llvm.data_layout = "e-p:64:64-i64:64-i32:32-i16:16-i8:8",
  llvm.target_triple = "x86_64-unknown-linux-gnu"
} {
  simulation.design @spawn_table {
    simulation.scope.decl 0
    simulation.code_unit.decl 1 in 0 initial hierarchy "spawn_table.parent"
    simulation.code_unit.decl 2 in 0 initial hierarchy "spawn_table.child"
    simulation.func @parent(
        %ctx: !simulation.context {simulation.capture_kind = 0 : i32},
        %dynamic: i64 {simulation.capture_kind = 2 : i32})
        attributes {entry_kind = 1 : i32, code_unit_id = 1 : i64} {
      %a = arith.constant 17 : i64
      %b = arith.constant 29 : i64
      %one = simulation.spawn @child(%ctx, %a) : !simulation.context, i64 -> !simulation.process
      %two = simulation.spawn @child(%ctx, %b) : !simulation.context, i64 -> !simulation.process
      // A side effect divides the constant batches.
      simulation.dump.flush %ctx : (!simulation.context) -> ()
      %three = simulation.spawn @child(%ctx, %b) : !simulation.context, i64 -> !simulation.process
      // A dynamic capture stays a typed marshalling call.
      %four = simulation.spawn @child(%ctx, %dynamic) : !simulation.context, i64 -> !simulation.process
      %five = simulation.spawn @child(%ctx, %a) : !simulation.context, i64 -> !simulation.process
      // A used process identity must not be discarded into a batch.
      %six = simulation.spawn @child(%ctx, %b) : !simulation.context, i64 -> !simulation.process
      simulation.suspend.await %six to ^done
    ^done:
      simulation.return
    }
    simulation.func @child(
        %ctx: !simulation.context {simulation.capture_kind = 0 : i32},
        %capture: i64 {simulation.capture_kind = 2 : i32})
        attributes {entry_kind = 1 : i32, code_unit_id = 2 : i64} {
      simulation.return
    }
  }
}

// CHECK: llvm.mlir.global internal constant @parent.__obelisk_coro_ramp.__obelisk_spawn_batch_0
// CHECK: llvm.mlir.addressof @child.__obelisk_spawn_plan
// CHECK: llvm.mlir.addressof @parent.__obelisk_coro_ramp.__obelisk_spawn_batch_0_captures_0
// CHECK: llvm.mlir.addressof @child.__obelisk_spawn_plan
// CHECK: llvm.mlir.addressof @parent.__obelisk_coro_ramp.__obelisk_spawn_batch_0_captures_1
// CHECK: llvm.mlir.global internal constant @parent.__obelisk_coro_ramp.__obelisk_spawn_batch_0_captures_1
// CHECK: llvm.mlir.constant(29 : i64)
// CHECK: llvm.mlir.global internal constant @parent.__obelisk_coro_ramp.__obelisk_spawn_batch_0_captures_0
// CHECK: llvm.mlir.constant(17 : i64)
// CHECK-LABEL: llvm.func @parent.__obelisk_coro_ramp
// CHECK: llvm.call @obelisk_rt_v1_process_spawn_batch
// CHECK-NOT: llvm.call @child.__obelisk_spawn
// CHECK: llvm.call @obelisk_rt_v1_dump_flush
// CHECK-COUNT-4: llvm.call @child.__obelisk_spawn
// CHECK-LABEL: llvm.func @child.__obelisk_spawn(
// CHECK: llvm.alloca
// CHECK: llvm.intr.memset
// CHECK: llvm.store
// CHECK: llvm.mlir.addressof @child.__obelisk_spawn_plan
// CHECK: llvm.call @obelisk_rt_v1_process_spawn
// CHECK-NOT: llvm.call @obelisk_rt_v1_scheduler_add
// CHECK: llvm.return
