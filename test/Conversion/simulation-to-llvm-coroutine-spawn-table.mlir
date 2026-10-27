// RUN: talos-opt %s --convert-talos-sim-processes-to-llvm-coroutines | FileCheck %s
// RUN: talos-opt %s --talos-sim-plan-native-partitions --convert-talos-sim-processes-to-llvm-coroutines | talos-translate --mlir-to-llvmir | opt -passes=verify -disable-output
// RUN: talos-opt %s '--test-talos-set-module-attributes=attrs="{native.closed_executable}"' > %t.closed.mlir
// RUN: talos-opt %t.closed.mlir --convert-talos-sim-processes-to-llvm-coroutines | FileCheck %s

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

// Rows {plan, capture byte offset, no initialization scope}.
// CHECK: llvm.mlir.global internal constant @parent.__talos_coro_ramp.__talos_spawn_batch_0() {{.*}} : !llvm.array<2 x struct<(ptr, i64, i64)>>
// CHECK: llvm.mlir.addressof @child.__talos_spawn_plan
// CHECK: llvm.mlir.constant(0 : i64)
// CHECK: llvm.mlir.constant(0 : i64)
// CHECK: llvm.mlir.addressof @child.__talos_spawn_plan
// CHECK: llvm.mlir.constant(8 : i64)
// CHECK: llvm.mlir.constant(0 : i64)
// CHECK: llvm.mlir.global internal constant @parent.__talos_coro_ramp.__talos_spawn_batch_0_captures(dense<[17, 29]> : tensor<2xi64>) {{.*}} : !llvm.array<2 x i64>
// CHECK-LABEL: llvm.func @parent.__talos_coro_ramp
// CHECK: %[[TABLE:.*]] = llvm.mlir.addressof @parent.__talos_coro_ramp.__talos_spawn_batch_0 :
// CHECK: %[[CAPTURES:.*]] = llvm.mlir.addressof @parent.__talos_coro_ramp.__talos_spawn_batch_0_captures
// CHECK: llvm.call @talos_rt_v1_process_spawn_batch(%{{.*}}, %[[TABLE]], %[[CAPTURES]], %{{.*}})
// CHECK-NOT: llvm.icmp
// CHECK-NOT: llvm.call @child.__talos_spawn
// CHECK: llvm.call @talos_rt_v1_dump_flush
// CHECK-COUNT-4: llvm.call @child.__talos_spawn
// CHECK-LABEL: llvm.func @child.__talos_spawn(
// CHECK: llvm.alloca
// CHECK: llvm.intr.memset
// CHECK: llvm.store
// CHECK: llvm.mlir.addressof @child.__talos_spawn_plan
// CHECK: llvm.call @talos_rt_v1_process_spawn
// CHECK-NOT: llvm.call @talos_rt_v1_scheduler_add
// CHECK: llvm.return
