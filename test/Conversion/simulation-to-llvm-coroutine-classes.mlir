// RUN: obelisk-opt %s --convert-obelisk-sim-processes-to-llvm-coroutines | FileCheck %s

module attributes {
  llvm.data_layout = "e-p:64:64-i64:64-i32:32-i16:16-i8:8",
  llvm.target_triple = "x86_64-unknown-linux-gnu"
} {
  obelisk_sim.design @classes {
    obelisk_sim.scope.decl 0 hierarchy "top"
    obelisk_sim.code_unit.decl 1 in 0 root_initializer hierarchy "__obelisk_root"
    obelisk_sim.code_unit.decl 2 in 0 function hierarchy "Base.get"
    obelisk_sim.code_unit.decl 3 in 0 function hierarchy "Base.tag"

    obelisk_sim.class.decl @I id 1 {
      is_abstract = true, is_final = false, is_interface = true
    }
    obelisk_sim.class.decl @J id 4 implements [@I] {
      is_abstract = true, is_final = false, is_interface = true
    }
    // Deliberately precede the base declaration. Native descriptor emission
    // must still flatten the base's managed trace slots into this layout.
    obelisk_sim.class.decl @Derived id 3 extends @Base implements [@J] {
      is_abstract = false, is_final = true, is_interface = false
    }
    obelisk_sim.class.decl @Base id 2 {
      is_abstract = false, is_final = false, is_interface = false
    }
    obelisk_sim.class.field @Base_value of @Base at 0 : i64 {
      is_static = false, is_weak = false
    }
    obelisk_sim.class.field @Base_next of @Base at 1 :
        !obelisk_sim.class_handle<@Base> {
      is_static = false, is_weak = false
    }
    obelisk_sim.class.method @I_get of @I slot 4294967295 signature_id 17
        interface_ordinal 0 :
      (!obelisk_sim.context, !obelisk_sim.class_handle<@I>) -> i64 {
        is_final = false, is_pure = true, is_static = false,
        is_task = false, is_virtual = true
      }
    obelisk_sim.class.method @J_tag of @J slot 4294967295 signature_id 18
        interface_ordinal 0 :
      (!obelisk_sim.context, !obelisk_sim.class_handle<@J>) -> i64 {
        is_final = false, is_pure = true, is_static = false,
        is_task = false, is_virtual = true
      }
    obelisk_sim.class.method @Base_get of @Base slot 0 signature_id 17 implemented_by @base_get :
      (!obelisk_sim.context, !obelisk_sim.class_handle<@Base>) -> i64 {
        is_final = false, is_pure = false, is_static = false,
        is_task = false, is_virtual = true
      }
    obelisk_sim.class.method @Base_tag of @Base slot 1 signature_id 18
        implemented_by @base_tag :
      (!obelisk_sim.context, !obelisk_sim.class_handle<@Base>) -> i64 {
        is_final = false, is_pure = false, is_static = false,
        is_task = false, is_virtual = true
      }

    obelisk_sim.func @base_get(
        %ctx: !obelisk_sim.context {obelisk_sim.capture_kind = 0 : i32},
        %this: !obelisk_sim.class_handle<@Base>
          {obelisk_sim.capture_kind = 1 : i32}) -> i64
        attributes {code_unit_id = 2 : i64, entry_kind = 8 : i32} {
      %field = obelisk_sim.class.field_ref %this[@Base_value] :
        !obelisk_sim.class_handle<@Base> ->
        !obelisk_sim.managed_ref<i64, @Base>
      %value = obelisk_sim.managed.load %field :
        !obelisk_sim.managed_ref<i64, @Base> -> i64
      obelisk_sim.return %value : i64
    }

    obelisk_sim.func @base_tag(
        %ctx: !obelisk_sim.context {obelisk_sim.capture_kind = 0 : i32},
        %this: !obelisk_sim.class_handle<@Base>
          {obelisk_sim.capture_kind = 1 : i32}) -> i64
        attributes {code_unit_id = 3 : i64, entry_kind = 8 : i32} {
      %zero = arith.constant 0 : i64
      obelisk_sim.return %zero : i64
    }

    obelisk_sim.func @root(
        %ctx: !obelisk_sim.context {obelisk_sim.capture_kind = 0 : i32})
        attributes {code_unit_id = 1 : i64, entry_kind = 0 : i32} {
      %null = obelisk_sim.class.null :
        !obelisk_sim.class_handle<@Derived>
      %object = obelisk_sim.class.alloc %ctx :
        !obelisk_sim.context -> !obelisk_sim.class_handle<@Derived>
      %copy = obelisk_sim.class.copy %ctx, %object :
        !obelisk_sim.context, !obelisk_sim.class_handle<@Derived> ->
        !obelisk_sim.class_handle<@Derived>
      %is_base = obelisk_sim.class.is_instance %object is @Base :
        !obelisk_sim.class_handle<@Derived>
      %base = obelisk_sim.class.cast %object :
        !obelisk_sim.class_handle<@Derived> to
        !obelisk_sim.class_handle<@Base>
      // The runtime copy preserves the dynamic Derived type even though this
      // source and result are statically Base handles.
      %polymorphic_copy = obelisk_sim.class.copy %ctx, %base :
        !obelisk_sim.context, !obelisk_sim.class_handle<@Base> ->
        !obelisk_sim.class_handle<@Base>
      %field = obelisk_sim.class.field_ref %base[@Base_value] :
        !obelisk_sim.class_handle<@Base> ->
        !obelisk_sim.managed_ref<i64, @Base>
      %one = arith.constant 1 : i64
      obelisk_sim.managed.store %one to %field :
        i64, !obelisk_sim.managed_ref<i64, @Base>
      %delay = obelisk_sim.time.constant 2
      obelisk_sim.managed.nba.enqueue %one to %field after %delay :
        (i64, !obelisk_sim.managed_ref<i64, @Base>, !obelisk_sim.time) -> ()
      %direct = obelisk_sim.class.direct_call @base_get %base() :
        (!obelisk_sim.class_handle<@Base>) -> i64
      %virtual = obelisk_sim.class.virtual_call
        %base[@Base_get] slot 0 signature_id 17() :
        (!obelisk_sim.class_handle<@Base>) -> i64
      %interface = obelisk_sim.class.cast %object :
        !obelisk_sim.class_handle<@Derived> to
        !obelisk_sim.class_handle<@I>
      %interface_virtual = obelisk_sim.class.virtual_call
        %interface[@I_get] slot 4294967295 signature_id 17() :
        (!obelisk_sim.class_handle<@I>) -> i64
      obelisk_sim.gc.safepoint %ctx : !obelisk_sim.context
      obelisk_sim.return
    }
  }
}


