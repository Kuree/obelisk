// RUN: obelisk-opt --split-input-file --verify-diagnostics %s

// Every diagnostic below is produced by a SymbolUserOpInterface
// verifySymbolUses hook rather than by the operation's own verify(). Those
// hooks only run as part of the enclosing symbol table's verification, so a
// missing interface declaration would silently drop the check instead of
// failing anything. These cases pin that wiring.

module {
  simulation.design @unknown_field_owner {
    simulation.scope.decl 0
    simulation.class.decl @C id 1 {
      is_abstract = false, is_final = false, is_interface = false
    }
    // expected-error @below {{references an unknown owner class}}
    simulation.class.field @C_value of @Missing at 0 offset 0 : i64 {
      is_static = false, is_weak = false
    }
  }
}

// -----

module {
  simulation.design @unknown_weak_referent {
    simulation.scope.decl 0
    // expected-error @below {{weak wrapper references an unknown referent class}}
    simulation.class.decl @W id 1 {
      is_abstract = false, is_final = false, is_interface = false,
      weak_referent = @Missing
    }
  }
}

// -----

module {
  simulation.design @unknown_alloc_class {
    simulation.scope.decl 0
    simulation.code_unit.decl 1 in 0 root_initializer hierarchy "__obelisk_root"
    simulation.func @__obelisk_root(
        %ctx: !simulation.context {simulation.capture_kind = 0 : i32})
        attributes {code_unit_id = 1 : i64, entry_kind = 0 : i32} {
      // expected-error @below {{result type references an unknown class}}
      %object = simulation.class.alloc %ctx :
        !simulation.context -> !simulation.class_handle<@Missing>
      simulation.return
    }
  }
}

// -----

module {
  simulation.design @abstract_alloc {
    simulation.scope.decl 0
    simulation.code_unit.decl 1 in 0 root_initializer hierarchy "__obelisk_root"
    simulation.class.decl @A id 1 {
      is_abstract = true, is_final = false, is_interface = false
    }
    simulation.func @__obelisk_root(
        %ctx: !simulation.context {simulation.capture_kind = 0 : i32})
        attributes {code_unit_id = 1 : i64, entry_kind = 0 : i32} {
      // expected-error @below {{cannot allocate an abstract or interface class}}
      %object = simulation.class.alloc %ctx :
        !simulation.context -> !simulation.class_handle<@A>
      simulation.return
    }
  }
}

// -----

module {
  simulation.design @unknown_field_reference {
    simulation.scope.decl 0
    simulation.code_unit.decl 1 in 0 root_initializer hierarchy "__obelisk_root"
    simulation.class.decl @C id 1 {
      is_abstract = false, is_final = false, is_interface = false
    }
    simulation.func @__obelisk_root(
        %ctx: !simulation.context {simulation.capture_kind = 0 : i32})
        attributes {code_unit_id = 1 : i64, entry_kind = 0 : i32} {
      %object = simulation.class.alloc %ctx :
        !simulation.context -> !simulation.class_handle<@C>
      // expected-error @below {{references an unknown class property}}
      %field = simulation.class.field_ref %object[@Missing] :
        !simulation.class_handle<@C> ->
        !simulation.managed_ref<i64, @C>
      simulation.return
    }
  }
}

// -----

module {
  simulation.design @static_field_reference {
    simulation.scope.decl 0
    simulation.code_unit.decl 1 in 0 root_initializer hierarchy "__obelisk_root"
    simulation.class.decl @C id 1 {
      is_abstract = false, is_final = false, is_interface = false
    }
    simulation.class.field @C_value of @C at 0 : i64 {
      is_static = true, is_weak = false
    }
    simulation.func @__obelisk_root(
        %ctx: !simulation.context {simulation.capture_kind = 0 : i32})
        attributes {code_unit_id = 1 : i64, entry_kind = 0 : i32} {
      %object = simulation.class.alloc %ctx :
        !simulation.context -> !simulation.class_handle<@C>
      // expected-error @below {{cannot form an instance reference to a static property}}
      %field = simulation.class.field_ref %object[@C_value] :
        !simulation.class_handle<@C> ->
        !simulation.managed_ref<i64, @C>
      simulation.return
    }
  }
}

