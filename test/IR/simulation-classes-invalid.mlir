// RUN: obelisk-opt --split-input-file --verify-diagnostics %s

module {
  simulation.design @empty_random_reference_inventory {
    simulation.scope.decl 0
    // expected-error @below {{random-variable reference inventory must be absent when empty}}
    simulation.class.decl @C id 1 {
      is_abstract = false, is_final = false, is_interface = false,
      random_variable_references = []
    }
  }
}

// -----

module {
  simulation.design @unknown_base {
    simulation.scope.decl 0
    // expected-error @below {{references an unknown base class}}
    simulation.class.decl @C id 1 extends @Missing {
      is_abstract = false, is_final = false, is_interface = false
    }
  }
}

// -----

module {
  simulation.design @unknown_random_constraint_template {
    simulation.scope.decl 0
    // expected-error @below {{random constraint template references an unknown template}}
    simulation.class.decl @C id 1 {
      is_abstract = false, is_final = false, is_interface = false,
      random_constraint_template = @missing
    }
  }
}

// -----

module {
  simulation.design @wrong_random_constraint_template_owner {
    simulation.scope.decl 0
    // expected-error @below {{random constraint template must be owned by this class}}
    simulation.class.decl @A id 1 {
      is_abstract = false, is_final = false, is_interface = false,
      random_constraint_template = @constraints
    }
    simulation.class.decl @B id 2 {
      is_abstract = false, is_final = false, is_interface = false
    }
    simulation.random.constraint_template @constraints of @B attributes {
      constraint_blocks = [
        #simulation.random_constraint_block_reference<
          kind = object_block, index = 0 : i32>
      ]
    } {
      %true = arith.constant true
      simulation.random.hard_constraint %true block 0
    }
  }
}

// -----

module {
  simulation.design @duplicate_random_reference {
    simulation.scope.decl 0
    // expected-error @below {{random-variable reference inventory contains a duplicate}}
    simulation.class.decl @C id 1 {
      is_abstract = false, is_final = false, is_interface = false,
      random_variable_references = [
        #simulation.random_variable_reference<target = @C_value>,
        #simulation.random_variable_reference<target = @C_value>
      ]
    }
    simulation.class.field @C_value of @C at 0 : i8 {
      is_static = false, is_weak = false,
      simulation.random_mode_index = 0 : i64,
      simulation.random_variable_kind = 1 : i32,
      simulation.random_variable_signed = false
    }
  }
}

// -----

module {
  simulation.design @random_reference_non_edge_path {
    simulation.scope.decl 0
    simulation.class.decl @Leaf id 1 {
      is_abstract = false, is_final = false, is_interface = false
    }
    simulation.class.field @Leaf_value of @Leaf at 0 : i8 {
      is_static = false, is_weak = false,
      simulation.random_mode_index = 0 : i64,
      simulation.random_variable_kind = 1 : i32,
      simulation.random_variable_signed = false
    }
    // expected-error @below {{random-variable reference path field @Root_child must be a strong rand object edge}}
    simulation.class.decl @Root id 2 {
      is_abstract = false, is_final = false, is_interface = false,
      random_variable_references = [
        #simulation.random_variable_reference<
          path = [@Root_child], target = @Leaf_value>
      ]
    }
    simulation.class.field @Root_child of @Root at 0 :
        !simulation.class_handle<@Leaf> {
      is_static = false, is_weak = false,
      simulation.random_mode_index = 0 : i64
    }
  }
}

// -----

module {
  simulation.design @random_reference_incompatible_target {
    simulation.scope.decl 0
    simulation.class.decl @Leaf id 1 {
      is_abstract = false, is_final = false, is_interface = false
    }
    simulation.class.decl @Other id 2 {
      is_abstract = false, is_final = false, is_interface = false
    }
    simulation.class.field @Other_value of @Other at 0 : i8 {
      is_static = false, is_weak = false,
      simulation.random_mode_index = 0 : i64,
      simulation.random_variable_kind = 1 : i32,
      simulation.random_variable_signed = false
    }
    // expected-error @below {{random-variable reference target @Other_value is not visible from class Leaf}}
    simulation.class.decl @Root id 3 {
      is_abstract = false, is_final = false, is_interface = false,
      random_variable_references = [
        #simulation.random_variable_reference<
          path = [@Root_child], target = @Other_value>
      ]
    }
    simulation.class.field @Root_child of @Root at 0 :
        !simulation.class_handle<@Leaf> {
      is_static = false, is_weak = false,
      simulation.random_mode_index = 0 : i64,
      simulation.random_object_edge
    }
  }
}

