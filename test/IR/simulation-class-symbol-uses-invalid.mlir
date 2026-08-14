// RUN: obelisk-opt --split-input-file --verify-diagnostics %s

// Every diagnostic below is produced by a SymbolUserOpInterface
// verifySymbolUses hook rather than by the operation's own verify(). Those
// hooks only run as part of the enclosing symbol table's verification, so a
// missing interface declaration would silently drop the check instead of
// failing anything. These cases pin that wiring.

module {
  obelisk_sim.design @unknown_field_owner {
    obelisk_sim.scope.decl 0
    obelisk_sim.class.decl @C id 1 {
      is_abstract = false, is_final = false, is_interface = false
    }
    // expected-error @below {{references an unknown owner class}}
    obelisk_sim.class.field @C_value of @Missing at 0 offset 0 : i64 {
      is_static = false, is_weak = false
    }
  }
}

// -----

module {
  obelisk_sim.design @unknown_weak_referent {
    obelisk_sim.scope.decl 0
    // expected-error @below {{weak wrapper references an unknown referent class}}
    obelisk_sim.class.decl @W id 1 {
      is_abstract = false, is_final = false, is_interface = false,
      weak_referent = @Missing
    }
  }
}

// -----

module {
  obelisk_sim.design @unknown_alloc_class {
    obelisk_sim.scope.decl 0
    obelisk_sim.code_unit.decl 1 in 0 root_initializer hierarchy "__obelisk_root"
    obelisk_sim.func @__obelisk_root(
        %ctx: !obelisk_sim.context {obelisk_sim.capture_kind = 0 : i32})
        attributes {code_unit_id = 1 : i64, entry_kind = 0 : i32} {
      // expected-error @below {{result type references an unknown class}}
      %object = obelisk_sim.class.alloc %ctx :
        !obelisk_sim.context -> !obelisk_sim.class_handle<@Missing>
      obelisk_sim.return
    }
  }
}

// -----

module {
  obelisk_sim.design @abstract_alloc {
    obelisk_sim.scope.decl 0
    obelisk_sim.code_unit.decl 1 in 0 root_initializer hierarchy "__obelisk_root"
    obelisk_sim.class.decl @A id 1 {
      is_abstract = true, is_final = false, is_interface = false
    }
    obelisk_sim.func @__obelisk_root(
        %ctx: !obelisk_sim.context {obelisk_sim.capture_kind = 0 : i32})
        attributes {code_unit_id = 1 : i64, entry_kind = 0 : i32} {
      // expected-error @below {{cannot allocate an abstract or interface class}}
      %object = obelisk_sim.class.alloc %ctx :
        !obelisk_sim.context -> !obelisk_sim.class_handle<@A>
      obelisk_sim.return
    }
  }
}

// -----

module {
  obelisk_sim.design @unknown_field_reference {
    obelisk_sim.scope.decl 0
    obelisk_sim.code_unit.decl 1 in 0 root_initializer hierarchy "__obelisk_root"
    obelisk_sim.class.decl @C id 1 {
      is_abstract = false, is_final = false, is_interface = false
    }
    obelisk_sim.func @__obelisk_root(
        %ctx: !obelisk_sim.context {obelisk_sim.capture_kind = 0 : i32})
        attributes {code_unit_id = 1 : i64, entry_kind = 0 : i32} {
      %object = obelisk_sim.class.alloc %ctx :
        !obelisk_sim.context -> !obelisk_sim.class_handle<@C>
      // expected-error @below {{references an unknown class property}}
      %field = obelisk_sim.class.field_ref %object[@Missing] :
        !obelisk_sim.class_handle<@C> ->
        !obelisk_sim.managed_ref<i64, @C>
      obelisk_sim.return
    }
  }
}

// -----

