// RUN: obelisk-opt --split-input-file --verify-diagnostics %s

module {
  obelisk_sim.design @cycle {
    obelisk_sim.scope.decl 0
    // expected-error @below {{class inheritance contains a cycle}}
    obelisk_sim.class.decl @A id 1 extends @B {
      is_abstract = false, is_final = false, is_interface = false
    }
    obelisk_sim.class.decl @B id 2 extends @A {
      is_abstract = false, is_final = false, is_interface = false
    }
  }
}

// -----

module {
  obelisk_sim.design @bad_weak_specialization {
    obelisk_sim.scope.decl 0
    obelisk_sim.code_unit.decl 1 in 0 root_initializer hierarchy "root"
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
    obelisk_sim.func @root(
        %ctx: !obelisk_sim.context {obelisk_sim.capture_kind = 0 : i32})
        attributes {code_unit_id = 1 : i64, entry_kind = 0 : i32} {
      %other = obelisk_sim.class.alloc %ctx :
        !obelisk_sim.context -> !obelisk_sim.class_handle<@Other>
      // expected-error @below {{referent type does not match the weak_reference specialization}}
      %weak = obelisk_sim.weak.create %ctx, %other :
        !obelisk_sim.context, !obelisk_sim.class_handle<@Other> ->
        !obelisk_sim.class_handle<@Weak>
      obelisk_sim.return
    }
  }
}

// -----

module {
  obelisk_sim.design @bad_interface {
    obelisk_sim.scope.decl 0
    obelisk_sim.class.decl @NotInterface id 1 {
      is_abstract = false, is_final = false, is_interface = false
    }
    // expected-error @below {{implements list references a non-interface class}}
    obelisk_sim.class.decl @C id 2 implements [@NotInterface] {
      is_abstract = false, is_final = false, is_interface = false
    }
  }
}

// -----

module {
  obelisk_sim.design @abstract_alloc {
    obelisk_sim.scope.decl 0
    obelisk_sim.code_unit.decl 1 in 0 root_initializer hierarchy "root"
    obelisk_sim.class.decl @Abstract id 1 {
      is_abstract = true, is_final = false, is_interface = false
    }
    obelisk_sim.func @root(
        %ctx: !obelisk_sim.context {obelisk_sim.capture_kind = 0 : i32})
        attributes {code_unit_id = 1 : i64, entry_kind = 0 : i32} {
      // expected-error @below {{cannot allocate an abstract or interface class}}
      %object = obelisk_sim.class.alloc %ctx :
        !obelisk_sim.context -> !obelisk_sim.class_handle<@Abstract>
      obelisk_sim.return
    }
  }
}

// -----

module {
  obelisk_sim.design @bad_field_ref {
    obelisk_sim.scope.decl 0
    obelisk_sim.code_unit.decl 1 in 0 function hierarchy "C.f"
    obelisk_sim.class.decl @C id 1 {
      is_abstract = false, is_final = false, is_interface = false
    }
    obelisk_sim.class.field @C_value of @C at 0 offset 8 : i64 {
      is_static = false, is_weak = false
    }
    obelisk_sim.func @f(
        %ctx: !obelisk_sim.context {obelisk_sim.capture_kind = 0 : i32},
        %this: !obelisk_sim.class_handle<@C>
          {obelisk_sim.capture_kind = 1 : i32})
        attributes {code_unit_id = 1 : i64, entry_kind = 8 : i32} {
      // expected-error @below {{managed reference type does not match the property}}
      %field = obelisk_sim.class.field_ref %this[@C_value] :
        !obelisk_sim.class_handle<@C> ->
        !obelisk_sim.managed_ref<i32, @C>
      obelisk_sim.return
    }
  }
}

// -----

module {
  obelisk_sim.design @bad_managed_nba {
    obelisk_sim.scope.decl 0
    obelisk_sim.code_unit.decl 1 in 0 function hierarchy "C.f"
    obelisk_sim.class.decl @C id 1 {
      is_abstract = false, is_final = false, is_interface = false
    }
    obelisk_sim.class.field @C_value of @C at 0 offset 8 : i64 {
      is_static = false, is_weak = false
    }
    obelisk_sim.func @f(
        %ctx: !obelisk_sim.context {obelisk_sim.capture_kind = 0 : i32},
        %this: !obelisk_sim.class_handle<@C>
          {obelisk_sim.capture_kind = 1 : i32})
        attributes {code_unit_id = 1 : i64, entry_kind = 8 : i32} {
      %field = obelisk_sim.class.field_ref %this[@C_value] :
        !obelisk_sim.class_handle<@C> ->
        !obelisk_sim.managed_ref<i64, @C>
      %value = arith.constant 1 : i32
      // expected-error @below {{value type must match the referenced element}}
      obelisk_sim.managed.nba.enqueue %value to %field :
        (i32, !obelisk_sim.managed_ref<i64, @C>) -> ()
      obelisk_sim.return
    }
  }
}