// -----

module {
  simulation.design @random_reference_non_random_target {
    simulation.scope.decl 0
    // expected-error @below {{random-variable reference target @C_value must be a packed instance rand or randc field}}
    simulation.class.decl @C id 1 {
      is_abstract = false, is_final = false, is_interface = false,
      random_variable_references = [
        #simulation.random_variable_reference<target = @C_value>
      ]
    }
    simulation.class.field @C_value of @C at 0 : i8 {
      is_static = false, is_weak = false
    }
  }
}

// -----

module {
  simulation.design @cycle {
    simulation.scope.decl 0
    // expected-error @below {{class inheritance contains a cycle}}
    simulation.class.decl @A id 1 extends @B {
      is_abstract = false, is_final = false, is_interface = false
    }
    simulation.class.decl @B id 2 extends @A {
      is_abstract = false, is_final = false, is_interface = false
    }
  }
}

// -----

module {
  simulation.design @random_object_edge_without_mode {
    simulation.scope.decl 0
    simulation.class.decl @C id 1 {
      is_abstract = false, is_final = false, is_interface = false
    }
    // expected-error @below {{random object edge requires an indexed, strong instance class-handle field}}
    simulation.class.field @C_child of @C at 0 :
        !simulation.class_handle<@C> {
      is_static = false, is_weak = false,
      simulation.random_object_edge
    }
  }
}

// -----

module {
  simulation.design @invalid_random_mode_field {
    simulation.scope.decl 0
    // expected-error @below {{random mode field must name an instance i64 field owned by the root class}}
    simulation.class.decl @C id 1 {
      is_abstract = false, is_final = false, is_interface = false,
      simulation.random_mode_field = @C_mode
    }
    simulation.class.field @C_mode of @C at 0 : i32 {
      is_static = false, is_weak = false
    }
  }
}

// -----

module {
  simulation.design @random_variable_without_signedness {
    simulation.scope.decl 0
    simulation.class.decl @C id 1 {
      is_abstract = false, is_final = false, is_interface = false
    }
    // expected-error @below {{random variable metadata requires signedness}}
    simulation.class.field @C_value of @C at 0 : i8 {
      is_static = false, is_weak = false,
      simulation.random_mode_index = 0 : i64,
      simulation.random_variable_kind = 1 : i32
    }
  }
}

// -----

module {
  simulation.design @randc_without_cycle_state {
    simulation.scope.decl 0
    simulation.class.decl @C id 1 {
      is_abstract = false, is_final = false, is_interface = false
    }
    // expected-error @below {{randc variables require key and position fields; rand variables forbid them}}
    simulation.class.field @C_value of @C at 0 : i8 {
      is_static = false, is_weak = false,
      simulation.random_mode_index = 0 : i64,
      simulation.random_variable_kind = 2 : i32,
      simulation.random_variable_signed = false
    }
  }
}

// -----

module {
  simulation.design @self_interface_cycle {
    simulation.scope.decl 0
    // expected-error @below {{interface inheritance contains a cycle}}
    simulation.class.decl @I id 1 implements [@I] {
      is_abstract = true, is_final = false, is_interface = true
    }
  }
}

// -----

module {
  simulation.design @transitive_interface_cycle {
    simulation.scope.decl 0
    // expected-error @below {{interface inheritance contains a cycle}}
    simulation.class.decl @I id 1 implements [@J] {
      is_abstract = true, is_final = false, is_interface = true
    }
    simulation.class.decl @J id 2 implements [@K] {
      is_abstract = true, is_final = false, is_interface = true
    }
    simulation.class.decl @K id 3 implements [@I] {
      is_abstract = true, is_final = false, is_interface = true
    }
  }
}

// -----

