// RUN: talos-opt %s --convert-talos-sim-processes-to-llvm-coroutines | FileCheck %s

// IEEE 1800-2023 18.14.1 seeds a static process from "the next value from the
// initialization RNG of the module instance ... containing the thread
// declaration". A binding around one spawn becomes that spawn-table row's
// scope; rows keep creation order, so every instance stream is consumed in
// the original order. A binding around nothing consumes no value and is
// dropped. A spawn the table cannot hold keeps explicit bindings.

module attributes {
  llvm.data_layout = "e-p:64:64-i64:64-i32:32-i16:16-i8:8",
  llvm.target_triple = "x86_64-unknown-linux-gnu"
} {
  simulation.design @scopes {
    simulation.scope.decl 0
    simulation.code_unit.decl 1 in 0 initial hierarchy "scopes.parent"
    simulation.code_unit.decl 2 in 0 initial hierarchy "scopes.child"
    simulation.func @parent(
        %ctx: !simulation.context {simulation.capture_kind = 0 : i32},
        %dynamic: i64 {simulation.capture_kind = 2 : i32})
        attributes {entry_kind = 1 : i32, code_unit_id = 1 : i64} {
      %a = arith.constant 17 : i64
      %b = arith.constant 29 : i64
      %e0 = simulation.random.initialization_enter %ctx {scope = 7 : i64} : (!simulation.context) -> i64
      %one = simulation.spawn @child(%ctx, %a) : !simulation.context, i64 -> !simulation.process
      simulation.random.initialization_leave %ctx, %e0 : (!simulation.context, i64) -> ()
      %e1 = simulation.random.initialization_enter %ctx {scope = 8 : i64} : (!simulation.context) -> i64
      simulation.random.initialization_leave %ctx, %e1 : (!simulation.context, i64) -> ()
      %two = simulation.spawn @child(%ctx, %b) : !simulation.context, i64 -> !simulation.process
      %e2 = simulation.random.initialization_enter %ctx {scope = 9 : i64} : (!simulation.context) -> i64
      %three = simulation.spawn @child(%ctx, %a) : !simulation.context, i64 -> !simulation.process
      simulation.random.initialization_leave %ctx, %e2 : (!simulation.context, i64) -> ()
      // A dynamic capture stays a typed marshalling call inside its binding.
      %e3 = simulation.random.initialization_enter %ctx {scope = 10 : i64} : (!simulation.context) -> i64
      %four = simulation.spawn @child(%ctx, %dynamic) : !simulation.context, i64 -> !simulation.process
      simulation.random.initialization_leave %ctx, %e3 : (!simulation.context, i64) -> ()
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

// Rows {plan, capture byte offset, scope}; equal captures share a slice.
// CHECK: llvm.mlir.global internal constant @parent.__talos_spawn_batch_0
// CHECK: llvm.mlir.addressof @child.__talos_spawn_plan
// CHECK: llvm.mlir.constant(0 : i64)
// CHECK: llvm.mlir.constant(7 : i64)
// CHECK: llvm.mlir.addressof @child.__talos_spawn_plan
// CHECK: llvm.mlir.constant(8 : i64)
// CHECK: llvm.mlir.constant(0 : i64)
// CHECK: llvm.mlir.addressof @child.__talos_spawn_plan
// CHECK: llvm.mlir.constant(0 : i64)
// CHECK: llvm.mlir.constant(9 : i64)
// CHECK: llvm.return %{{.*}} : !llvm.array<3 x struct<(ptr, i64, i64)>>
// CHECK: llvm.mlir.global internal constant @parent.__talos_spawn_batch_0_captures(dense<[17, 29]> : tensor<2xi64>) {{.*}} : !llvm.array<2 x i64>

// CHECK-LABEL: llvm.func @parent(
// CHECK-NOT: talos_rt_v1_random_initialization
// CHECK: %[[TABLE:.*]] = llvm.mlir.addressof @parent.__talos_spawn_batch_0
// CHECK: %[[CAPTURES:.*]] = llvm.mlir.addressof @parent.__talos_spawn_batch_0_captures
// CHECK: %[[COUNT:.*]] = llvm.mlir.constant(3 : i32)
// CHECK: %[[STATUS:.*]] = llvm.call @talos_rt_v1_process_spawn_batch(%{{.*}}, %[[TABLE]], %[[CAPTURES]], %[[COUNT]])
// CHECK: llvm.icmp "eq" %[[STATUS]]
// CHECK: %[[SCOPE:.*]] = llvm.mlir.constant(10 : i64)
// CHECK: llvm.call @talos_rt_v1_random_initialization_enter(%{{.*}}, %[[SCOPE]], %{{.*}})
// CHECK: llvm.call @child.__talos_spawn
// CHECK: llvm.call @talos_rt_v1_random_initialization_leave
// CHECK-NOT: talos_rt_v1_random_initialization
// CHECK: llvm.return