// -----

module {
  simulation.design @foreign_field_reference {
    simulation.scope.decl 0
    simulation.code_unit.decl 1 in 0 root_initializer hierarchy "__obelisk_root"
    simulation.class.decl @C id 1 {
      is_abstract = false, is_final = false, is_interface = false
    }
    simulation.class.decl @D id 2 {
      is_abstract = false, is_final = false, is_interface = false
    }
    simulation.class.field @D_value of @D at 0 offset 0 : i64 {
      is_static = false, is_weak = false
    }
    simulation.func @__obelisk_root(
        %ctx: !simulation.context {simulation.capture_kind = 0 : i32})
        attributes {code_unit_id = 1 : i64, entry_kind = 0 : i32} {
      %object = simulation.class.alloc %ctx :
        !simulation.context -> !simulation.class_handle<@C>
      // expected-error @below {{property is not a member of the receiver class}}
      %field = simulation.class.field_ref %object[@D_value] :
        !simulation.class_handle<@C> ->
        !simulation.managed_ref<i64, @C>
      simulation.return
    }
  }
}

// -----

module {
  simulation.design @unknown_is_instance_target {
    simulation.scope.decl 0
    simulation.code_unit.decl 1 in 0 root_initializer hierarchy "__obelisk_root"
    simulation.class.decl @C id 1 {
      is_abstract = false, is_final = false, is_interface = false
    }
    simulation.func @__obelisk_root(
        %ctx: !simulation.context {simulation.capture_kind = 0 : i32})
        attributes {code_unit_id = 1 : i64, entry_kind = 0 : i32} {
      %object = simulation.class.alloc %ctx :
        !simulation.context -> !simulation.class_handle<@C>
      // expected-error @below {{references an unknown target class}}
      %test = simulation.class.is_instance %object is @Missing :
        !simulation.class_handle<@C>
      simulation.return
    }
  }
}

// -----

module {
  simulation.design @unrelated_cast {
    simulation.scope.decl 0
    simulation.code_unit.decl 1 in 0 root_initializer hierarchy "__obelisk_root"
    simulation.class.decl @C id 1 {
      is_abstract = false, is_final = false, is_interface = false
    }
    simulation.class.decl @D id 2 {
      is_abstract = false, is_final = false, is_interface = false
    }
    simulation.func @__obelisk_root(
        %ctx: !simulation.context {simulation.capture_kind = 0 : i32})
        attributes {code_unit_id = 1 : i64, entry_kind = 0 : i32} {
      %object = simulation.class.alloc %ctx :
        !simulation.context -> !simulation.class_handle<@C>
      // expected-error @below {{cast classes are unrelated}}
      %cast = simulation.class.cast %object :
        !simulation.class_handle<@C> to !simulation.class_handle<@D>
      simulation.return
    }
  }
}

// -----

module {
  simulation.design @unknown_direct_callee {
    simulation.scope.decl 0
    simulation.code_unit.decl 1 in 0 root_initializer hierarchy "__obelisk_root"
    simulation.class.decl @C id 1 {
      is_abstract = false, is_final = false, is_interface = false
    }
    simulation.func @__obelisk_root(
        %ctx: !simulation.context {simulation.capture_kind = 0 : i32})
        attributes {code_unit_id = 1 : i64, entry_kind = 0 : i32} {
      %object = simulation.class.alloc %ctx :
        !simulation.context -> !simulation.class_handle<@C>
      // expected-error @below {{references an unknown method implementation}}
      simulation.class.direct_call @Missing %object() :
        (!simulation.class_handle<@C>) -> ()
      simulation.return
    }
  }
}

// -----

