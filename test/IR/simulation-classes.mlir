// RUN: obelisk-opt %s | obelisk-opt | FileCheck %s

module {
  obelisk_sim.design @classes {
    obelisk_sim.scope.decl 0 hierarchy "top"
    obelisk_sim.code_unit.decl 1 in 0 root_initializer hierarchy "__obelisk_root"
    obelisk_sim.code_unit.decl 2 in 0 function hierarchy "Base.get"

    obelisk_sim.class.decl @I id 1 {
      is_abstract = true, is_final = false, is_interface = true
    }
    obelisk_sim.class.decl @Base id 2 {
      is_abstract = false, is_final = false, is_interface = false
    }
    obelisk_sim.class.decl @Derived id 3 extends @Base implements [@I] {
      is_abstract = false, is_final = true, is_interface = false
    }
    obelisk_sim.class.field @Base_value of @Base at 0 offset 8 : i64 {
      is_static = false, is_weak = false
    }
    obelisk_sim.class.method @Base_get of @Base slot 0 signature_id 17 implemented_by @base_get :
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
      obelisk_sim.gc.safepoint %ctx : !obelisk_sim.context
      obelisk_sim.return
    }
  }
}

// CHECK: obelisk_sim.class.decl @Derived id 3 extends @Base implements [@I]
// CHECK: obelisk_sim.class.method @Base_get of @Base slot 0
// CHECK: !obelisk_sim.class_handle<@Derived>
// CHECK: obelisk_sim.managed.nba.enqueue
// CHECK: obelisk_sim.class.virtual_call
// CHECK: obelisk_sim.gc.safepoint
