// RUN: obelisk-opt %s --convert-obelisk-sim-processes-to-llvm-coroutines | FileCheck %s
// RUN: obelisk-opt %s --obelisk-sim-plan-native-partitions \
// RUN:   --convert-obelisk-sim-processes-to-llvm-coroutines \
// RUN:   | FileCheck %s --check-prefix=PARTITION

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
    obelisk_sim.class.field @Base_bits of @Base at 2 :
        !obelisk_sim.packed_array<32767 : 0 x i1> {
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
      %bits_field = obelisk_sim.class.field_ref %base[@Base_bits] :
        !obelisk_sim.class_handle<@Base> ->
        !obelisk_sim.managed_ref<
          !obelisk_sim.packed_array<32767 : 0 x i1>, @Base>
      %bits = obelisk_sim.managed.load %bits_field :
        !obelisk_sim.managed_ref<
          !obelisk_sim.packed_array<32767 : 0 x i1>, @Base> ->
        !obelisk_sim.packed_array<32767 : 0 x i1>
      %flat = obelisk_sim.packed.flatten %bits :
        (!obelisk_sim.packed_array<32767 : 0 x i1>) -> i32768
      %byte = arith.constant 42 : i8
      %low = arith.constant 17 : i66
      %inserted = obelisk_sim.bits.dyn_insert %byte into %flat at %low :
        (i32768, i8, i66) -> i32768
      %updated = obelisk_sim.packed.unflatten %inserted :
        (i32768) -> !obelisk_sim.packed_array<32767 : 0 x i1>
      obelisk_sim.managed.store %updated to %bits_field :
        !obelisk_sim.packed_array<32767 : 0 x i1>,
        !obelisk_sim.managed_ref<
          !obelisk_sim.packed_array<32767 : 0 x i1>, @Base>
      // Four-state indices retain the generic read/modify/write lowering so
      // an unknown index preserves the original field.
      %bits_again = obelisk_sim.managed.load %bits_field :
        !obelisk_sim.managed_ref<
          !obelisk_sim.packed_array<32767 : 0 x i1>, @Base> ->
        !obelisk_sim.packed_array<32767 : 0 x i1>
      %flat_again = obelisk_sim.packed.flatten %bits_again :
        (!obelisk_sim.packed_array<32767 : 0 x i1>) -> i32768
      %unknown_low = obelisk_sim.logic.constant 0 : i66, 1 : i66 :
        !obelisk_sim.logic<66>
      %unknown_inserted = obelisk_sim.bits.dyn_insert %byte into
        %flat_again at %unknown_low :
        (i32768, i8, !obelisk_sim.logic<66>) -> i32768
      %unknown_updated = obelisk_sim.packed.unflatten %unknown_inserted :
        (i32768) -> !obelisk_sim.packed_array<32767 : 0 x i1>
      obelisk_sim.managed.store %unknown_updated to %bits_field :
        !obelisk_sim.packed_array<32767 : 0 x i1>,
        !obelisk_sim.managed_ref<
          !obelisk_sim.packed_array<32767 : 0 x i1>, @Base>
      // An intervening heap effect prevents moving the read to an atomic RMW
      // at the final store.
      %bits_before_effect = obelisk_sim.managed.load %bits_field :
        !obelisk_sim.managed_ref<
          !obelisk_sim.packed_array<32767 : 0 x i1>, @Base> ->
        !obelisk_sim.packed_array<32767 : 0 x i1>
      %flat_before_effect = obelisk_sim.packed.flatten %bits_before_effect :
        (!obelisk_sim.packed_array<32767 : 0 x i1>) -> i32768
      obelisk_sim.managed.store %one to %field :
        i64, !obelisk_sim.managed_ref<i64, @Base>
      %inserted_after_effect = obelisk_sim.bits.dyn_insert %byte into
        %flat_before_effect at %low : (i32768, i8, i66) -> i32768
      %updated_after_effect = obelisk_sim.packed.unflatten
        %inserted_after_effect :
        (i32768) -> !obelisk_sim.packed_array<32767 : 0 x i1>
      obelisk_sim.managed.store %updated_after_effect to %bits_field :
        !obelisk_sim.packed_array<32767 : 0 x i1>,
        !obelisk_sim.managed_ref<
          !obelisk_sim.packed_array<32767 : 0 x i1>, @Base>
      // A pure but non-speculatable operation also preserves the original
      // read position and therefore blocks fusion.
      %bits_before_div = obelisk_sim.managed.load %bits_field :
        !obelisk_sim.managed_ref<
          !obelisk_sim.packed_array<32767 : 0 x i1>, @Base> ->
        !obelisk_sim.packed_array<32767 : 0 x i1>
      %flat_before_div = obelisk_sim.packed.flatten %bits_before_div :
        (!obelisk_sim.packed_array<32767 : 0 x i1>) -> i32768
      %zero_byte = arith.constant 0 : i8
      %unsafe_div = arith.divui %byte, %zero_byte : i8
      %inserted_after_div = obelisk_sim.bits.dyn_insert %unsafe_div into
        %flat_before_div at %low : (i32768, i8, i66) -> i32768
      %updated_after_div = obelisk_sim.packed.unflatten %inserted_after_div :
        (i32768) -> !obelisk_sim.packed_array<32767 : 0 x i1>
      obelisk_sim.managed.store %updated_after_div to %bits_field :
        !obelisk_sim.packed_array<32767 : 0 x i1>,
        !obelisk_sim.managed_ref<
          !obelisk_sim.packed_array<32767 : 0 x i1>, @Base>
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


// CHECK: llvm.func @obelisk_rt_v1_object_bits_insert(!llvm.ptr, i64, i64, i64, i32, i64, i32) -> i32
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
// PARTITION: module attributes {
// PARTITION-SAME: obelisk.native.physical_partition_manifest = [
// PARTITION-SAME: id = "unit:2"
// PARTITION-SAME: members = [@Base_get.__obelisk_native_thunk, @base_get]
// PARTITION-SAME: id = "unit:3"
// PARTITION-SAME: members = [@Base_tag.__obelisk_native_thunk, @base_tag]
// PARTITION: llvm.func internal @Base_get.__obelisk_native_thunk
// PARTITION-SAME: obelisk.native.partition = "unit:2"
// PARTITION: llvm.func internal @Base_tag.__obelisk_native_thunk
// PARTITION-SAME: obelisk.native.partition = "unit:3"
// CHECK-LABEL: llvm.func @root(
// CHECK: llvm.call @obelisk_rt_v1_object_allocate
// CHECK: llvm.call @obelisk_rt_v1_object_shallow_copy
// CHECK: llvm.call @obelisk_rt_v1_object_is_instance
// CHECK: llvm.call @obelisk_rt_v1_object_cast
// CHECK: llvm.call @obelisk_rt_v1_object_shallow_copy
// CHECK: llvm.call @obelisk_rt_v1_object_write
// CHECK-NOT: llvm.shl {{.*}} : i327
// CHECK: %[[BIT_OFFSET:.*]] = llvm.mlir.constant(24 : i64) : i64
// CHECK: %[[LOW:.*]] = llvm.mlir.constant(17 : i64) : i64
// CHECK: %[[REPLACEMENT:.*]] = llvm.mlir.constant(42 : i64) : i64
// CHECK: %[[VALID:.*]] = llvm.zext {{.*}} : i1 to i32
// CHECK: %[[OBJECT:.*]] = llvm.inttoptr {{.*}} : i64 to !llvm.ptr
// CHECK: %[[FIELD_WIDTH:.*]] = llvm.mlir.constant(32768 : i64) : i64
// CHECK: %[[REPLACEMENT_WIDTH:.*]] = llvm.mlir.constant(8 : i32) : i32
// CHECK: llvm.call @obelisk_rt_v1_object_bits_insert(%[[OBJECT]], %[[BIT_OFFSET]], %[[FIELD_WIDTH]], %[[LOW]], %[[VALID]], %[[REPLACEMENT]], %[[REPLACEMENT_WIDTH]])
// CHECK-NOT: llvm.call @obelisk_rt_v1_object_bits_insert
// CHECK: llvm.call @obelisk_rt_v1_object_read
// CHECK: llvm.call @obelisk_rt_v1_object_write
// CHECK: llvm.call @obelisk_rt_v1_object_read
// CHECK: llvm.call @obelisk_rt_v1_object_write
// CHECK: llvm.call @obelisk_rt_v1_object_write
// CHECK: llvm.call @obelisk_rt_v1_object_read
// CHECK: llvm.udiv
// CHECK: llvm.call @obelisk_rt_v1_object_write
// CHECK-NOT: llvm.call @obelisk_rt_v1_object_bits_insert
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
