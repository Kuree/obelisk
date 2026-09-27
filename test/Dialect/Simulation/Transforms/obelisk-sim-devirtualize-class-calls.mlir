// RUN: obelisk-opt %s --obelisk-sim-devirtualize-class-calls | FileCheck %s
// RUN: obelisk-opt %s --obelisk-sim-devirtualize-class-calls \
// RUN:   '--obelisk-sim-inline=opt-level=3' | FileCheck %s --check-prefix=INLINE

module {
  simulation.design @classes {
    simulation.scope.decl 0 hierarchy "top"
    simulation.code_unit.decl 1 in 0 root_initializer hierarchy "root"
    simulation.code_unit.decl 2 in 0 function hierarchy "Base.get"
    simulation.code_unit.decl 3 in 0 function hierarchy "Derived.get"
    simulation.code_unit.decl 4 in 0 function hierarchy "BadSignature.get"
    simulation.code_unit.decl 5 in 0 function hierarchy "InterfaceOrder.extra"
    simulation.class.decl @I id 1 {
      is_abstract = true, is_final = false, is_interface = true
    }
    simulation.class.decl @Base id 2 {
      is_abstract = false, is_final = false, is_interface = false
    }
    simulation.class.decl @Derived id 3 extends @Base implements [@I] {
      is_abstract = false, is_final = false, is_interface = false
    }
    simulation.class.decl @BadSignature id 4 extends @Base {
      is_abstract = false, is_final = false, is_interface = false
    }
    simulation.class.decl @PureShadow id 5 extends @Base {
      is_abstract = false, is_final = false, is_interface = false
    }
    simulation.class.decl @InterfaceOrder id 6 extends @Base implements [@I] {
      is_abstract = false, is_final = false, is_interface = false
    }

    simulation.class.method @I_get of @I slot 4294967295
        signature_id 17 interface_ordinal 0 :
      (!simulation.context, !simulation.class_handle<@I>) -> i64 {
        is_final = false, is_pure = true, is_static = false,
        is_task = false, is_virtual = true
      }
    simulation.class.method @Base_get of @Base slot 0 signature_id 17
        implemented_by @base_get :
      (!simulation.context, !simulation.class_handle<@Base>) -> i64 {
        is_final = false, is_pure = false, is_static = false,
        is_task = false, is_virtual = true
      }
    simulation.class.method @Derived_get of @Derived slot 0 signature_id 17
        implemented_by @derived_get :
      (!simulation.context, !simulation.class_handle<@Derived>) -> i64 {
        is_final = false, is_pure = false, is_static = false,
        is_task = false, is_virtual = true
      }
    simulation.class.method @BadSignature_get of @BadSignature slot 0
        signature_id 23 implemented_by @bad_signature_get :
      (!simulation.context,
       !simulation.class_handle<@BadSignature>) -> i64 {
        is_final = false, is_pure = false, is_static = false,
        is_task = false, is_virtual = true
      }
    simulation.class.method @PureShadow_get of @PureShadow slot 0
        signature_id 17 :
      (!simulation.context, !simulation.class_handle<@PureShadow>) -> i64 {
        is_final = false, is_pure = true, is_static = false,
        is_task = false, is_virtual = true
      }
    simulation.class.method @InterfaceOrder_extra of @InterfaceOrder slot 5
        signature_id 17 implemented_by @interface_order_extra :
      (!simulation.context,
       !simulation.class_handle<@InterfaceOrder>) -> i64 {
        is_final = false, is_pure = false, is_static = false,
        is_task = false, is_virtual = true
      }

    simulation.func private @base_get(
        %ctx: !simulation.context {simulation.capture_kind = 0 : i32},
        %this: !simulation.class_handle<@Base>
          {simulation.capture_kind = 1 : i32}) -> i64
        attributes {code_unit_id = 2 : i64, entry_kind = 8 : i32} {
      %value = arith.constant 1 : i64
      simulation.return %value : i64
    }
    simulation.func private @derived_get(
        %ctx: !simulation.context {simulation.capture_kind = 0 : i32},
        %this: !simulation.class_handle<@Derived>
          {simulation.capture_kind = 1 : i32}) -> i64
        attributes {code_unit_id = 3 : i64, entry_kind = 8 : i32} {
      %value = arith.constant 2 : i64
      simulation.return %value : i64
    }
    simulation.func private @bad_signature_get(
        %ctx: !simulation.context {simulation.capture_kind = 0 : i32},
        %this: !simulation.class_handle<@BadSignature>
          {simulation.capture_kind = 1 : i32}) -> i64
        attributes {code_unit_id = 4 : i64, entry_kind = 8 : i32} {
      %value = arith.constant 3 : i64
      simulation.return %value : i64
    }
    simulation.func private @interface_order_extra(
        %ctx: !simulation.context {simulation.capture_kind = 0 : i32},
        %this: !simulation.class_handle<@InterfaceOrder>
          {simulation.capture_kind = 1 : i32}) -> i64
        attributes {code_unit_id = 5 : i64, entry_kind = 8 : i32} {
      %value = arith.constant 5 : i64
      simulation.return %value : i64
    }

    simulation.func @root(
        %ctx: !simulation.context {simulation.capture_kind = 0 : i32})
        attributes {code_unit_id = 1 : i64, entry_kind = 0 : i32} {
      %object = simulation.class.alloc %ctx :
        !simulation.context -> !simulation.class_handle<@Derived>
      %base = simulation.class.cast %object :
        !simulation.class_handle<@Derived> to
        !simulation.class_handle<@Base>
      %exact = simulation.class.virtual_call
        %base[@Base_get] slot 0 signature_id 17() :
        (!simulation.class_handle<@Base>) -> i64

      %copy = simulation.class.copy %ctx, %base :
        !simulation.context, !simulation.class_handle<@Base> ->
        !simulation.class_handle<@Base>
      %copied = simulation.class.virtual_call
        %copy[@Base_get] slot 0 signature_id 17() :
        (!simulation.class_handle<@Base>) -> i64

      %interface = simulation.class.cast %object :
        !simulation.class_handle<@Derived> to
        !simulation.class_handle<@I>
      %through_interface = simulation.class.virtual_call
        %interface[@I_get] slot 4294967295 signature_id 17() :
        (!simulation.class_handle<@I>) -> i64

      %direct = simulation.class.direct_call @base_get %base() :
        (!simulation.class_handle<@Base>) -> i64

      %null = simulation.class.null :
        !simulation.class_handle<@Base>
      %null_call = simulation.class.virtual_call
        %null[@Base_get] slot 0 signature_id 17() :
        (!simulation.class_handle<@Base>) -> i64

      %bad_object = simulation.class.alloc %ctx :
        !simulation.context -> !simulation.class_handle<@BadSignature>
      %bad_base = simulation.class.cast %bad_object :
        !simulation.class_handle<@BadSignature> to
        !simulation.class_handle<@Base>
      %bad_signature = simulation.class.virtual_call
        %bad_base[@Base_get] slot 0 signature_id 17() :
        (!simulation.class_handle<@Base>) -> i64

      %pure_object = simulation.class.alloc %ctx :
        !simulation.context -> !simulation.class_handle<@PureShadow>
      %pure_base = simulation.class.cast %pure_object :
        !simulation.class_handle<@PureShadow> to
        !simulation.class_handle<@Base>
      %pure_shadow = simulation.class.virtual_call
        %pure_base[@Base_get] slot 0 signature_id 17() :
        (!simulation.class_handle<@Base>) -> i64

      %ordered_object = simulation.class.alloc %ctx :
        !simulation.context -> !simulation.class_handle<@InterfaceOrder>
      %ordered_interface = simulation.class.cast %ordered_object :
        !simulation.class_handle<@InterfaceOrder> to
        !simulation.class_handle<@I>
      %ordered = simulation.class.virtual_call
        %ordered_interface[@I_get] slot 4294967295 signature_id 17() :
        (!simulation.class_handle<@I>) -> i64

      %condition = arith.constant true
      cf.cond_br %condition, ^join(%base : !simulation.class_handle<@Base>),
                              ^join(%null : !simulation.class_handle<@Base>)
    ^join(%unknown: !simulation.class_handle<@Base>):
      %dynamic = simulation.class.virtual_call
        %unknown[@Base_get] slot 0 signature_id 17() :
        (!simulation.class_handle<@Base>) -> i64
      simulation.return
    }
  }
}