module {
  obelisk_sim.design @static_field_reference {
    obelisk_sim.scope.decl 0
    obelisk_sim.code_unit.decl 1 in 0 root_initializer hierarchy "__obelisk_root"
    obelisk_sim.class.decl @C id 1 {
      is_abstract = false, is_final = false, is_interface = false
    }
    obelisk_sim.class.field @C_value of @C at 0 : i64 {
      is_static = true, is_weak = false
    }
    obelisk_sim.func @__obelisk_root(
        %ctx: !obelisk_sim.context {obelisk_sim.capture_kind = 0 : i32})
        attributes {code_unit_id = 1 : i64, entry_kind = 0 : i32} {
      %object = obelisk_sim.class.alloc %ctx :
        !obelisk_sim.context -> !obelisk_sim.class_handle<@C>
      // expected-error @below {{cannot form an instance reference to a static property}}
      %field = obelisk_sim.class.field_ref %object[@C_value] :
        !obelisk_sim.class_handle<@C> ->
        !obelisk_sim.managed_ref<i64, @C>
      obelisk_sim.return
    }
  }
}

// -----

module {
  obelisk_sim.design @foreign_field_reference {
    obelisk_sim.scope.decl 0
    obelisk_sim.code_unit.decl 1 in 0 root_initializer hierarchy "__obelisk_root"
    obelisk_sim.class.decl @C id 1 {
      is_abstract = false, is_final = false, is_interface = false
    }
    obelisk_sim.class.decl @D id 2 {
      is_abstract = false, is_final = false, is_interface = false
    }
    obelisk_sim.class.field @D_value of @D at 0 offset 0 : i64 {
      is_static = false, is_weak = false
    }
    obelisk_sim.func @__obelisk_root(
        %ctx: !obelisk_sim.context {obelisk_sim.capture_kind = 0 : i32})
        attributes {code_unit_id = 1 : i64, entry_kind = 0 : i32} {
      %object = obelisk_sim.class.alloc %ctx :
        !obelisk_sim.context -> !obelisk_sim.class_handle<@C>
      // expected-error @below {{property is not a member of the receiver class}}
      %field = obelisk_sim.class.field_ref %object[@D_value] :
        !obelisk_sim.class_handle<@C> ->
        !obelisk_sim.managed_ref<i64, @C>
      obelisk_sim.return
    }
  }
}

// -----

module {
  obelisk_sim.design @unknown_is_instance_target {
    obelisk_sim.scope.decl 0
    obelisk_sim.code_unit.decl 1 in 0 root_initializer hierarchy "__obelisk_root"
    obelisk_sim.class.decl @C id 1 {
      is_abstract = false, is_final = false, is_interface = false
    }
    obelisk_sim.func @__obelisk_root(
        %ctx: !obelisk_sim.context {obelisk_sim.capture_kind = 0 : i32})
        attributes {code_unit_id = 1 : i64, entry_kind = 0 : i32} {
      %object = obelisk_sim.class.alloc %ctx :
        !obelisk_sim.context -> !obelisk_sim.class_handle<@C>
      // expected-error @below {{references an unknown target class}}
      %test = obelisk_sim.class.is_instance %object is @Missing :
        !obelisk_sim.class_handle<@C>
      obelisk_sim.return
    }
  }
}

// -----

module {
  obelisk_sim.design @unrelated_cast {
    obelisk_sim.scope.decl 0
    obelisk_sim.code_unit.decl 1 in 0 root_initializer hierarchy "__obelisk_root"
    obelisk_sim.class.decl @C id 1 {
      is_abstract = false, is_final = false, is_interface = false
    }
    obelisk_sim.class.decl @D id 2 {
      is_abstract = false, is_final = false, is_interface = false
    }
    obelisk_sim.func @__obelisk_root(
        %ctx: !obelisk_sim.context {obelisk_sim.capture_kind = 0 : i32})
        attributes {code_unit_id = 1 : i64, entry_kind = 0 : i32} {
      %object = obelisk_sim.class.alloc %ctx :
        !obelisk_sim.context -> !obelisk_sim.class_handle<@C>
      // expected-error @below {{cast classes are unrelated}}
      %cast = obelisk_sim.class.cast %object :
        !obelisk_sim.class_handle<@C> to !obelisk_sim.class_handle<@D>
      obelisk_sim.return
    }
  }
}