// -----

module {
  obelisk_sim.design @bad_virtual_signature {
    obelisk_sim.scope.decl 0
    obelisk_sim.code_unit.decl 1 in 0 function hierarchy "C.f"
    obelisk_sim.code_unit.decl 2 in 0 root_initializer hierarchy "root"
    obelisk_sim.class.decl @C id 1 {
      is_abstract = false, is_final = false, is_interface = false
    }
    obelisk_sim.class.method @C_f of @C slot 0 signature_id 17
        implemented_by @f :
        (!obelisk_sim.context, !obelisk_sim.class_handle<@C>) -> i64 {
      is_final = false, is_pure = false, is_static = false,
      is_task = false, is_virtual = true
    }
    obelisk_sim.func @f(
        %ctx: !obelisk_sim.context {obelisk_sim.capture_kind = 0 : i32},
        %this: !obelisk_sim.class_handle<@C>
          {obelisk_sim.capture_kind = 1 : i32}) -> i64
        attributes {code_unit_id = 1 : i64, entry_kind = 8 : i32} {
      %zero = arith.constant 0 : i64
      obelisk_sim.return %zero : i64
    }
    obelisk_sim.func @root(
        %ctx: !obelisk_sim.context {obelisk_sim.capture_kind = 0 : i32})
        attributes {code_unit_id = 2 : i64, entry_kind = 0 : i32} {
      %object = obelisk_sim.class.alloc %ctx :
        !obelisk_sim.context -> !obelisk_sim.class_handle<@C>
      // expected-error @below {{signature ID does not match the virtual method}}
      %value = obelisk_sim.class.virtual_call
        %object[@C_f] slot 0 signature_id 18() :
        (!obelisk_sim.class_handle<@C>) -> i64
      obelisk_sim.return
    }
  }
}

// -----

module {
  obelisk_sim.design @bad_argument_ref_conversion {
    obelisk_sim.scope.decl 0
    obelisk_sim.code_unit.decl 1 in 0 function hierarchy "f"
    obelisk_sim.func @f(
        %ctx: !obelisk_sim.context {obelisk_sim.capture_kind = 0 : i32},
        %storage: !obelisk_sim.ref<i64>
          {obelisk_sim.capture_kind = 1 : i32})
        attributes {code_unit_id = 1 : i64, entry_kind = 1 : i32} {
      // expected-error @below {{input and result element types must match}}
      %reference = obelisk_sim.argument_ref.from_ref %storage :
        !obelisk_sim.ref<i64> -> !obelisk_sim.argument_ref<i32>
      obelisk_sim.return
    }
  }
}

// -----

module {
  obelisk_sim.design @bad_argument_ref_load {
    obelisk_sim.scope.decl 0
    obelisk_sim.code_unit.decl 1 in 0 function hierarchy "f"
    obelisk_sim.func @f(
        %ctx: !obelisk_sim.context {obelisk_sim.capture_kind = 0 : i32},
        %reference: !obelisk_sim.argument_ref<i64>
          {obelisk_sim.capture_kind = 1 : i32})
        attributes {code_unit_id = 1 : i64, entry_kind = 1 : i32} {
      // expected-error @below {{result type must match the referenced element}}
      %value = obelisk_sim.argument_ref.load %reference :
        !obelisk_sim.argument_ref<i64> -> i32
      obelisk_sim.return
    }
  }
}

// -----

module {
  obelisk_sim.design @bad_managed_null {
    obelisk_sim.scope.decl 0
    obelisk_sim.code_unit.decl 1 in 0 function hierarchy "f"
    obelisk_sim.func @f(
        %ctx: !obelisk_sim.context {obelisk_sim.capture_kind = 0 : i32})
        attributes {code_unit_id = 1 : i64, entry_kind = 1 : i32} {
      // expected-error @below {{result must be a non-class managed handle type}}
      %null = obelisk_sim.managed.null : f64
      obelisk_sim.return
    }
  }
}