// CHECK-LABEL: simulation.func @root
// CHECK: %[[OBJECT:.*]] = simulation.class.alloc
// CHECK: %[[BASE:.*]] = simulation.class.cast %[[OBJECT]]
// CHECK: %[[EXACT_THIS:.*]] = simulation.class.cast %[[BASE]]
// CHECK-NEXT: simulation.call @derived_get(%{{.*}}, %[[EXACT_THIS]])
// CHECK: %[[COPY:.*]] = simulation.class.copy %{{.*}}, %[[BASE]]
// CHECK: %[[COPY_THIS:.*]] = simulation.class.cast %[[COPY]]
// CHECK-NEXT: simulation.call @derived_get(%{{.*}}, %[[COPY_THIS]])
// CHECK: %[[INTERFACE:.*]] = simulation.class.cast %[[OBJECT]]
// CHECK: %[[INTERFACE_THIS:.*]] = simulation.class.cast %[[INTERFACE]]
// CHECK-NEXT: simulation.call @derived_get(%{{.*}}, %[[INTERFACE_THIS]])
// CHECK: simulation.call @base_get
// CHECK: simulation.class.null
// CHECK-NEXT: simulation.class.virtual_call
// CHECK: simulation.class.alloc {{.*}}class_handle<@BadSignature>
// CHECK: simulation.class.virtual_call
// CHECK: simulation.class.alloc {{.*}}class_handle<@PureShadow>
// CHECK: simulation.class.virtual_call
// CHECK: simulation.class.alloc {{.*}}class_handle<@InterfaceOrder>
// CHECK: simulation.call @base_get
// CHECK: simulation.class.virtual_call

// INLINE-LABEL: simulation.func @root
// INLINE-COUNT-3: arith.constant 2 : i64
// INLINE: arith.constant 1 : i64
// INLINE: simulation.class.null
// INLINE-NEXT: simulation.class.virtual_call
// INLINE: simulation.class.alloc {{.*}}class_handle<@BadSignature>
// INLINE: simulation.class.virtual_call
// INLINE: simulation.class.alloc {{.*}}class_handle<@PureShadow>
// INLINE: simulation.class.virtual_call
// INLINE: simulation.class.alloc {{.*}}class_handle<@InterfaceOrder>
// INLINE: arith.constant 1 : i64
// INLINE: simulation.class.virtual_call
