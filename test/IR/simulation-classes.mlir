// RUN: obelisk-opt %s | obelisk-opt | FileCheck %s

module {
  simulation.design @classes {
    simulation.scope.decl 0 hierarchy "top"
    simulation.storage.decl 0 in 0 : i64 design
    simulation.code_unit.decl 1 in 0 root_initializer hierarchy "__obelisk_root"
    simulation.code_unit.decl 2 in 0 function hierarchy "Base.get"
    simulation.code_unit.decl 3 in 0 task hierarchy "Base.run"
    simulation.code_unit.decl 4 in 0 task hierarchy "Derived.run"

    simulation.class.decl @I id 1 {
      is_abstract = true, is_final = false, is_interface = true
    }
    simulation.class.decl @Base id 2 {
      is_abstract = false, is_final = false, is_interface = false
    }
    simulation.class.decl @Derived id 3 extends @Base implements [@I] {
      is_abstract = false, is_final = true, is_interface = false
    }
    simulation.class.decl @RandomLeaf id 4 {
      is_abstract = false, is_final = false, is_interface = false,
      random_variable_references = [
        #simulation.random_variable_reference<target = @RandomLeaf_value>,
        #simulation.random_variable_reference<
          path = [@RandomLeaf_next], target = @RandomLeaf_value>
      ]
    }
    simulation.class.field @RandomLeaf_value of @RandomLeaf at 0 : i8 {
      is_static = false, is_weak = false,
      simulation.random_mode_index = 0 : i64,
      simulation.random_variable_kind = 1 : i32,
      simulation.random_variable_signed = false
    }
    simulation.class.field @RandomLeaf_next of @RandomLeaf at 1 :
        !simulation.class_handle<@RandomLeaf> {
      is_static = false, is_weak = false,
      simulation.random_mode_index = 1 : i64,
      simulation.random_object_edge
    }
    simulation.class.decl @RandomRoot id 5 {
      is_abstract = false, is_final = false, is_interface = false,
      random_constraint_template = @RandomRoot_constraints,
      random_variable_references = [
        #simulation.random_variable_reference<
          path = [@RandomRoot_child], target = @RandomLeaf_value>
      ],
      test_random_value_references = [
        #simulation.random_value_reference<
          kind = object_field, path = [@RandomRoot_child],
          target = @RandomLeaf_value, low = 0, width = 8>,
        #simulation.random_value_reference<
          kind = storage, storage = 7 : i64, low = 4, width = 8>
      ]
    }
    simulation.class.field @RandomRoot_child of @RandomRoot at 0 :
        !simulation.class_handle<@RandomLeaf> {
      is_static = false, is_weak = false,
      simulation.random_mode_index = 0 : i64,
      simulation.random_object_edge
    }
    simulation.random.constraint_template @RandomRoot_constraints
        of @RandomRoot attributes {
      references = [
        #simulation.random_value_reference<
          kind = object_field, path = [@RandomRoot_child],
          target = @RandomLeaf_value, low = 0, width = 8>,
        #simulation.random_value_reference<
          kind = storage, storage = 0 : i64, low = 0, width = 8>
      ],
      constraint_blocks = [
        #simulation.random_constraint_block_reference<
          kind = object_block, index = 0 : i32>,
        #simulation.random_constraint_block_reference<
          kind = storage, storage = 0 : i64>
      ]
    } {
      %field = simulation.random.constraint_value 0 : i8
      %state = simulation.random.constraint_value 1 : i8
      %equal = arith.cmpi eq, %field, %state : i8
      simulation.random.hard_constraint %equal block 0
      %true = arith.constant true
      simulation.random.soft_constraint %true block 1 priority 0
    }
    simulation.class.method @I_first of @I slot 4294967295
        signature_id 15 interface_ordinal 0 :
      (!simulation.context, !simulation.class_handle<@I>) -> i64 {
        is_final = false, is_pure = true, is_static = false,
        is_task = false, is_virtual = true
      }
    simulation.class.method @I_second of @I slot 4294967295
        signature_id 16 interface_ordinal 1 :
      (!simulation.context, !simulation.class_handle<@I>) -> i64 {
        is_final = false, is_pure = true, is_static = false,
        is_task = false, is_virtual = true
      }
    simulation.class.method @I_run of @I slot 4294967295
        signature_id 19 interface_ordinal 2 :
      (!simulation.context, !simulation.class_handle<@I>, i64) -> () {
        is_final = false, is_pure = true, is_static = false,
        is_task = true, is_virtual = true
      }
    simulation.class.field @Base_value of @Base at 0 offset 8 : i64 {
      is_static = false, is_weak = false
    }
    simulation.class.method @Base_get of @Base slot 0 signature_id 17 implemented_by @base_get :
      (!simulation.context, !simulation.class_handle<@Base>) -> i64 {
        is_final = false, is_pure = false, is_static = false,
        is_task = false, is_virtual = true
      }
    simulation.class.method @Base_run of @Base slot 1 signature_id 18
        implemented_by @base_run :
      (!simulation.context, !simulation.class_handle<@Base>, i64) -> () {
        is_final = false, is_pure = false, is_static = false,
        is_task = true, is_virtual = true
      }
    simulation.class.method @Derived_run of @Derived slot 2 signature_id 19
        implemented_by @derived_run :
      (!simulation.context, !simulation.class_handle<@Derived>, i64) -> () {
        is_final = true, is_pure = false, is_static = false,
        is_task = true, is_virtual = true
      }

    simulation.func @base_get(
        %ctx: !simulation.context {simulation.capture_kind = 0 : i32},
        %this: !simulation.class_handle<@Base>
          {simulation.capture_kind = 1 : i32}) -> i64
        attributes {code_unit_id = 2 : i64, entry_kind = 8 : i32} {
      %field = simulation.class.field_ref %this[@Base_value] :
        !simulation.class_handle<@Base> ->
        !simulation.managed_ref<i64, @Base>
      %value = simulation.managed.load %field :
        !simulation.managed_ref<i64, @Base> -> i64
      simulation.return %value : i64
    }

    simulation.func @base_run(
        %ctx: !simulation.context {simulation.capture_kind = 0 : i32},
        %this: !simulation.class_handle<@Base>
          {simulation.capture_kind = 1 : i32},
        %value: i64 {simulation.capture_kind = 1 : i32})
        attributes {code_unit_id = 3 : i64, entry_kind = 12 : i32} {
      simulation.return
    }

    simulation.func @derived_run(
        %ctx: !simulation.context {simulation.capture_kind = 0 : i32},
        %this: !simulation.class_handle<@Derived>
          {simulation.capture_kind = 1 : i32},
        %value: i64 {simulation.capture_kind = 1 : i32})
        attributes {code_unit_id = 4 : i64, entry_kind = 12 : i32} {
      simulation.return
    }

    simulation.func @root(
        %ctx: !simulation.context {simulation.capture_kind = 0 : i32})
        attributes {code_unit_id = 1 : i64, entry_kind = 0 : i32} {
      %null = simulation.class.null :
        !simulation.class_handle<@Derived>
      %object = simulation.class.alloc %ctx :
        !simulation.context -> !simulation.class_handle<@Derived>
      %copy = simulation.class.copy %ctx, %object :
        !simulation.context, !simulation.class_handle<@Derived> ->
        !simulation.class_handle<@Derived>
      %is_base = simulation.class.is_instance %object is @Base :
        !simulation.class_handle<@Derived>
      %base = simulation.class.cast %object :
        !simulation.class_handle<@Derived> to
        !simulation.class_handle<@Base>
      %interface = simulation.class.cast %object :
        !simulation.class_handle<@Derived> to
        !simulation.class_handle<@I>
      %field = simulation.class.field_ref %base[@Base_value] :
        !simulation.class_handle<@Base> ->
        !simulation.managed_ref<i64, @Base>
      %one = arith.constant 1 : i64
      simulation.managed.store %one to %field :
        i64, !simulation.managed_ref<i64, @Base>
      %delay = simulation.time.constant 2
      simulation.managed.nba.enqueue %one to %field after %delay :
        (i64, !simulation.managed_ref<i64, @Base>, !simulation.time) -> ()
      %direct = simulation.class.direct_call @base_get %base() :
        (!simulation.class_handle<@Base>) -> i64
      %virtual = simulation.class.virtual_call
        %base[@Base_get] slot 0 signature_id 17() :
        (!simulation.class_handle<@Base>) -> i64
      simulation.gc.safepoint %ctx : !simulation.context
      simulation.class.virtual_task_call
        %base[@Base_run] slot 1 signature_id 18
        (%one) arguments 1 to ^interface_call :
        (!simulation.class_handle<@Base>, i64) -> ()
    ^interface_call:
      simulation.class.virtual_task_call
        %interface[@I_run] slot 4294967295 signature_id 19
        (%one) arguments 1 to ^done :
        (!simulation.class_handle<@I>, i64) -> ()
    ^done:
      simulation.return
    }
  }
}