module {
  simulation.design @invalid_edge_before_cycle {
    simulation.scope.decl 0
    simulation.class.decl @I id 1 implements [@J] {
      is_abstract = true, is_final = false, is_interface = true
    }
    // expected-error @below {{implements list references a non-interface class}}
    simulation.class.decl @J id 2 implements [@C] {
      is_abstract = true, is_final = false, is_interface = true
    }
    simulation.class.decl @C id 3 implements [@I] {
      is_abstract = false, is_final = false, is_interface = false
    }
  }
}

// -----

module {
  simulation.design @bad_weak_specialization {
    simulation.scope.decl 0
    simulation.code_unit.decl 1 in 0 root_initializer hierarchy "root"
    simulation.class.decl @Referent id 1 {
      is_abstract = false, is_final = false, is_interface = false
    }
    simulation.class.decl @Other id 2 {
      is_abstract = false, is_final = false, is_interface = false
    }
    simulation.class.decl @Weak id 3 {
      is_abstract = false, is_final = false, is_interface = false,
      weak_referent = @Referent
    }
    simulation.func @root(
        %ctx: !simulation.context {simulation.capture_kind = 0 : i32})
        attributes {code_unit_id = 1 : i64, entry_kind = 0 : i32} {
      %other = simulation.class.alloc %ctx :
        !simulation.context -> !simulation.class_handle<@Other>
      // expected-error @below {{referent type does not match the weak_reference specialization}}
      %weak = simulation.weak.create %ctx, %other :
        !simulation.context, !simulation.class_handle<@Other> ->
        !simulation.class_handle<@Weak>
      simulation.return
    }
  }
}

// -----

module {
  simulation.design @bad_interface {
    simulation.scope.decl 0
    simulation.class.decl @NotInterface id 1 {
      is_abstract = false, is_final = false, is_interface = false
    }
    // expected-error @below {{implements list references a non-interface class}}
    simulation.class.decl @C id 2 implements [@NotInterface] {
      is_abstract = false, is_final = false, is_interface = false
    }
  }
}

// -----

module {
  simulation.design @abstract_alloc {
    simulation.scope.decl 0
    simulation.code_unit.decl 1 in 0 root_initializer hierarchy "root"
    simulation.class.decl @Abstract id 1 {
      is_abstract = true, is_final = false, is_interface = false
    }
    simulation.func @root(
        %ctx: !simulation.context {simulation.capture_kind = 0 : i32})
        attributes {code_unit_id = 1 : i64, entry_kind = 0 : i32} {
      // expected-error @below {{cannot allocate an abstract or interface class}}
      %object = simulation.class.alloc %ctx :
        !simulation.context -> !simulation.class_handle<@Abstract>
      simulation.return
    }
  }
}

// -----

module {
  simulation.design @bad_field_ref {
    simulation.scope.decl 0
    simulation.code_unit.decl 1 in 0 function hierarchy "C.f"
    simulation.class.decl @C id 1 {
      is_abstract = false, is_final = false, is_interface = false
    }
    simulation.class.field @C_value of @C at 0 offset 8 : i64 {
      is_static = false, is_weak = false
    }
    simulation.func @f(
        %ctx: !simulation.context {simulation.capture_kind = 0 : i32},
        %this: !simulation.class_handle<@C>
          {simulation.capture_kind = 1 : i32})
        attributes {code_unit_id = 1 : i64, entry_kind = 8 : i32} {
      // expected-error @below {{managed reference type does not match the property}}
      %field = simulation.class.field_ref %this[@C_value] :
        !simulation.class_handle<@C> ->
        !simulation.managed_ref<i32, @C>
      simulation.return
    }
  }
}

// -----

module {
  simulation.design @bad_managed_nba {
    simulation.scope.decl 0
    simulation.code_unit.decl 1 in 0 function hierarchy "C.f"
    simulation.class.decl @C id 1 {
      is_abstract = false, is_final = false, is_interface = false
    }
    simulation.class.field @C_value of @C at 0 offset 8 : i64 {
      is_static = false, is_weak = false
    }
    simulation.func @f(
        %ctx: !simulation.context {simulation.capture_kind = 0 : i32},
        %this: !simulation.class_handle<@C>
          {simulation.capture_kind = 1 : i32})
        attributes {code_unit_id = 1 : i64, entry_kind = 8 : i32} {
      %field = simulation.class.field_ref %this[@C_value] :
        !simulation.class_handle<@C> ->
        !simulation.managed_ref<i64, @C>
      %value = arith.constant 1 : i32
      // expected-error @below {{value type must match the referenced element}}
      simulation.managed.nba.enqueue %value to %field :
        (i32, !simulation.managed_ref<i64, @C>) -> ()
      simulation.return
    }
  }
}

