// RUN: obelisk-opt %s | obelisk-opt | FileCheck %s

module {
  obelisk_sim.design @classes {
    obelisk_sim.scope.decl 0 hierarchy "top"
    obelisk_sim.storage.decl 0 in 0 : i64 design
    obelisk_sim.code_unit.decl 1 in 0 root_initializer hierarchy "__obelisk_root"
    obelisk_sim.code_unit.decl 2 in 0 function hierarchy "Base.get"
    obelisk_sim.code_unit.decl 3 in 0 task hierarchy "Base.run"
    obelisk_sim.code_unit.decl 4 in 0 task hierarchy "Derived.run"

    obelisk_sim.class.decl @I id 1 {
      is_abstract = true, is_final = false, is_interface = true
    }
    obelisk_sim.class.decl @Base id 2 {
      is_abstract = false, is_final = false, is_interface = false
    }
    obelisk_sim.class.decl @Derived id 3 extends @Base implements [@I] {
      is_abstract = false, is_final = true, is_interface = false
    }
    obelisk_sim.class.decl @RandomLeaf id 4 {
      is_abstract = false, is_final = false, is_interface = false,
      random_variable_references = [
        #obelisk_sim.random_variable_reference<target = @RandomLeaf_value>,
        #obelisk_sim.random_variable_reference<
          path = [@RandomLeaf_next], target = @RandomLeaf_value>
      ]
    }
    obelisk_sim.class.field @RandomLeaf_value of @RandomLeaf at 0 : i8 {
      is_static = false, is_weak = false,
      obelisk_sim.random_mode_index = 0 : i64,
      obelisk_sim.random_variable_kind = 1 : i32,
      obelisk_sim.random_variable_signed = false
    }
    obelisk_sim.class.field @RandomLeaf_next of @RandomLeaf at 1 :
        !obelisk_sim.class_handle<@RandomLeaf> {
      is_static = false, is_weak = false,
      obelisk_sim.random_mode_index = 1 : i64,
      obelisk_sim.random_object_edge
    }
    obelisk_sim.class.decl @RandomRoot id 5 {
      is_abstract = false, is_final = false, is_interface = false,
      random_variable_references = [
        #obelisk_sim.random_variable_reference<
          path = [@RandomRoot_child], target = @RandomLeaf_value>
      ],
      test_random_value_references = [
        #obelisk_sim.random_value_reference<
          kind = object_field, path = [@RandomRoot_child],
          target = @RandomLeaf_value, low = 0, width = 8>,
        #obelisk_sim.random_value_reference<
          kind = storage, storage = 7 : i64, low = 4, width = 8>
      ]
    }
    obelisk_sim.class.field @RandomRoot_child of @RandomRoot at 0 :
        !obelisk_sim.class_handle<@RandomLeaf> {
      is_static = false, is_weak = false,
      obelisk_sim.random_mode_index = 0 : i64,
      obelisk_sim.random_object_edge
    }
    obelisk_sim.random.constraint_template @RandomRoot_constraints
        of @RandomRoot attributes {
      references = [
        #obelisk_sim.random_value_reference<
          kind = object_field, path = [@RandomRoot_child],
          target = @RandomLeaf_value, low = 0, width = 8>,
        #obelisk_sim.random_value_reference<
          kind = storage, storage = 0 : i64, low = 0, width = 8>
      ],
      constraint_blocks = [
        #obelisk_sim.random_constraint_block_reference<
          kind = object_block, index = 0 : i32>,
        #obelisk_sim.random_constraint_block_reference<
          kind = storage, storage = 0 : i64>
      ]
    } {
      %field = obelisk_sim.random.constraint_value 0 : i8
      %state = obelisk_sim.random.constraint_value 1 : i8
      %equal = arith.cmpi eq, %field, %state : i8
      obelisk_sim.random.hard_constraint %equal block 0
      %true = arith.constant true
      obelisk_sim.random.soft_constraint %true block 1 priority 0
    }
    obelisk_sim.class.method @I_first of @I slot 4294967295
        signature_id 15 interface_ordinal 0 :
      (!obelisk_sim.context, !obelisk_sim.class_handle<@I>) -> i64 {
        is_final = false, is_pure = true, is_static = false,
        is_task = false, is_virtual = true
      }
    obelisk_sim.class.method @I_second of @I slot 4294967295
        signature_id 16 interface_ordinal 1 :
      (!obelisk_sim.context, !obelisk_sim.class_handle<@I>) -> i64 {
        is_final = false, is_pure = true, is_static = false,
        is_task = false, is_virtual = true
      }
    obelisk_sim.class.method @I_run of @I slot 4294967295
        signature_id 19 interface_ordinal 2 :
      (!obelisk_sim.context, !obelisk_sim.class_handle<@I>, i64) -> () {
        is_final = false, is_pure = true, is_static = false,
        is_task = true, is_virtual = true
      }
    obelisk_sim.class.field @Base_value of @Base at 0 offset 8 : i64 {
      is_static = false, is_weak = false
    }
    obelisk_sim.class.method @Base_get of @Base slot 0 signature_id 17 implemented_by @base_get :
      (!obelisk_sim.context, !obelisk_sim.class_handle<@Base>) -> i64 {
        is_final = false, is_pure = false, is_static = false,
        is_task = false, is_virtual = true
      }
    obelisk_sim.class.method @Base_run of @Base slot 1 signature_id 18
        implemented_by @base_run :
      (!obelisk_sim.context, !obelisk_sim.class_handle<@Base>, i64) -> () {
        is_final = false, is_pure = false, is_static = false,
        is_task = true, is_virtual = true
      }
    obelisk_sim.class.method @Derived_run of @Derived slot 2 signature_id 19
        implemented_by @derived_run :
      (!obelisk_sim.context, !obelisk_sim.class_handle<@Derived>, i64) -> () {
        is_final = true, is_pure = false, is_static = false,
        is_task = true, is_virtual = true
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

    obelisk_sim.func @base_run(
        %ctx: !obelisk_sim.context {obelisk_sim.capture_kind = 0 : i32},
        %this: !obelisk_sim.class_handle<@Base>
          {obelisk_sim.capture_kind = 1 : i32},
        %value: i64 {obelisk_sim.capture_kind = 1 : i32})
        attributes {code_unit_id = 3 : i64, entry_kind = 12 : i32} {
      obelisk_sim.return
    }

    obelisk_sim.func @derived_run(
        %ctx: !obelisk_sim.context {obelisk_sim.capture_kind = 0 : i32},
        %this: !obelisk_sim.class_handle<@Derived>
          {obelisk_sim.capture_kind = 1 : i32},
        %value: i64 {obelisk_sim.capture_kind = 1 : i32})
        attributes {code_unit_id = 4 : i64, entry_kind = 12 : i32} {
      obelisk_sim.return
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
      %interface = obelisk_sim.class.cast %object :
        !obelisk_sim.class_handle<@Derived> to
        !obelisk_sim.class_handle<@I>
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
      obelisk_sim.class.virtual_task_call
        %base[@Base_run] slot 1 signature_id 18
        (%one) arguments 1 to ^interface_call :
        (!obelisk_sim.class_handle<@Base>, i64) -> ()
    ^interface_call:
      obelisk_sim.class.virtual_task_call
        %interface[@I_run] slot 4294967295 signature_id 19
        (%one) arguments 1 to ^done :
        (!obelisk_sim.class_handle<@I>, i64) -> ()
    ^done:
      obelisk_sim.return
    }
  }
}

// CHECK: obelisk_sim.class.decl @Derived id 3 extends @Base implements [@I]
// CHECK: obelisk_sim.class.decl @RandomLeaf id 4
// CHECK-SAME: random_variable_references = [#obelisk_sim.random_variable_reference<target = @RandomLeaf_value>, #obelisk_sim.random_variable_reference<path = [@RandomLeaf_next],target = @RandomLeaf_value>]
// CHECK: obelisk_sim.class.decl @RandomRoot id 5
// CHECK-SAME: #obelisk_sim.random_variable_reference<path = [@RandomRoot_child],target = @RandomLeaf_value>
// CHECK-SAME: test_random_value_references = [#obelisk_sim.random_value_reference<kind = object_field, path = [@RandomRoot_child], target = @RandomLeaf_value, low = 0, width = 8>, #obelisk_sim.random_value_reference<kind = storage, storage = 7 : i64, low = 4, width = 8>]
// CHECK: obelisk_sim.random.constraint_template @RandomRoot_constraints of @RandomRoot
// CHECK-SAME: constraint_blocks = [#obelisk_sim.random_constraint_block_reference<kind = object_block, index = 0 : i32>, #obelisk_sim.random_constraint_block_reference<kind = storage, storage = 0 : i64>]
// CHECK-SAME: references = [#obelisk_sim.random_value_reference<kind = object_field, path = [@RandomRoot_child], target = @RandomLeaf_value, low = 0, width = 8>, #obelisk_sim.random_value_reference<kind = storage, storage = 0 : i64, low = 0, width = 8>]
// CHECK: %{{.*}} = obelisk_sim.random.constraint_value 0 : i8
// CHECK: obelisk_sim.random.hard_constraint %{{.*}} block 0
// CHECK: obelisk_sim.random.soft_constraint %{{.*}} block 1 priority 0
// CHECK: obelisk_sim.class.method @I_first of @I slot 4294967295
// CHECK-SAME: interface_ordinal 0
// CHECK: obelisk_sim.class.method @I_second of @I slot 4294967295
// CHECK-SAME: interface_ordinal 1
// CHECK: obelisk_sim.class.method @Base_get of @Base slot 0
// CHECK: !obelisk_sim.class_handle<@Derived>
// CHECK: obelisk_sim.managed.nba.enqueue
// CHECK: obelisk_sim.class.virtual_call
// CHECK: obelisk_sim.gc.safepoint
// CHECK: obelisk_sim.class.virtual_task_call
// CHECK: obelisk_sim.class.virtual_task_call
// CHECK-SAME: slot 4294967295
