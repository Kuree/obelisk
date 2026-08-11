// RUN: obelisk-opt %s --convert-obelisk-sim-processes-to-llvm-coroutines | FileCheck %s

module attributes {
  llvm.data_layout = "e-p:64:64-i64:64-i32:32-i16:16-i8:8",
  llvm.target_triple = "x86_64-unknown-linux-gnu"
} {
  obelisk_sim.design @virtual_tasks {
    obelisk_sim.scope.decl 0 hierarchy "top"
    obelisk_sim.code_unit.decl 1 in 0 initial hierarchy "top.caller"
    obelisk_sim.code_unit.decl 2 in 0 task hierarchy "Base.run"

    obelisk_sim.class.decl @Runner id 1 {
      is_abstract = true, is_final = false, is_interface = true
    }
    obelisk_sim.class.decl @Base id 2 implements [@Runner] {
      is_abstract = false, is_final = false, is_interface = false
    }
    obelisk_sim.class.method @Runner_run of @Runner slot 4294967295
      signature_id 17 interface_ordinal 0 :
      (!obelisk_sim.context, !obelisk_sim.class_handle<@Runner>, f32,
       !obelisk_sim.logic<8>, !obelisk_sim.class_handle<@Base>,
       !obelisk_sim.ref<i32>) -> () {
        is_final = false, is_pure = true, is_static = false,
        is_task = true, is_virtual = true
      }
    obelisk_sim.class.method @Base_run of @Base slot 0 signature_id 17
      implemented_by @base_run :
      (!obelisk_sim.context, !obelisk_sim.class_handle<@Base>, f32,
       !obelisk_sim.logic<8>, !obelisk_sim.class_handle<@Base>,
       !obelisk_sim.ref<i32>) -> () {
        is_final = false, is_pure = false, is_static = false,
        is_task = true, is_virtual = true
      }

    obelisk_sim.func @base_run(
        %ctx: !obelisk_sim.context {obelisk_sim.capture_kind = 0 : i32},
        %this: !obelisk_sim.class_handle<@Base>
          {obelisk_sim.capture_kind = 1 : i32},
        %short: f32 {obelisk_sim.capture_kind = 2 : i32},
        %logic: !obelisk_sim.logic<8>
          {obelisk_sim.capture_kind = 2 : i32},
        %managed: !obelisk_sim.class_handle<@Base>
          {obelisk_sim.capture_kind = 2 : i32},
        %reference: !obelisk_sim.ref<i32>
          {obelisk_sim.capture_kind = 2 : i32})
        attributes {code_unit_id = 2 : i64, entry_kind = 12 : i32} {
      obelisk_sim.return
    }

    obelisk_sim.func @caller(
        %ctx: !obelisk_sim.context {obelisk_sim.capture_kind = 0 : i32})
        attributes {code_unit_id = 1 : i64, entry_kind = 1 : i32} {
      %receiver = obelisk_sim.class.alloc %ctx :
        !obelisk_sim.context -> !obelisk_sim.class_handle<@Base>
      %interface = obelisk_sim.class.cast %receiver :
        !obelisk_sim.class_handle<@Base> to
        !obelisk_sim.class_handle<@Runner>
      %managed = obelisk_sim.class.alloc %ctx :
        !obelisk_sim.context -> !obelisk_sim.class_handle<@Base>
      %short = arith.constant 1.25 : f32
      %bits = arith.constant 42 : i8
      %logic = obelisk_sim.logic.from_bits %bits :
        i8 -> !obelisk_sim.logic<8>
      %initial = arith.constant 7 : i32
      %reference = obelisk_sim.ref.alloc %initial :
        i32 -> !obelisk_sim.ref<i32>
      obelisk_sim.class.virtual_task_call
        %interface[@Runner_run] slot 4294967295 signature_id 17
        (%short, %logic, %managed, %reference, %reference) arguments 4
        to ^done :
        (!obelisk_sim.class_handle<@Runner>, f32, !obelisk_sim.logic<8>,
         !obelisk_sim.class_handle<@Base>, !obelisk_sim.ref<i32>,
         !obelisk_sim.ref<i32>) -> ()
    ^done(%continued: !obelisk_sim.ref<i32>):
      obelisk_sim.return
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
// CHECK-NOT: obelisk_sim.class.virtual_task_call
