// RUN: obelisk-opt %s --convert-obelisk-sim-processes-to-llvm-coroutines | FileCheck %s

module attributes {
  llvm.data_layout = "e-p:64:64-i64:64-i32:32-i16:16-i8:8",
  llvm.target_triple = "x86_64-unknown-linux-gnu"
} {
  simulation.design @virtual_tasks {
    simulation.scope.decl 0 hierarchy "top"
    simulation.code_unit.decl 1 in 0 initial hierarchy "top.caller"
    simulation.code_unit.decl 2 in 0 task hierarchy "Base.run"

    simulation.class.decl @Runner id 1 {
      is_abstract = true, is_final = false, is_interface = true
    }
    simulation.class.decl @Base id 2 implements [@Runner] {
      is_abstract = false, is_final = false, is_interface = false
    }
    simulation.class.method @Runner_run of @Runner slot 4294967295
      signature_id 17 interface_ordinal 0 :
      (!simulation.context, !simulation.class_handle<@Runner>, f32,
       !simulation.logic<8>, !simulation.class_handle<@Base>,
       !simulation.ref<i32>) -> () {
        is_final = false, is_pure = true, is_static = false,
        is_task = true, is_virtual = true
      }
    simulation.class.method @Base_run of @Base slot 0 signature_id 17
      implemented_by @base_run :
      (!simulation.context, !simulation.class_handle<@Base>, f32,
       !simulation.logic<8>, !simulation.class_handle<@Base>,
       !simulation.ref<i32>) -> () {
        is_final = false, is_pure = false, is_static = false,
        is_task = true, is_virtual = true
      }

    simulation.func @base_run(
        %ctx: !simulation.context {simulation.capture_kind = 0 : i32},
        %this: !simulation.class_handle<@Base>
          {simulation.capture_kind = 1 : i32},
        %short: f32 {simulation.capture_kind = 2 : i32},
        %logic: !simulation.logic<8>
          {simulation.capture_kind = 2 : i32},
        %managed: !simulation.class_handle<@Base>
          {simulation.capture_kind = 2 : i32},
        %reference: !simulation.ref<i32>
          {simulation.capture_kind = 2 : i32})
        attributes {code_unit_id = 2 : i64, entry_kind = 12 : i32} {
      simulation.return
    }

    simulation.func @caller(
        %ctx: !simulation.context {simulation.capture_kind = 0 : i32})
        attributes {code_unit_id = 1 : i64, entry_kind = 1 : i32} {
      %receiver = simulation.class.alloc %ctx :
        !simulation.context -> !simulation.class_handle<@Base>
      %interface = simulation.class.cast %receiver :
        !simulation.class_handle<@Base> to
        !simulation.class_handle<@Runner>
      %managed = simulation.class.alloc %ctx :
        !simulation.context -> !simulation.class_handle<@Base>
      %short = arith.constant 1.25 : f32
      %bits = arith.constant 42 : i8
      %logic = simulation.logic.from_bits %bits :
        i8 -> !simulation.logic<8>
      %initial = arith.constant 7 : i32
      %reference = simulation.ref.alloc %initial :
        i32 -> !simulation.ref<i32>
      simulation.class.virtual_task_call
        %interface[@Runner_run] slot 4294967295 signature_id 17
        (%short, %logic, %managed, %reference, %reference) arguments 4
        to ^done :
        (!simulation.class_handle<@Runner>, f32, !simulation.logic<8>,
         !simulation.class_handle<@Base>, !simulation.ref<i32>,
         !simulation.ref<i32>) -> ()
    ^done(%continued: !simulation.ref<i32>):
      simulation.return
    }
  }
}

// CHECK-LABEL: llvm.func internal @Base_run.__obelisk_native_thunk(
// CHECK-COUNT-5: llvm.icmp "ne"
// CHECK: llvm.icmp "eq"
// CHECK: llvm.call @base_run.__obelisk_activate_checked
// CHECK-NOT: llvm.mlir.constant(13 : i32)
// CHECK: llvm.return
// CHECK: llvm.func @obelisk_rt_v1_method_task_activate
// CHECK-LABEL: llvm.mlir.global internal constant @Base.__obelisk_interfaces
// CHECK: llvm.mlir.constant(1 : i64)
// CHECK: llvm.mlir.addressof @Base.__obelisk_interface_0_slots
// CHECK-LABEL: llvm.mlir.global internal constant @Base.__obelisk_interface_0_slots
// CHECK: llvm.mlir.constant(0 : i32)
// CHECK-LABEL: llvm.func internal @base_run.__obelisk_activate_checked(
// CHECK: llvm.call @obelisk_rt_v1_process_instance_create_for_context
// CHECK: llvm.store {{.*}} : i64, !llvm.ptr
// CHECK: llvm.return {{.*}} : i32
// CHECK-LABEL: llvm.func @base_run.__obelisk_activate(
// CHECK: llvm.call @base_run.__obelisk_activate_checked
// CHECK: llvm.call @obelisk_rt_v1_scheduler_fail
// CHECK-LABEL: llvm.func @caller.__obelisk_coro_ramp(
// CHECK-COUNT-2: llvm.call @obelisk_rt_v1_native_state_retain
// CHECK-NOT: llvm.alloca
// CHECK: %[[ROOT:.*]] = llvm.load {{.*}} : !llvm.ptr -> i64
// CHECK: llvm.store %[[ROOT]], {{.*}} : i64, !llvm.ptr
// CHECK-NOT: llvm.alloca
// CHECK: llvm.call @obelisk_rt_v1_gc_managed_root_range_push
// CHECK: llvm.mlir.constant(1 : i64)
// CHECK: llvm.mlir.constant(0 : i64)
// CHECK: %[[STATUS:.*]] = llvm.call @obelisk_rt_v1_interface_method_task_activate
// CHECK: llvm.call @obelisk_rt_v1_gc_managed_root_range_pop
// CHECK: llvm.cond_br {{.*}}, ^[[SUCCESS:bb[0-9]+]], ^[[FAILURE:bb[0-9]+]](%[[STATUS]] : i32)
// CHECK: ^[[SUCCESS]]:
// CHECK: llvm.mlir.constant(3 : i32)
// CHECK: llvm.intr.coro.suspend
// CHECK: ^[[FAILURE]]
// CHECK-COUNT-2: llvm.call @obelisk_rt_v1_native_state_release
// CHECK: llvm.call @obelisk_rt_v1_scheduler_fail
// CHECK-NOT: simulation.class.virtual_task_call
