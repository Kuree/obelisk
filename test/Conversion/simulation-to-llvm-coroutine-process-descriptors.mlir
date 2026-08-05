// RUN: obelisk-opt %s --convert-obelisk-sim-processes-to-llvm-coroutines | FileCheck %s

module attributes {
  llvm.data_layout = "e-p:64:64-i64:64-i32:32-i16:16-i8:8",
  llvm.target_triple = "x86_64-unknown-linux-gnu"
} {
  obelisk_sim.design @process_descriptors {
    obelisk_sim.scope.decl 0
    obelisk_sim.code_unit.decl 42 in 0 initial hierarchy "process_descriptors.process"
    obelisk_sim.code_unit.decl 43 in 0 task hierarchy "process_descriptors.task"

    obelisk_sim.func @process(
        %ctx: !obelisk_sim.context
            {obelisk_sim.capture_kind = 0 : i32},
        %capture: i16 {obelisk_sim.capture_kind = 2 : i32})
        attributes {code_unit_id = 42 : i64, entry_kind = 1 : i32} {
      %delay = obelisk_sim.time.constant 1
      obelisk_sim.suspend.delay %delay to ^resume(%capture : i16)
    ^resume(%value: i16):
      obelisk_sim.return
    }

    obelisk_sim.func @task(
        %ctx: !obelisk_sim.context
            {obelisk_sim.capture_kind = 0 : i32})
        attributes {code_unit_id = 43 : i64, entry_kind = 12 : i32} {
      obelisk_sim.return
    }
  }
}

// CHECK: llvm.mlir.global external constant @process.__obelisk_process_descriptor
// CHECK-SAME: alignment = 8 : i64
// CHECK: llvm.mlir.constant(6 : i32)
// CHECK: llvm.mlir.constant(42 : i64)
// CHECK: llvm.mlir.addressof @process.__obelisk_frame_layout
// CHECK: llvm.mlir.addressof @process.__obelisk_native_requirements
// CHECK: llvm.mlir.addressof @process.__obelisk_native_execute
// CHECK: llvm.mlir.addressof @process.__obelisk_native_destroy
// CHECK: llvm.mlir.global internal constant @process.__obelisk_frame_layout
// CHECK-SAME: alignment = 8 : i64
// CHECK: llvm.mlir.constant(1 : i32)
// CHECK: llvm.mlir.addressof @process.__obelisk_frame_fields
// CHECK: llvm.mlir.addressof @process.__obelisk_continuations
// CHECK: llvm.mlir.global internal constant @process.__obelisk_continuations
// CHECK-SAME: alignment = 4 : i64
// CHECK: llvm.mlir.global internal constant @process.__obelisk_frame_fields
// CHECK-SAME: alignment = 8 : i64
// CHECK-LABEL: llvm.func @task.__obelisk_activate(
// CHECK-SAME: %[[TASK_CTX:.*]]: !llvm.ptr)
// CHECK: llvm.call @obelisk_rt_v1_process_instance_create_for_context(%[[TASK_CTX]], {{.*}}, {{.*}})
