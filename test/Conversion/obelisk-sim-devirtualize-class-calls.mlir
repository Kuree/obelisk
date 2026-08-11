// RUN: obelisk-opt %s --obelisk-sim-devirtualize-class-calls | FileCheck %s
// RUN: obelisk-opt %s --obelisk-sim-devirtualize-class-calls \
// RUN:   '--obelisk-sim-inline=opt-level=3' | FileCheck %s --check-prefix=INLINE

module {
  obelisk_sim.design @classes {
    obelisk_sim.scope.decl 0 hierarchy "top"
    obelisk_sim.code_unit.decl 1 in 0 root_initializer hierarchy "root"
    obelisk_sim.code_unit.decl 2 in 0 function hierarchy "Base.get"
    obelisk_sim.code_unit.decl 3 in 0 function hierarchy "Derived.get"
    obelisk_sim.code_unit.decl 4 in 0 function hierarchy "BadSignature.get"
    obelisk_sim.code_unit.decl 5 in 0 function hierarchy "InterfaceOrder.extra"
    obelisk_sim.class.decl @I id 1 {
      is_abstract = true, is_final = false, is_interface = true
    }
    obelisk_sim.class.decl @Base id 2 {
      is_abstract = false, is_final = false, is_interface = false
    }
    obelisk_sim.class.decl @Derived id 3 extends @Base implements [@I] {
      is_abstract = false, is_final = false, is_interface = false
    }
    obelisk_sim.class.decl @BadSignature id 4 extends @Base {
      is_abstract = false, is_final = false, is_interface = false
    }
    obelisk_sim.class.decl @PureShadow id 5 extends @Base {
      is_abstract = false, is_final = false, is_interface = false
    }
    obelisk_sim.class.decl @InterfaceOrder id 6 extends @Base implements [@I] {
      is_abstract = false, is_final = false, is_interface = false
    }

    obelisk_sim.class.method @I_get of @I slot 4294967295
        signature_id 17 :
      (!obelisk_sim.context, !obelisk_sim.class_handle<@I>) -> i64 {
        is_final = false, is_pure = true, is_static = false,
        is_task = false, is_virtual = true
      }
    obelisk_sim.class.method @Base_get of @Base slot 0 signature_id 17
        implemented_by @base_get :
      (!obelisk_sim.context, !obelisk_sim.class_handle<@Base>) -> i64 {
        is_final = false, is_pure = false, is_static = false,
        is_task = false, is_virtual = true
      }
    obelisk_sim.class.method @Derived_get of @Derived slot 0 signature_id 17
        implemented_by @derived_get :
      (!obelisk_sim.context, !obelisk_sim.class_handle<@Derived>) -> i64 {
        is_final = false, is_pure = false, is_static = false,
        is_task = false, is_virtual = true
      }
    obelisk_sim.class.method @BadSignature_get of @BadSignature slot 0
        signature_id 23 implemented_by @bad_signature_get :
      (!obelisk_sim.context,
       !obelisk_sim.class_handle<@BadSignature>) -> i64 {
        is_final = false, is_pure = false, is_static = false,
        is_task = false, is_virtual = true
      }
    obelisk_sim.class.method @PureShadow_get of @PureShadow slot 0
        signature_id 17 :
      (!obelisk_sim.context, !obelisk_sim.class_handle<@PureShadow>) -> i64 {
        is_final = false, is_pure = true, is_static = false,
        is_task = false, is_virtual = true
      }
    obelisk_sim.class.method @InterfaceOrder_extra of @InterfaceOrder slot 5
        signature_id 17 implemented_by @interface_order_extra :
      (!obelisk_sim.context,
       !obelisk_sim.class_handle<@InterfaceOrder>) -> i64 {
        is_final = false, is_pure = false, is_static = false,
        is_task = false, is_virtual = true
      }

    obelisk_sim.func private @base_get(
        %ctx: !obelisk_sim.context {obelisk_sim.capture_kind = 0 : i32},
        %this: !obelisk_sim.class_handle<@Base>
          {obelisk_sim.capture_kind = 1 : i32}) -> i64
        attributes {code_unit_id = 2 : i64, entry_kind = 8 : i32} {
      %value = arith.constant 1 : i64
      obelisk_sim.return %value : i64
    }
    obelisk_sim.func private @derived_get(
        %ctx: !obelisk_sim.context {obelisk_sim.capture_kind = 0 : i32},
        %this: !obelisk_sim.class_handle<@Derived>
          {obelisk_sim.capture_kind = 1 : i32}) -> i64
        attributes {code_unit_id = 3 : i64, entry_kind = 8 : i32} {
      %value = arith.constant 2 : i64
      obelisk_sim.return %value : i64
    }
    obelisk_sim.func private @bad_signature_get(
        %ctx: !obelisk_sim.context {obelisk_sim.capture_kind = 0 : i32},
        %this: !obelisk_sim.class_handle<@BadSignature>
          {obelisk_sim.capture_kind = 1 : i32}) -> i64
        attributes {code_unit_id = 4 : i64, entry_kind = 8 : i32} {
      %value = arith.constant 3 : i64
      obelisk_sim.return %value : i64
    }
    obelisk_sim.func private @interface_order_extra(
        %ctx: !obelisk_sim.context {obelisk_sim.capture_kind = 0 : i32},
        %this: !obelisk_sim.class_handle<@InterfaceOrder>
          {obelisk_sim.capture_kind = 1 : i32}) -> i64
        attributes {code_unit_id = 5 : i64, entry_kind = 8 : i32} {
      %value = arith.constant 5 : i64
      obelisk_sim.return %value : i64
    }

    obelisk_sim.func @root(
        %ctx: !obelisk_sim.context {obelisk_sim.capture_kind = 0 : i32})
        attributes {code_unit_id = 1 : i64, entry_kind = 0 : i32} {
      %object = obelisk_sim.class.alloc %ctx :
        !obelisk_sim.context -> !obelisk_sim.class_handle<@Derived>
      %base = obelisk_sim.class.cast %object :
        !obelisk_sim.class_handle<@Derived> to
        !obelisk_sim.class_handle<@Base>
      %exact = obelisk_sim.class.virtual_call
        %base[@Base_get] slot 0 signature_id 17() :
        (!obelisk_sim.class_handle<@Base>) -> i64

      %copy = obelisk_sim.class.copy %ctx, %base :
        !obelisk_sim.context, !obelisk_sim.class_handle<@Base> ->
        !obelisk_sim.class_handle<@Base>
      %copied = obelisk_sim.class.virtual_call
        %copy[@Base_get] slot 0 signature_id 17() :
        (!obelisk_sim.class_handle<@Base>) -> i64

      %interface = obelisk_sim.class.cast %object :
        !obelisk_sim.class_handle<@Derived> to
        !obelisk_sim.class_handle<@I>
      %through_interface = obelisk_sim.class.virtual_call
        %interface[@I_get] slot 4294967295 signature_id 17() :
        (!obelisk_sim.class_handle<@I>) -> i64

      %direct = obelisk_sim.class.direct_call @base_get %base() :
        (!obelisk_sim.class_handle<@Base>) -> i64

      %null = obelisk_sim.class.null :
        !obelisk_sim.class_handle<@Base>
      %null_call = obelisk_sim.class.virtual_call
        %null[@Base_get] slot 0 signature_id 17() :
        (!obelisk_sim.class_handle<@Base>) -> i64

      %bad_object = obelisk_sim.class.alloc %ctx :
        !obelisk_sim.context -> !obelisk_sim.class_handle<@BadSignature>
      %bad_base = obelisk_sim.class.cast %bad_object :
        !obelisk_sim.class_handle<@BadSignature> to
        !obelisk_sim.class_handle<@Base>
      %bad_signature = obelisk_sim.class.virtual_call
        %bad_base[@Base_get] slot 0 signature_id 17() :
        (!obelisk_sim.class_handle<@Base>) -> i64

      %pure_object = obelisk_sim.class.alloc %ctx :
        !obelisk_sim.context -> !obelisk_sim.class_handle<@PureShadow>
      %pure_base = obelisk_sim.class.cast %pure_object :
        !obelisk_sim.class_handle<@PureShadow> to
        !obelisk_sim.class_handle<@Base>
      %pure_shadow = obelisk_sim.class.virtual_call
        %pure_base[@Base_get] slot 0 signature_id 17() :
        (!obelisk_sim.class_handle<@Base>) -> i64

      %ordered_object = obelisk_sim.class.alloc %ctx :
        !obelisk_sim.context -> !obelisk_sim.class_handle<@InterfaceOrder>
      %ordered_interface = obelisk_sim.class.cast %ordered_object :
        !obelisk_sim.class_handle<@InterfaceOrder> to
        !obelisk_sim.class_handle<@I>
      %ordered = obelisk_sim.class.virtual_call
        %ordered_interface[@I_get] slot 4294967295 signature_id 17() :
        (!obelisk_sim.class_handle<@I>) -> i64

      %condition = arith.constant true
      cf.cond_br %condition, ^join(%base : !obelisk_sim.class_handle<@Base>),
                              ^join(%null : !obelisk_sim.class_handle<@Base>)
    ^join(%unknown: !obelisk_sim.class_handle<@Base>):
      %dynamic = obelisk_sim.class.virtual_call
        %unknown[@Base_get] slot 0 signature_id 17() :
        (!obelisk_sim.class_handle<@Base>) -> i64
      obelisk_sim.return
    }
  }
}