module {
  simulation.design @unknown_cast_class {
    simulation.scope.decl 0
    simulation.code_unit.decl 1 in 0 root_initializer hierarchy "__obelisk_root"
    simulation.class.decl @C id 1 {
      is_abstract = false, is_final = false, is_interface = false
    }
    simulation.func @__obelisk_root(
        %ctx: !simulation.context {simulation.capture_kind = 0 : i32})
        attributes {code_unit_id = 1 : i64, entry_kind = 0 : i32} {
      %object = simulation.class.alloc %ctx :
        !simulation.context -> !simulation.class_handle<@C>
      // expected-error @below {{cast references an unknown class}}
      %cast = simulation.class.cast %object :
        !simulation.class_handle<@C> to !simulation.class_handle<@Missing>
      simulation.return
    }
  }
}

// -----

module {
  simulation.design @mismatched_field_ref_type {
    simulation.scope.decl 0
    simulation.code_unit.decl 1 in 0 root_initializer hierarchy "__obelisk_root"
    simulation.class.decl @C id 1 {
      is_abstract = false, is_final = false, is_interface = false
    }
    simulation.class.field @C_value of @C at 0 offset 0 : i64 {
      is_static = false, is_weak = false
    }
    simulation.func @__obelisk_root(
        %ctx: !simulation.context {simulation.capture_kind = 0 : i32})
        attributes {code_unit_id = 1 : i64, entry_kind = 0 : i32} {
      %object = simulation.class.alloc %ctx :
        !simulation.context -> !simulation.class_handle<@C>
      // expected-error @below {{managed reference type does not match the property}}
      %field = simulation.class.field_ref %object[@C_value] :
        !simulation.class_handle<@C> ->
        !simulation.managed_ref<i32, @C>
      simulation.return
    }
  }
}

// -----

module {
  simulation.design @randc_state_not_owned {
    simulation.scope.decl 0
    simulation.class.decl @C id 1 {
      is_abstract = false, is_final = false, is_interface = false
    }
    // expected-error @below {{randc state must name distinct owned instance i64 fields}}
    simulation.class.field @C_value of @C at 0 : i8 {
      is_static = false, is_weak = false,
      simulation.random_mode_index = 0 : i64,
      simulation.random_variable_kind = 2 : i32,
      simulation.random_variable_signed = false,
      simulation.random_cycle_key_field = @Missing_key,
      simulation.random_cycle_position_field = @Missing_position
    }
  }
}

// -----

module {
  simulation.design @unknown_random_mode_field {
    simulation.scope.decl 0
    // expected-error @below {{random mode field must name an instance i64 field owned by the root class}}
    simulation.class.decl @C id 1 {
      is_abstract = false, is_final = false, is_interface = false,
      simulation.random_mode_field = @Missing
    }
  }
}

// -----

module {
  simulation.design @weak_wrapper_not_declared {
    simulation.scope.decl 0
    simulation.code_unit.decl 1 in 0 root_initializer hierarchy "__obelisk_root"
    simulation.class.decl @Referent id 1 {
      is_abstract = false, is_final = false, is_interface = false
    }
    simulation.class.decl @NotAWrapper id 2 {
      is_abstract = false, is_final = false, is_interface = false
    }
    simulation.func @__obelisk_root(
        %ctx: !simulation.context {simulation.capture_kind = 0 : i32})
        attributes {code_unit_id = 1 : i64, entry_kind = 0 : i32} {
      %referent = simulation.class.alloc %ctx :
        !simulation.context -> !simulation.class_handle<@Referent>
      // expected-error @below {{result must be a declared weak_reference wrapper}}
      %weak = simulation.weak.create %ctx, %referent :
        !simulation.context, !simulation.class_handle<@Referent> ->
        !simulation.class_handle<@NotAWrapper>
      simulation.return
    }
  }
}

// -----

module {
  simulation.design @weak_get_result_mismatch {
    simulation.scope.decl 0
    simulation.code_unit.decl 1 in 0 root_initializer hierarchy "__obelisk_root"
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
    simulation.func @__obelisk_root(
        %ctx: !simulation.context {simulation.capture_kind = 0 : i32})
        attributes {code_unit_id = 1 : i64, entry_kind = 0 : i32} {
      %referent = simulation.class.alloc %ctx :
        !simulation.context -> !simulation.class_handle<@Referent>
      %weak = simulation.weak.create %ctx, %referent :
        !simulation.context, !simulation.class_handle<@Referent> ->
        !simulation.class_handle<@Weak>
      // expected-error @below {{result type does not match the weak_reference specialization}}
      %got = simulation.weak.get %weak :
        !simulation.class_handle<@Weak> -> !simulation.class_handle<@Other>
      simulation.return
    }
  }
}

