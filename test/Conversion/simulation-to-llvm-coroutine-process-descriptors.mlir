// RUN: talos-opt %s --convert-talos-sim-processes-to-llvm-coroutines | FileCheck %s
// RUN: talos-opt %s --talos-sim-plan-native-partitions \
// RUN:   --convert-talos-sim-processes-to-llvm-coroutines \
// RUN:   | FileCheck %s --check-prefix=PARTITION

module attributes {
  llvm.data_layout = "e-p:64:64-i64:64-i32:32-i16:16-i8:8",
  llvm.target_triple = "x86_64-unknown-linux-gnu"
} {
  simulation.design @process_descriptors {
    simulation.scope.decl 0
    simulation.code_unit.decl 42 in 0 initial hierarchy "process_descriptors.process"
    simulation.code_unit.decl 43 in 0 task hierarchy "process_descriptors.task"

    simulation.func @process(
        %ctx: !simulation.context
            {simulation.capture_kind = 0 : i32},
        %capture: i16 {simulation.capture_kind = 2 : i32})
        attributes {code_unit_id = 42 : i64, entry_kind = 1 : i32} {
      %delay = simulation.time.constant 1
      simulation.suspend.delay %delay to ^resume(%capture : i16)
    ^resume(%value: i16):
      simulation.return
    }

    simulation.func @task(
        %ctx: !simulation.context
            {simulation.capture_kind = 0 : i32},
        %ref: !simulation.ref<i32>
            {simulation.capture_kind = 1 : i32})
        attributes {code_unit_id = 43 : i64, entry_kind = 12 : i32} {
      simulation.return
    }
  }
}

// CHECK: llvm.mlir.global external constant @process.__talos_process_descriptor
// CHECK-SAME: alignment = 8 : i64
// CHECK: llvm.mlir.constant(6 : i32)
// CHECK: llvm.mlir.constant(42 : i64)
// CHECK: llvm.mlir.addressof @process.__talos_frame_layout
// CHECK: llvm.mlir.addressof @process.__talos_native_requirements
// CHECK: llvm.mlir.addressof @process.__talos_native_execute

// PARTITION: module attributes {
// PARTITION-SAME: native.physical_partition_manifest = [
// PARTITION-SAME: #native.partition<id = "primary", members = [{{[^]]*}}], imports = [@process.__talos_native_execute, @process.__talos_native_requirements, @task.__talos_native_execute], exports = [@__talos_current_context, @__talos_native_coro_execute_v1, @process.__talos_process_descriptor, @task.__talos_process_descriptor], dependencies = ["unit:42", "unit:43"]
// PARTITION-SAME: #native.partition<id = "unit:42", members = [@process.__talos_coro_ramp, @process.__talos_native_execute, @process.__talos_native_requirements, @process.__talos_schedule_continuations, @process.__talos_schedule_ranks, @process.__talos_spawn, @process.__talos_spawn_plan], imports = [@__talos_native_coro_execute_v1, @process.__talos_process_descriptor], exports = [@process.__talos_native_execute, @process.__talos_native_requirements], dependencies = ["primary"]>
// PARTITION: llvm.mlir.global internal constant @process.__talos_schedule_ranks
// PARTITION-SAME: native.partition = "unit:42"
// PARTITION: llvm.mlir.global internal constant @process.__talos_schedule_continuations
// PARTITION-SAME: native.partition = "unit:42"
// PARTITION: llvm.func @process.__talos_coro_ramp
// PARTITION-SAME: native.partition = "unit:42"
// PARTITION: llvm.func @process.__talos_native_requirements
// PARTITION-SAME: native.partition = "unit:42"
// PARTITION: llvm.func @process.__talos_native_execute
// PARTITION-SAME: native.partition = "unit:42"
// PARTITION: llvm.func @process.__talos_spawn
// PARTITION-SAME: native.partition = "unit:42"
// PARTITION: llvm.func @task
// PARTITION-SAME: native.partition = "unit:43"
// PARTITION: llvm.func internal @task.__talos_activate_checked
// PARTITION-SAME: native.partition = "unit:43"
// PARTITION: llvm.func @task.__talos_activate
// PARTITION-SAME: native.partition = "unit:43"
// CHECK: llvm.mlir.addressof @__talos_native_coro_destroy_v1
// CHECK: llvm.mlir.global internal constant @process.__talos_frame_layout
// CHECK-SAME: alignment = 8 : i64
// CHECK: llvm.mlir.constant(1 : i32)
// CHECK: llvm.mlir.addressof @[[FIELDS:__talos_frame_fields_[0-9a-f]+]]
// CHECK: llvm.mlir.addressof @process.__talos_continuations
// CHECK: llvm.mlir.global internal constant @process.__talos_continuations
// CHECK-SAME: alignment = 4 : i64
// CHECK: llvm.mlir.global internal constant @[[FIELDS]](
// CHECK-SAME: alignment = 8 : i64
// CHECK-LABEL: llvm.func internal @task.__talos_activate_checked(
// CHECK-SAME: %[[TASK_CTX:.*]]: !llvm.ptr,
// CHECK: llvm.call @talos_rt_v1_process_instance_create_for_context(%[[TASK_CTX]], {{.*}}, {{.*}})
// CHECK-LABEL: llvm.func @task.__talos_activate(
// CHECK: llvm.call @task.__talos_activate_checked
// CHECK: llvm.call @talos_rt_v1_native_state_release