// CHECK-LABEL: obelisk_sim.func @root
// CHECK: %[[OBJECT:.*]] = obelisk_sim.class.alloc
// CHECK: %[[BASE:.*]] = obelisk_sim.class.cast %[[OBJECT]]
// CHECK: %[[EXACT_THIS:.*]] = obelisk_sim.class.cast %[[BASE]]
// CHECK-NEXT: obelisk_sim.call @derived_get(%{{.*}}, %[[EXACT_THIS]])
// CHECK: %[[COPY:.*]] = obelisk_sim.class.copy %{{.*}}, %[[BASE]]
// CHECK: %[[COPY_THIS:.*]] = obelisk_sim.class.cast %[[COPY]]
// CHECK-NEXT: obelisk_sim.call @derived_get(%{{.*}}, %[[COPY_THIS]])
// CHECK: %[[INTERFACE:.*]] = obelisk_sim.class.cast %[[OBJECT]]
// CHECK: %[[INTERFACE_THIS:.*]] = obelisk_sim.class.cast %[[INTERFACE]]
// CHECK-NEXT: obelisk_sim.call @derived_get(%{{.*}}, %[[INTERFACE_THIS]])
// CHECK: obelisk_sim.call @base_get
// CHECK: obelisk_sim.class.null
// CHECK-NEXT: obelisk_sim.class.virtual_call
// CHECK: obelisk_sim.class.alloc {{.*}}class_handle<@BadSignature>
// CHECK: obelisk_sim.class.virtual_call
// CHECK: obelisk_sim.class.alloc {{.*}}class_handle<@PureShadow>
// CHECK: obelisk_sim.class.virtual_call
// CHECK: obelisk_sim.class.alloc {{.*}}class_handle<@InterfaceOrder>
// CHECK: obelisk_sim.call @base_get
// CHECK: obelisk_sim.class.virtual_call

// INLINE-LABEL: obelisk_sim.func @root
// INLINE-COUNT-3: arith.constant 2 : i64
// INLINE: arith.constant 1 : i64
// INLINE: obelisk_sim.class.null
// INLINE-NEXT: obelisk_sim.class.virtual_call
// INLINE: obelisk_sim.class.alloc {{.*}}class_handle<@BadSignature>
// INLINE: obelisk_sim.class.virtual_call
// INLINE: obelisk_sim.class.alloc {{.*}}class_handle<@PureShadow>
// INLINE: obelisk_sim.class.virtual_call
// INLINE: obelisk_sim.class.alloc {{.*}}class_handle<@InterfaceOrder>
// INLINE: arith.constant 1 : i64
// INLINE: obelisk_sim.class.virtual_call