// -----

module {
  simulation.design @bad_field_watch {
    simulation.scope.decl 0
    simulation.code_unit.decl 1 in 0 function hierarchy "C.f"
    simulation.class.decl @C id 1 {
      is_abstract = false, is_final = false, is_interface = false
    }
    simulation.func @f(
        %ctx: !simulation.context {simulation.capture_kind = 0 : i32},
        %this: !simulation.class_handle<@C>
          {simulation.capture_kind = 1 : i32})
        attributes {code_unit_id = 1 : i64, entry_kind = 8 : i32} {
      // expected-error @below {{field watches require a managed reference}}
      %watch = simulation.managed.watch field %this :
        !simulation.class_handle<@C>
      simulation.return
    }
  }
}

// -----

module {
  simulation.design @bad_container_size_watch {
    simulation.scope.decl 0
    simulation.code_unit.decl 1 in 0 function hierarchy "C.f"
    simulation.class.decl @C id 1 {
      is_abstract = false, is_final = false, is_interface = false
    }
    simulation.class.field @C_value of @C at 0 offset 8 : i64 {
      is_static = false, is_weak = false
    }
    simulation.func @f(
        %ctx: !simulation.context {simulation.capture_kind = 0 : i32},
        %this: !simulation.class_handle<@C>
          {simulation.capture_kind = 1 : i32})
        attributes {code_unit_id = 1 : i64, entry_kind = 8 : i32} {
      %field = simulation.class.field_ref %this[@C_value] :
        !simulation.class_handle<@C> ->
        !simulation.managed_ref<i64, @C>
      // expected-error @below {{container-size watches require a dynamic, queue, or associative array handle}}
      %watch = simulation.managed.watch container_size %field :
        !simulation.managed_ref<i64, @C>
      simulation.return
    }
  }
}

// -----

module {
  simulation.design @bad_virtual_signature {
    simulation.scope.decl 0
    simulation.code_unit.decl 1 in 0 function hierarchy "C.f"
    simulation.code_unit.decl 2 in 0 root_initializer hierarchy "root"
    simulation.class.decl @C id 1 {
      is_abstract = false, is_final = false, is_interface = false
    }
    simulation.class.method @C_f of @C slot 0 signature_id 17
        implemented_by @f :
        (!simulation.context, !simulation.class_handle<@C>) -> i64 {
      is_final = false, is_pure = false, is_static = false,
      is_task = false, is_virtual = true
    }
    simulation.func @f(
        %ctx: !simulation.context {simulation.capture_kind = 0 : i32},
        %this: !simulation.class_handle<@C>
          {simulation.capture_kind = 1 : i32}) -> i64
        attributes {code_unit_id = 1 : i64, entry_kind = 8 : i32} {
      %zero = arith.constant 0 : i64
      simulation.return %zero : i64
    }
    simulation.func @root(
        %ctx: !simulation.context {simulation.capture_kind = 0 : i32})
        attributes {code_unit_id = 2 : i64, entry_kind = 0 : i32} {
      %object = simulation.class.alloc %ctx :
        !simulation.context -> !simulation.class_handle<@C>
      // expected-error @below {{signature ID does not match the virtual method}}
      %value = simulation.class.virtual_call
        %object[@C_f] slot 0 signature_id 18() :
        (!simulation.class_handle<@C>) -> i64
      simulation.return
    }
  }
}

// -----

module {
  simulation.design @bad_argument_ref_conversion {
    simulation.scope.decl 0
    simulation.code_unit.decl 1 in 0 function hierarchy "f"
    simulation.func @f(
        %ctx: !simulation.context {simulation.capture_kind = 0 : i32},
        %storage: !simulation.ref<i64>
          {simulation.capture_kind = 1 : i32})
        attributes {code_unit_id = 1 : i64, entry_kind = 1 : i32} {
      // expected-error @below {{input and result element types must have equivalent packed layouts}}
      %reference = simulation.argument_ref.from_ref %storage :
        !simulation.ref<i64> -> !simulation.argument_ref<i32>
      simulation.return
    }
  }
}