// -----

module {
  obelisk_sim.design @unknown_direct_callee {
    obelisk_sim.scope.decl 0
    obelisk_sim.code_unit.decl 1 in 0 root_initializer hierarchy "__obelisk_root"
    obelisk_sim.class.decl @C id 1 {
      is_abstract = false, is_final = false, is_interface = false
    }
    obelisk_sim.func @__obelisk_root(
        %ctx: !obelisk_sim.context {obelisk_sim.capture_kind = 0 : i32})
        attributes {code_unit_id = 1 : i64, entry_kind = 0 : i32} {
      %object = obelisk_sim.class.alloc %ctx :
        !obelisk_sim.context -> !obelisk_sim.class_handle<@C>
      // expected-error @below {{references an unknown method implementation}}
      obelisk_sim.class.direct_call @Missing %object() :
        (!obelisk_sim.class_handle<@C>) -> ()
      obelisk_sim.return
    }
  }
}

// -----

module {
  obelisk_sim.design @unknown_cast_class {
    obelisk_sim.scope.decl 0
    obelisk_sim.code_unit.decl 1 in 0 root_initializer hierarchy "__obelisk_root"
    obelisk_sim.class.decl @C id 1 {
      is_abstract = false, is_final = false, is_interface = false
    }
    obelisk_sim.func @__obelisk_root(
        %ctx: !obelisk_sim.context {obelisk_sim.capture_kind = 0 : i32})
        attributes {code_unit_id = 1 : i64, entry_kind = 0 : i32} {
      %object = obelisk_sim.class.alloc %ctx :
        !obelisk_sim.context -> !obelisk_sim.class_handle<@C>
      // expected-error @below {{cast references an unknown class}}
      %cast = obelisk_sim.class.cast %object :
        !obelisk_sim.class_handle<@C> to !obelisk_sim.class_handle<@Missing>
      obelisk_sim.return
    }
  }
}

// -----

module {
  obelisk_sim.design @mismatched_field_ref_type {
    obelisk_sim.scope.decl 0
    obelisk_sim.code_unit.decl 1 in 0 root_initializer hierarchy "__obelisk_root"
    obelisk_sim.class.decl @C id 1 {
      is_abstract = false, is_final = false, is_interface = false
    }
    obelisk_sim.class.field @C_value of @C at 0 offset 0 : i64 {
      is_static = false, is_weak = false
    }
    obelisk_sim.func @__obelisk_root(
        %ctx: !obelisk_sim.context {obelisk_sim.capture_kind = 0 : i32})
        attributes {code_unit_id = 1 : i64, entry_kind = 0 : i32} {
      %object = obelisk_sim.class.alloc %ctx :
        !obelisk_sim.context -> !obelisk_sim.class_handle<@C>
      // expected-error @below {{managed reference type does not match the property}}
      %field = obelisk_sim.class.field_ref %object[@C_value] :
        !obelisk_sim.class_handle<@C> ->
        !obelisk_sim.managed_ref<i32, @C>
      obelisk_sim.return
    }
  }
}

// -----

module {
  obelisk_sim.design @randc_state_not_owned {
    obelisk_sim.scope.decl 0
    obelisk_sim.class.decl @C id 1 {
      is_abstract = false, is_final = false, is_interface = false
    }
    // expected-error @below {{randc state must name distinct owned instance i64 fields}}
    obelisk_sim.class.field @C_value of @C at 0 : i8 {
      is_static = false, is_weak = false,
      obelisk_sim.random_mode_index = 0 : i64,
      obelisk_sim.random_variable_kind = 2 : i32,
      obelisk_sim.random_variable_signed = false,
      obelisk_sim.random_cycle_key_field = @Missing_key,
      obelisk_sim.random_cycle_position_field = @Missing_position
    }
  }
}

// -----