// CHECK-LABEL: llvm.mlir.global internal constant @Derived.__obelisk_class_descriptor
// CHECK-LABEL: llvm.mlir.global internal constant @Derived.__obelisk_interfaces
// CHECK: llvm.mlir.constant(1 : i64)
// CHECK: llvm.mlir.addressof @Derived.__obelisk_interface_0_slots
// CHECK: llvm.mlir.constant(4 : i64)
// CHECK: llvm.mlir.addressof @Derived.__obelisk_interface_1_slots
// CHECK-LABEL: llvm.mlir.global internal constant @Derived.__obelisk_interface_1_slots
// CHECK: llvm.mlir.constant(1 : i32)
// CHECK-LABEL: llvm.mlir.global internal constant @Derived.__obelisk_interface_0_slots
// CHECK: llvm.mlir.constant(0 : i32)
// CHECK-LABEL: llvm.mlir.global internal constant @Derived.__obelisk_trace_entries
// CHECK: llvm.mlir.constant(16 : i64)
// CHECK-LABEL: llvm.mlir.global internal constant @Base.__obelisk_class_descriptor
// CHECK-LABEL: llvm.func internal @Base_get.__obelisk_native_thunk
// CHECK-LABEL: llvm.func @root(
// CHECK: llvm.call @obelisk_rt_v1_object_allocate
// CHECK: llvm.call @obelisk_rt_v1_object_shallow_copy
// CHECK: llvm.call @obelisk_rt_v1_object_is_instance
// CHECK: llvm.call @obelisk_rt_v1_object_cast
// CHECK: llvm.call @obelisk_rt_v1_object_shallow_copy
// CHECK: llvm.call @obelisk_rt_v1_object_write
// CHECK: llvm.call @obelisk_rt_v1_scheduler_managed_nba
// CHECK: llvm.call @base_get
// CHECK: llvm.call @obelisk_rt_v1_method_invoke
// CHECK: llvm.call @obelisk_rt_v1_object_cast
// CHECK: %[[IID:.*]] = llvm.mlir.constant(1 : i64)
// CHECK: %[[ORDINAL:.*]] = llvm.mlir.constant(0 : i64)
// CHECK: %[[SIGNATURE:.*]] = llvm.mlir.constant(17 : i64)
// CHECK: llvm.call @obelisk_rt_v1_interface_method_invoke({{.*}}, {{.*}}, %[[IID]], %[[ORDINAL]], %[[SIGNATURE]],
// CHECK: llvm.call @obelisk_rt_v1_gc_safepoint
// CHECK-NOT: obelisk_sim.class
// CHECK-NOT: obelisk_sim.managed