// -----

module {
  simulation.design @bad_argument_ref_retype {
    simulation.scope.decl 0
    simulation.code_unit.decl 1 in 0 function hierarchy "f"
    simulation.func @f(
        %ctx: !simulation.context {simulation.capture_kind = 0 : i32},
        %reference: !simulation.argument_ref<i32>
          {simulation.capture_kind = 1 : i32})
        attributes {code_unit_id = 1 : i64, entry_kind = 1 : i32} {
      // expected-error @below {{input and result element types must have equivalent packed layouts}}
      %view = simulation.argument_ref.retype %reference :
        !simulation.argument_ref<i32> ->
        !simulation.argument_ref<!simulation.logic<32>>
      simulation.return
    }
  }
}

// -----

module {
  simulation.design @bad_argument_ref_load {
    simulation.scope.decl 0
    simulation.code_unit.decl 1 in 0 function hierarchy "f"
    simulation.func @f(
        %ctx: !simulation.context {simulation.capture_kind = 0 : i32},
        %reference: !simulation.argument_ref<i64>
          {simulation.capture_kind = 1 : i32})
        attributes {code_unit_id = 1 : i64, entry_kind = 1 : i32} {
      // expected-error @below {{result type must match the referenced element}}
      %value = simulation.argument_ref.load %reference :
        !simulation.argument_ref<i64> -> i32
      simulation.return
    }
  }
}

// -----

module {
  simulation.design @bad_managed_null {
    simulation.scope.decl 0
    simulation.code_unit.decl 1 in 0 function hierarchy "f"
    simulation.func @f(
        %ctx: !simulation.context {simulation.capture_kind = 0 : i32})
        attributes {code_unit_id = 1 : i64, entry_kind = 1 : i32} {
      // expected-error @below {{result must be a non-class managed handle type}}
      %null = simulation.managed.null : f64
      simulation.return
    }
  }
}

// -----

module {
  simulation.design @bad_interface_method_slot {
    simulation.scope.decl 0
    simulation.class.decl @I id 1 {
      is_abstract = true, is_final = false, is_interface = true
    }
    // expected-error @below {{interface virtual methods require the interface dispatch slot}}
    simulation.class.method @I_f of @I slot 0 signature_id 17
        interface_ordinal 0 :
      (!simulation.context, !simulation.class_handle<@I>) -> i64 {
        is_final = false, is_pure = true, is_static = false,
        is_task = false, is_virtual = true
      }
  }
}

// -----

module {
  simulation.design @missing_interface_method_ordinal {
    simulation.scope.decl 0
    simulation.class.decl @I id 1 {
      is_abstract = true, is_final = false, is_interface = true
    }
    // expected-error @below {{interface virtual methods require a 32-bit interface ordinal}}
    simulation.class.method @I_f of @I slot 4294967295 signature_id 17 :
      (!simulation.context, !simulation.class_handle<@I>) -> i64 {
        is_final = false, is_pure = true, is_static = false,
        is_task = false, is_virtual = true
      }
  }
}

// -----

module {
  // expected-error @below {{interface I contains a non-dense method ordinal set}}
  simulation.design @sparse_interface_method_ordinals {
    simulation.scope.decl 0
    simulation.class.decl @I id 1 {
      is_abstract = true, is_final = false, is_interface = true
    }
    simulation.class.method @I_f of @I slot 4294967295 signature_id 17
        interface_ordinal 1 :
      (!simulation.context, !simulation.class_handle<@I>) -> i64 {
        is_final = false, is_pure = true, is_static = false,
        is_task = false, is_virtual = true
      }
  }
}

// -----