module {
  obelisk_sim.design @unknown_random_mode_field {
    obelisk_sim.scope.decl 0
    // expected-error @below {{random mode field must name an instance i64 field owned by the root class}}
    obelisk_sim.class.decl @C id 1 {
      is_abstract = false, is_final = false, is_interface = false,
      obelisk_sim.random_mode_field = @Missing
    }
  }
}

// -----

module {
  obelisk_sim.design @weak_wrapper_not_declared {
    obelisk_sim.scope.decl 0
    obelisk_sim.code_unit.decl 1 in 0 root_initializer hierarchy "__obelisk_root"
    obelisk_sim.class.decl @Referent id 1 {
      is_abstract = false, is_final = false, is_interface = false
    }
    obelisk_sim.class.decl @NotAWrapper id 2 {
      is_abstract = false, is_final = false, is_interface = false
    }
    obelisk_sim.func @__obelisk_root(
        %ctx: !obelisk_sim.context {obelisk_sim.capture_kind = 0 : i32})
        attributes {code_unit_id = 1 : i64, entry_kind = 0 : i32} {
      %referent = obelisk_sim.class.alloc %ctx :
        !obelisk_sim.context -> !obelisk_sim.class_handle<@Referent>
      // expected-error @below {{result must be a declared weak_reference wrapper}}
      %weak = obelisk_sim.weak.create %ctx, %referent :
        !obelisk_sim.context, !obelisk_sim.class_handle<@Referent> ->
        !obelisk_sim.class_handle<@NotAWrapper>
      obelisk_sim.return
    }
  }
}

// -----

module {
  obelisk_sim.design @weak_get_result_mismatch {
    obelisk_sim.scope.decl 0
    obelisk_sim.code_unit.decl 1 in 0 root_initializer hierarchy "__obelisk_root"
    obelisk_sim.class.decl @Referent id 1 {
      is_abstract = false, is_final = false, is_interface = false
    }
    obelisk_sim.class.decl @Other id 2 {
      is_abstract = false, is_final = false, is_interface = false
    }
    obelisk_sim.class.decl @Weak id 3 {
      is_abstract = false, is_final = false, is_interface = false,
      weak_referent = @Referent
    }
    obelisk_sim.func @__obelisk_root(
        %ctx: !obelisk_sim.context {obelisk_sim.capture_kind = 0 : i32})
        attributes {code_unit_id = 1 : i64, entry_kind = 0 : i32} {
      %referent = obelisk_sim.class.alloc %ctx :
        !obelisk_sim.context -> !obelisk_sim.class_handle<@Referent>
      %weak = obelisk_sim.weak.create %ctx, %referent :
        !obelisk_sim.context, !obelisk_sim.class_handle<@Referent> ->
        !obelisk_sim.class_handle<@Weak>
      // expected-error @below {{result type does not match the weak_reference specialization}}
      %got = obelisk_sim.weak.get %weak :
        !obelisk_sim.class_handle<@Weak> -> !obelisk_sim.class_handle<@Other>
      obelisk_sim.return
    }
  }
}

// -----

module {
  obelisk_sim.design @weak_clear_not_wrapper {
    obelisk_sim.scope.decl 0
    obelisk_sim.code_unit.decl 1 in 0 root_initializer hierarchy "__obelisk_root"
    obelisk_sim.class.decl @C id 1 {
      is_abstract = false, is_final = false, is_interface = false
    }
    obelisk_sim.func @__obelisk_root(
        %ctx: !obelisk_sim.context {obelisk_sim.capture_kind = 0 : i32})
        attributes {code_unit_id = 1 : i64, entry_kind = 0 : i32} {
      %object = obelisk_sim.class.alloc %ctx :
        !obelisk_sim.context -> !obelisk_sim.class_handle<@C>
      // expected-error @below {{operand must be a declared weak_reference wrapper}}
      obelisk_sim.weak.clear %object : !obelisk_sim.class_handle<@C>
      obelisk_sim.return
    }
  }
}

// -----