// -----

module {
  simulation.design @weak_clear_not_wrapper {
    simulation.scope.decl 0
    simulation.code_unit.decl 1 in 0 root_initializer hierarchy "__obelisk_root"
    simulation.class.decl @C id 1 {
      is_abstract = false, is_final = false, is_interface = false
    }
    simulation.func @__obelisk_root(
        %ctx: !simulation.context {simulation.capture_kind = 0 : i32})
        attributes {code_unit_id = 1 : i64, entry_kind = 0 : i32} {
      %object = simulation.class.alloc %ctx :
        !simulation.context -> !simulation.class_handle<@C>
      // expected-error @below {{operand must be a declared weak_reference wrapper}}
      simulation.weak.clear %object : !simulation.class_handle<@C>
      simulation.return
    }
  }
}

// -----

module {
  simulation.design @unknown_virtual_slot {
    simulation.scope.decl 0
    simulation.code_unit.decl 1 in 0 root_initializer hierarchy "__obelisk_root"
    simulation.class.decl @C id 1 {
      is_abstract = false, is_final = false, is_interface = false
    }
    simulation.func @__obelisk_root(
        %ctx: !simulation.context {simulation.capture_kind = 0 : i32})
        attributes {code_unit_id = 1 : i64, entry_kind = 0 : i32} {
      %object = simulation.class.alloc %ctx :
        !simulation.context -> !simulation.class_handle<@C>
      // expected-error @below {{references an unknown or incompatible virtual slot}}
      %result = simulation.class.virtual_call
        %object[@Missing] slot 0 signature_id 17() :
        (!simulation.class_handle<@C>) -> i64
      simulation.return
    }
  }
}

// -----

module {
  simulation.design @virtual_call_signature_mismatch {
    simulation.scope.decl 0
    simulation.code_unit.decl 1 in 0 root_initializer hierarchy "__obelisk_root"
    simulation.code_unit.decl 2 in 0 function hierarchy "C.get"
    simulation.class.decl @C id 1 {
      is_abstract = false, is_final = false, is_interface = false
    }
    simulation.class.method @C_get of @C slot 0 signature_id 17
        implemented_by @c_get :
      (!simulation.context, !simulation.class_handle<@C>) -> i64 {
        is_final = false, is_pure = false, is_static = false,
        is_task = false, is_virtual = true
      }
    simulation.func @c_get(
        %ctx: !simulation.context {simulation.capture_kind = 0 : i32},
        %this: !simulation.class_handle<@C>
          {simulation.capture_kind = 1 : i32}) -> i64
        attributes {code_unit_id = 2 : i64, entry_kind = 8 : i32} {
      %zero = arith.constant 0 : i64
      simulation.return %zero : i64
    }
    simulation.func @__obelisk_root(
        %ctx: !simulation.context {simulation.capture_kind = 0 : i32})
        attributes {code_unit_id = 1 : i64, entry_kind = 0 : i32} {
      %object = simulation.class.alloc %ctx :
        !simulation.context -> !simulation.class_handle<@C>
      // expected-error @below {{signature ID does not match the virtual method}}
      %result = simulation.class.virtual_call
        %object[@C_get] slot 0 signature_id 99() :
        (!simulation.class_handle<@C>) -> i64
      simulation.return
    }
  }
}

// -----