module {
  simulation.design @duplicate_interface_method_ordinals {
    simulation.scope.decl 0
    simulation.class.decl @I id 1 {
      is_abstract = true, is_final = false, is_interface = true
    }
    simulation.class.method @I_f of @I slot 4294967295 signature_id 17
        interface_ordinal 0 :
      (!simulation.context, !simulation.class_handle<@I>) -> i64 {
        is_final = false, is_pure = true, is_static = false,
        is_task = false, is_virtual = true
      }
    // expected-error @below {{owner interface contains a duplicate method ordinal}}
    simulation.class.method @I_g of @I slot 4294967295 signature_id 18
        interface_ordinal 0 :
      (!simulation.context, !simulation.class_handle<@I>) -> i64 {
        is_final = false, is_pure = true, is_static = false,
        is_task = false, is_virtual = true
      }
  }
}

// -----

module {
  simulation.design @class_method_with_interface_ordinal {
    simulation.scope.decl 0
    simulation.class.decl @C id 1 {
      is_abstract = true, is_final = false, is_interface = false
    }
    // expected-error @below {{only interface virtual methods may have an interface ordinal}}
    simulation.class.method @C_f of @C slot 0 signature_id 17
        interface_ordinal 0 :
      (!simulation.context, !simulation.class_handle<@C>) -> i64 {
        is_final = false, is_pure = true, is_static = false,
        is_task = false, is_virtual = true
      }
  }
}

// -----

module {
  simulation.design @bad_class_method_slot {
    simulation.scope.decl 0
    simulation.code_unit.decl 1 in 0 function hierarchy "C.f"
    simulation.class.decl @C id 1 {
      is_abstract = false, is_final = false, is_interface = false
    }
    // expected-error @below {{non-interface virtual methods cannot use the interface dispatch slot}}
    simulation.class.method @C_f of @C slot 4294967295 signature_id 17
        implemented_by @f :
      (!simulation.context, !simulation.class_handle<@C>) -> i64 {
        is_final = false, is_pure = false, is_static = false,
        is_task = false, is_virtual = true
      }
    simulation.func @f(
        %ctx: !simulation.context {simulation.capture_kind = 0 : i32},
        %this: !simulation.class_handle<@C>
          {simulation.capture_kind = 1 : i32}) -> i64
        attributes {code_unit_id = 1 : i64, entry_kind = 8 : i32} {
      %zero = arith.constant 0 : i64
      simulation.return %zero : i64
    }
  }
}

// -----

module {
  simulation.design @oversized_class_method_slot {
    simulation.scope.decl 0
    simulation.class.decl @C id 1 {
      is_abstract = true, is_final = false, is_interface = false
    }
    // expected-error @below {{virtual-method slot exceeds the 32-bit dispatch ABI}}
    simulation.class.method @C_f of @C slot 4294967296 signature_id 17 :
      (!simulation.context, !simulation.class_handle<@C>) -> () {
        is_final = false, is_pure = true, is_static = false,
        is_task = true, is_virtual = true
      }
  }
}

// -----

module {
  simulation.design @duplicate_class_method_slot {
    simulation.scope.decl 0
    simulation.class.decl @C id 1 {
      is_abstract = true, is_final = false, is_interface = false
    }
    simulation.class.method @C_first of @C slot 0 signature_id 17 :
      (!simulation.context, !simulation.class_handle<@C>) -> i64 {
        is_final = false, is_pure = true, is_static = false,
        is_task = false, is_virtual = true
      }
    // expected-error @below {{owner class contains a duplicate virtual-method slot}}
    simulation.class.method @C_second of @C slot 0 signature_id 18 :
      (!simulation.context, !simulation.class_handle<@C>) -> i64 {
        is_final = false, is_pure = true, is_static = false,
        is_task = false, is_virtual = true
      }
  }
}

// -----

module {
  simulation.design @task_method_non_task_implementation {
    simulation.scope.decl 0
    simulation.code_unit.decl 1 in 0 function hierarchy "C.run"
    simulation.class.decl @C id 1 {
      is_abstract = false, is_final = false, is_interface = false
    }
    // expected-error @below {{implementation entry kind does not match the method kind}}
    simulation.class.method @C_run of @C slot 0 signature_id 17
        implemented_by @not_task :
      (!simulation.context, !simulation.class_handle<@C>) -> () {
        is_final = false, is_pure = false, is_static = false,
        is_task = true, is_virtual = true
      }
    simulation.func @not_task(
        %ctx: !simulation.context {simulation.capture_kind = 0 : i32},
        %this: !simulation.class_handle<@C>
          {simulation.capture_kind = 1 : i32})
        attributes {code_unit_id = 1 : i64, entry_kind = 8 : i32} {
      simulation.return
    }
  }
}