module {
  obelisk_sim.design @unknown_virtual_slot {
    obelisk_sim.scope.decl 0
    obelisk_sim.code_unit.decl 1 in 0 root_initializer hierarchy "__obelisk_root"
    obelisk_sim.class.decl @C id 1 {
      is_abstract = false, is_final = false, is_interface = false
    }
    obelisk_sim.func @__obelisk_root(
        %ctx: !obelisk_sim.context {obelisk_sim.capture_kind = 0 : i32})
        attributes {code_unit_id = 1 : i64, entry_kind = 0 : i32} {
      %object = obelisk_sim.class.alloc %ctx :
        !obelisk_sim.context -> !obelisk_sim.class_handle<@C>
      // expected-error @below {{references an unknown or incompatible virtual slot}}
      %result = obelisk_sim.class.virtual_call
        %object[@Missing] slot 0 signature_id 17() :
        (!obelisk_sim.class_handle<@C>) -> i64
      obelisk_sim.return
    }
  }
}

// -----

module {
  obelisk_sim.design @virtual_call_signature_mismatch {
    obelisk_sim.scope.decl 0
    obelisk_sim.code_unit.decl 1 in 0 root_initializer hierarchy "__obelisk_root"
    obelisk_sim.code_unit.decl 2 in 0 function hierarchy "C.get"
    obelisk_sim.class.decl @C id 1 {
      is_abstract = false, is_final = false, is_interface = false
    }
    obelisk_sim.class.method @C_get of @C slot 0 signature_id 17
        implemented_by @c_get :
      (!obelisk_sim.context, !obelisk_sim.class_handle<@C>) -> i64 {
        is_final = false, is_pure = false, is_static = false,
        is_task = false, is_virtual = true
      }
    obelisk_sim.func @c_get(
        %ctx: !obelisk_sim.context {obelisk_sim.capture_kind = 0 : i32},
        %this: !obelisk_sim.class_handle<@C>
          {obelisk_sim.capture_kind = 1 : i32}) -> i64
        attributes {code_unit_id = 2 : i64, entry_kind = 8 : i32} {
      %zero = arith.constant 0 : i64
      obelisk_sim.return %zero : i64
    }
    obelisk_sim.func @__obelisk_root(
        %ctx: !obelisk_sim.context {obelisk_sim.capture_kind = 0 : i32})
        attributes {code_unit_id = 1 : i64, entry_kind = 0 : i32} {
      %object = obelisk_sim.class.alloc %ctx :
        !obelisk_sim.context -> !obelisk_sim.class_handle<@C>
      // expected-error @below {{signature ID does not match the virtual method}}
      %result = obelisk_sim.class.virtual_call
        %object[@C_get] slot 0 signature_id 99() :
        (!obelisk_sim.class_handle<@C>) -> i64
      obelisk_sim.return
    }
  }
}

// -----

module {
  obelisk_sim.design @virtual_call_operand_mismatch {
    obelisk_sim.scope.decl 0
    obelisk_sim.code_unit.decl 1 in 0 root_initializer hierarchy "__obelisk_root"
    obelisk_sim.code_unit.decl 2 in 0 function hierarchy "C.get"
    obelisk_sim.class.decl @C id 1 {
      is_abstract = false, is_final = false, is_interface = false
    }
    obelisk_sim.class.method @C_get of @C slot 0 signature_id 17
        implemented_by @c_get :
      (!obelisk_sim.context, !obelisk_sim.class_handle<@C>) -> i64 {
        is_final = false, is_pure = false, is_static = false,
        is_task = false, is_virtual = true
      }
    obelisk_sim.func @c_get(
        %ctx: !obelisk_sim.context {obelisk_sim.capture_kind = 0 : i32},
        %this: !obelisk_sim.class_handle<@C>
          {obelisk_sim.capture_kind = 1 : i32}) -> i64
        attributes {code_unit_id = 2 : i64, entry_kind = 8 : i32} {
      %zero = arith.constant 0 : i64
      obelisk_sim.return %zero : i64
    }
    obelisk_sim.func @__obelisk_root(
        %ctx: !obelisk_sim.context {obelisk_sim.capture_kind = 0 : i32})
        attributes {code_unit_id = 1 : i64, entry_kind = 0 : i32} {
      %object = obelisk_sim.class.alloc %ctx :
        !obelisk_sim.context -> !obelisk_sim.class_handle<@C>
      // expected-error @below {{operands or results do not match the method slot}}
      %result = obelisk_sim.class.virtual_call
        %object[@C_get] slot 0 signature_id 17() :
        (!obelisk_sim.class_handle<@C>) -> i32
      obelisk_sim.return
    }
  }
}