module {
  simulation.design @virtual_call_operand_mismatch {
    simulation.scope.decl 0
    simulation.code_unit.decl 1 in 0 root_initializer hierarchy "__obelisk_root"
    simulation.code_unit.decl 2 in 0 function hierarchy "C.get"
    simulation.class.decl @C id 1 {
      is_abstract = false, is_final = false, is_interface = false
    }
    simulation.class.method @C_get of @C slot 0 signature_id 17
        implemented_by @c_get :
      (!simulation.context, !simulation.class_handle<@C>) -> i64 {
        is_final = false, is_pure = false, is_static = false,
        is_task = false, is_virtual = true
      }
    simulation.func @c_get(
        %ctx: !simulation.context {simulation.capture_kind = 0 : i32},
        %this: !simulation.class_handle<@C>
          {simulation.capture_kind = 1 : i32}) -> i64
        attributes {code_unit_id = 2 : i64, entry_kind = 8 : i32} {
      %zero = arith.constant 0 : i64
      simulation.return %zero : i64
    }
    simulation.func @__obelisk_root(
        %ctx: !simulation.context {simulation.capture_kind = 0 : i32})
        attributes {code_unit_id = 1 : i64, entry_kind = 0 : i32} {
      %object = simulation.class.alloc %ctx :
        !simulation.context -> !simulation.class_handle<@C>
      // expected-error @below {{operands or results do not match the method slot}}
      %result = simulation.class.virtual_call
        %object[@C_get] slot 0 signature_id 17() :
        (!simulation.class_handle<@C>) -> i32
      simulation.return
    }
  }
}

// -----

module {
  simulation.design @direct_call_operand_mismatch {
    simulation.scope.decl 0
    simulation.code_unit.decl 1 in 0 root_initializer hierarchy "__obelisk_root"
    simulation.code_unit.decl 2 in 0 function hierarchy "C.get"
    simulation.class.decl @C id 1 {
      is_abstract = false, is_final = false, is_interface = false
    }
    simulation.func @c_get(
        %ctx: !simulation.context {simulation.capture_kind = 0 : i32},
        %this: !simulation.class_handle<@C>
          {simulation.capture_kind = 1 : i32}) -> i64
        attributes {code_unit_id = 2 : i64, entry_kind = 8 : i32} {
      %zero = arith.constant 0 : i64
      simulation.return %zero : i64
    }
    simulation.func @__obelisk_root(
        %ctx: !simulation.context {simulation.capture_kind = 0 : i32})
        attributes {code_unit_id = 1 : i64, entry_kind = 0 : i32} {
      %object = simulation.class.alloc %ctx :
        !simulation.context -> !simulation.class_handle<@C>
      // expected-error @below {{operands or results do not match the method}}
      %result = simulation.class.direct_call @c_get %object() :
        (!simulation.class_handle<@C>) -> i32
      simulation.return
    }
  }
}

// -----

module {
  simulation.design @duplicate_constraint_blocks {
    simulation.scope.decl 0
    simulation.class.decl @C id 1 {
      is_abstract = false, is_final = false, is_interface = false
    }
    // expected-error @below {{constraint-block references contain a duplicate}}
    simulation.random.constraint_template @constraints of @C attributes {
      constraint_blocks = [
        #simulation.random_constraint_block_reference<
          kind = object_block, index = 0 : i32>,
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
  simulation.design @unknown_constraint_block_storage {
    simulation.scope.decl 0
    simulation.class.decl @C id 1 {
      is_abstract = false, is_final = false, is_interface = false
    }
    // expected-error @below {{constraint-block reference names unknown storage ID}}
    simulation.random.constraint_template @constraints of @C attributes {
      constraint_blocks = [
        #simulation.random_constraint_block_reference<
          kind = storage, storage = 99 : i64>
      ]
    } {
      %true = arith.constant true
      simulation.random.hard_constraint %true block 0
    }
  }
}

// -----

module {
  simulation.design @constraint_template_block_arguments {
    simulation.scope.decl 0
    simulation.class.decl @C id 1 {
      is_abstract = false, is_final = false, is_interface = false
    }
    // expected-error @below {{dataflow block cannot have arguments}}
    simulation.random.constraint_template @constraints of @C attributes {
      constraint_blocks = [
        #simulation.random_constraint_block_reference<
          kind = object_block, index = 0 : i32>
      ]
    } {
    ^entry(%arg: i1):
      simulation.random.hard_constraint %arg block 0
    }
  }
}