// -----

module {
  simulation.design @virtual_task_bad_arguments {
    simulation.scope.decl 0
    simulation.code_unit.decl 1 in 0 task hierarchy "C.run"
    simulation.code_unit.decl 2 in 0 initial hierarchy "root"
    simulation.class.decl @C id 1 {
      is_abstract = false, is_final = false, is_interface = false
    }
    simulation.class.method @C_run of @C slot 0 signature_id 17
        implemented_by @run :
      (!simulation.context, !simulation.class_handle<@C>, i64) -> () {
        is_final = false, is_pure = false, is_static = false,
        is_task = true, is_virtual = true
      }
    simulation.func @run(
        %ctx: !simulation.context {simulation.capture_kind = 0 : i32},
        %this: !simulation.class_handle<@C>
          {simulation.capture_kind = 1 : i32},
        %value: i64 {simulation.capture_kind = 1 : i32})
        attributes {code_unit_id = 1 : i64, entry_kind = 12 : i32} {
      simulation.return
    }
    simulation.func @root(
        %ctx: !simulation.context {simulation.capture_kind = 0 : i32})
        attributes {code_unit_id = 2 : i64, entry_kind = 1 : i32} {
      %object = simulation.class.alloc %ctx :
        !simulation.context -> !simulation.class_handle<@C>
      // expected-error @below {{receiver or arguments do not match the virtual task}}
      simulation.class.virtual_task_call
        %object[@C_run] slot 0 signature_id 17
        () arguments 0 to ^done :
        (!simulation.class_handle<@C>) -> ()
    ^done:
      simulation.return
    }
  }
}

// -----

module {
  simulation.design @virtual_task_method_kind {
    simulation.scope.decl 0
    simulation.code_unit.decl 1 in 0 function hierarchy "C.run"
    simulation.code_unit.decl 2 in 0 initial hierarchy "root"
    simulation.class.decl @C id 1 {
      is_abstract = false, is_final = false, is_interface = false
    }
    simulation.class.method @C_run of @C slot 0 signature_id 17
        implemented_by @run :
      (!simulation.context, !simulation.class_handle<@C>) -> () {
        is_final = false, is_pure = false, is_static = false,
        is_task = false, is_virtual = true
      }
    simulation.func @run(
        %ctx: !simulation.context {simulation.capture_kind = 0 : i32},
        %this: !simulation.class_handle<@C>
          {simulation.capture_kind = 1 : i32})
        attributes {code_unit_id = 1 : i64, entry_kind = 8 : i32} {
      simulation.return
    }
    simulation.func @root(
        %ctx: !simulation.context {simulation.capture_kind = 0 : i32})
        attributes {code_unit_id = 2 : i64, entry_kind = 1 : i32} {
      %object = simulation.class.null : !simulation.class_handle<@C>
      // expected-error @below {{method must name a compatible virtual task slot}}
      simulation.class.virtual_task_call
        %object[@C_run] slot 0 signature_id 17
        () arguments 0 to ^done :
        (!simulation.class_handle<@C>) -> ()
    ^done:
      simulation.return
    }
  }
}

// -----

module {
  simulation.design @virtual_task_observer {
    simulation.scope.decl 0
    simulation.code_unit.decl 1 in 0 task hierarchy "C.run"
    simulation.code_unit.decl 2 in 0 observer hierarchy "observe"
    simulation.class.decl @C id 1 {
      is_abstract = false, is_final = false, is_interface = false
    }
    simulation.class.method @C_run of @C slot 0 signature_id 17
        implemented_by @run :
      (!simulation.context, !simulation.class_handle<@C>) -> () {
        is_final = false, is_pure = false, is_static = false,
        is_task = true, is_virtual = true
      }
    simulation.func @run(
        %ctx: !simulation.context {simulation.capture_kind = 0 : i32},
        %this: !simulation.class_handle<@C>
          {simulation.capture_kind = 1 : i32})
        attributes {code_unit_id = 1 : i64, entry_kind = 12 : i32} {
      simulation.return
    }
    simulation.func @observe(
        %ctx: !simulation.context {simulation.capture_kind = 0 : i32}) -> i1
        attributes {code_unit_id = 2 : i64, entry_kind = 14 : i32} {
      %object = simulation.class.null : !simulation.class_handle<@C>
      // expected-error @below {{task calls are not permitted in an observer entry}}
      simulation.class.virtual_task_call
        %object[@C_run] slot 0 signature_id 17
        () arguments 0 to ^done :
        (!simulation.class_handle<@C>) -> ()
    ^done:
      %false = arith.constant false
      simulation.return %false : i1
    }
  }
}