// -----

module {
  obelisk_sim.design @direct_call_operand_mismatch {
    obelisk_sim.scope.decl 0
    obelisk_sim.code_unit.decl 1 in 0 root_initializer hierarchy "__obelisk_root"
    obelisk_sim.code_unit.decl 2 in 0 function hierarchy "C.get"
    obelisk_sim.class.decl @C id 1 {
      is_abstract = false, is_final = false, is_interface = false
    }
    obelisk_sim.func @c_get(
        %ctx: !obelisk_sim.context {obelisk_sim.capture_kind = 0 : i32},
        %this: !obelisk_sim.class_handle<@C>
          {obelisk_sim.capture_kind = 1 : i32}) -> i64
        attributes {code_unit_id = 2 : i64, entry_kind = 8 : i32} {
      %zero = arith.constant 0 : i64
      obelisk_sim.return %zero : i64
    }
    obelisk_sim.func @__obelisk_root(
        %ctx: !obelisk_sim.context {obelisk_sim.capture_kind = 0 : i32})
        attributes {code_unit_id = 1 : i64, entry_kind = 0 : i32} {
      %object = obelisk_sim.class.alloc %ctx :
        !obelisk_sim.context -> !obelisk_sim.class_handle<@C>
      // expected-error @below {{operands or results do not match the method}}
      %result = obelisk_sim.class.direct_call @c_get %object() :
        (!obelisk_sim.class_handle<@C>) -> i32
      obelisk_sim.return
    }
  }
}

// -----

module {
  obelisk_sim.design @duplicate_constraint_blocks {
    obelisk_sim.scope.decl 0
    obelisk_sim.class.decl @C id 1 {
      is_abstract = false, is_final = false, is_interface = false
    }
    // expected-error @below {{constraint-block references contain a duplicate}}
    obelisk_sim.random.constraint_template @constraints of @C attributes {
      constraint_blocks = [
        #obelisk_sim.random_constraint_block_reference<
          kind = object_block, index = 0 : i32>,
        #obelisk_sim.random_constraint_block_reference<
          kind = object_block, index = 0 : i32>
      ]
    } {
      %true = arith.constant true
      obelisk_sim.random.hard_constraint %true block 0
    }
  }
}

// -----

module {
  obelisk_sim.design @unknown_constraint_block_storage {
    obelisk_sim.scope.decl 0
    obelisk_sim.class.decl @C id 1 {
      is_abstract = false, is_final = false, is_interface = false
    }
    // expected-error @below {{constraint-block reference names unknown storage ID}}
    obelisk_sim.random.constraint_template @constraints of @C attributes {
      constraint_blocks = [
        #obelisk_sim.random_constraint_block_reference<
          kind = storage, storage = 99 : i64>
      ]
    } {
      %true = arith.constant true
      obelisk_sim.random.hard_constraint %true block 0
    }
  }
}

// -----

module {
  obelisk_sim.design @constraint_template_block_arguments {
    obelisk_sim.scope.decl 0
    obelisk_sim.class.decl @C id 1 {
      is_abstract = false, is_final = false, is_interface = false
    }
    // expected-error @below {{dataflow block cannot have arguments}}
    obelisk_sim.random.constraint_template @constraints of @C attributes {
      constraint_blocks = [
        #obelisk_sim.random_constraint_block_reference<
          kind = object_block, index = 0 : i32>
      ]
    } {
    ^entry(%arg: i1):
      obelisk_sim.random.hard_constraint %arg block 0
    }
  }
}