// CHECK: simulation.class.decl @Derived id 3 extends @Base implements [@I]
// CHECK: simulation.class.decl @RandomLeaf id 4
// CHECK-SAME: random_variable_references = [#simulation.random_variable_reference<target = @RandomLeaf_value>, #simulation.random_variable_reference<path = [@RandomLeaf_next],target = @RandomLeaf_value>]
// CHECK: simulation.class.decl @RandomRoot id 5
// CHECK-SAME: random_constraint_template = @RandomRoot_constraints
// CHECK-SAME: #simulation.random_variable_reference<path = [@RandomRoot_child],target = @RandomLeaf_value>
// CHECK-SAME: test_random_value_references = [#simulation.random_value_reference<kind = object_field, path = [@RandomRoot_child], target = @RandomLeaf_value, low = 0, width = 8>, #simulation.random_value_reference<kind = storage, storage = 7 : i64, low = 4, width = 8>]
// CHECK: simulation.random.constraint_template @RandomRoot_constraints of @RandomRoot
// CHECK-SAME: constraint_blocks = [#simulation.random_constraint_block_reference<kind = object_block, index = 0 : i32>, #simulation.random_constraint_block_reference<kind = storage, storage = 0 : i64>]
// CHECK-SAME: references = [#simulation.random_value_reference<kind = object_field, path = [@RandomRoot_child], target = @RandomLeaf_value, low = 0, width = 8>, #simulation.random_value_reference<kind = storage, storage = 0 : i64, low = 0, width = 8>]
// CHECK: %{{.*}} = simulation.random.constraint_value 0 : i8
// CHECK: simulation.random.hard_constraint %{{.*}} block 0
// CHECK: simulation.random.soft_constraint %{{.*}} block 1 priority 0
// CHECK: simulation.class.method @I_first of @I slot 4294967295
// CHECK-SAME: interface_ordinal 0
// CHECK: simulation.class.method @I_second of @I slot 4294967295
// CHECK-SAME: interface_ordinal 1
// CHECK: simulation.class.method @Base_get of @Base slot 0
// CHECK: !simulation.class_handle<@Derived>
// CHECK: simulation.managed.nba.enqueue
// CHECK: simulation.class.virtual_call
// CHECK: simulation.gc.safepoint
// CHECK: simulation.class.virtual_task_call
// CHECK: simulation.class.virtual_task_call
// CHECK-SAME: slot 4294967295