// -----

module {
  simulation.design @direct_call_to_task {
    simulation.scope.decl 0
    simulation.code_unit.decl 1 in 0 task hierarchy "C.run"
    simulation.code_unit.decl 2 in 0 initial hierarchy "root"
    simulation.class.decl @C id 1 {
      is_abstract = false, is_final = false, is_interface = false
    }
    simulation.func @run(
        %ctx: !simulation.context {simulation.capture_kind = 0 : i32},
        %this: !simulation.class_handle<@C>
          {simulation.capture_kind = 1 : i32})
        attributes {code_unit_id = 1 : i64, entry_kind = 12 : i32} {
      simulation.return
    }
    simulation.func @root(
        %ctx: !simulation.context {simulation.capture_kind = 0 : i32})
        attributes {code_unit_id = 2 : i64, entry_kind = 1 : i32} {
      %object = simulation.class.null : !simulation.class_handle<@C>
      // expected-error @below {{must reference a zero-time function implementation}}
      simulation.class.direct_call @run %object() :
        (!simulation.class_handle<@C>) -> ()
      simulation.return
    }
  }
}

// -----

module {
  simulation.design @static_bitstream_member {
    simulation.scope.decl 0
    simulation.class.decl @C id 1 {
      is_abstract = false, is_final = false, is_interface = false
    }
    // expected-error @below {{static properties cannot be object bit-stream members}}
    simulation.class.field @C_value of @C at 0 : i8 {
      is_static = true, is_weak = false,
      simulation.class_bitstream_member,
      simulation.class_bitstream_visibility = #simulation.member_visibility<public>
    }
  }
}

// -----

module {
  simulation.design @invalid_bitstream_visibility {
    simulation.scope.decl 0
    simulation.class.decl @C id 1 {
      is_abstract = false, is_final = false, is_interface = false
    }
    // expected-error @below {{class bit-stream visibility must be a typed public/protected/local value}}
    simulation.class.field @C_value of @C at 0 : i8 {
      is_static = false, is_weak = false,
      simulation.class_bitstream_member,
      simulation.class_bitstream_visibility = 3 : i32
    }
  }
}

// -----

module {
  simulation.design @unpaired_bitstream_metadata {
    simulation.scope.decl 0
    simulation.class.decl @C id 1 {
      is_abstract = false, is_final = false, is_interface = false
    }
    // expected-error @below {{class bit-stream member and visibility metadata must be paired}}
    simulation.class.field @C_value of @C at 0 : i8 {
      is_static = false, is_weak = false,
      simulation.class_bitstream_member
    }
  }
}

// -----

module {
  simulation.design @unmarked_object_plan {
    simulation.scope.decl 0
    simulation.code_unit.decl 1 in 0 function hierarchy "cast"
    simulation.class.decl @__obelisk_class_s3_C id 1 {
      is_abstract = false, is_final = true, is_interface = false
    }
    simulation.func private @cast(
        %ctx: !simulation.context {simulation.capture_kind = 0 : i32},
        %source: !simulation.class_handle<@__obelisk_class_s3_C>
            {simulation.capture_kind = 1 : i32})
        attributes {entry_kind = 8 : i32, code_unit_id = 1 : i64} {
      // expected-error @+2 {{object plan requires the class bit-stream source feature marker}}
      %result, %matched, %watch =
          simulation.recursive.export_bitstream %source {
            plan = array<i64: 9702691408, 1, 64, 0,
                5, 0, 3235077357463657086, 0, 64, 0>
          } : (!simulation.class_handle<@__obelisk_class_s3_C>) ->
              (i8, i1, !simulation.managed_watch)
      simulation.return
    }
  }
}
