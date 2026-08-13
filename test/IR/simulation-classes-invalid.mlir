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
  obelisk_sim.design @self_interface_cycle {
    obelisk_sim.scope.decl 0
    // expected-error @below {{interface inheritance contains a cycle}}
    obelisk_sim.class.decl @I id 1 implements [@I] {
      is_abstract = true, is_final = false, is_interface = true
    }
  }
}

// -----

module {
  obelisk_sim.design @transitive_interface_cycle {
    obelisk_sim.scope.decl 0
    // expected-error @below {{interface inheritance contains a cycle}}
    obelisk_sim.class.decl @I id 1 implements [@J] {
      is_abstract = true, is_final = false, is_interface = true
    }
    obelisk_sim.class.decl @J id 2 implements [@K] {
      is_abstract = true, is_final = false, is_interface = true
    }
    obelisk_sim.class.decl @K id 3 implements [@I] {
      is_abstract = true, is_final = false, is_interface = true
    }
  }
}

// -----

module {
  obelisk_sim.design @invalid_edge_before_cycle {
    obelisk_sim.scope.decl 0
    obelisk_sim.class.decl @I id 1 implements [@J] {
      is_abstract = true, is_final = false, is_interface = true
    }
    // expected-error @below {{implements list references a non-interface class}}
    obelisk_sim.class.decl @J id 2 implements [@C] {
      is_abstract = true, is_final = false, is_interface = true
    }
    obelisk_sim.class.decl @C id 3 implements [@I] {
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
  obelisk_sim.design @bad_field_watch {
    obelisk_sim.scope.decl 0
    obelisk_sim.code_unit.decl 1 in 0 function hierarchy "C.f"
    obelisk_sim.class.decl @C id 1 {
      is_abstract = false, is_final = false, is_interface = false
    }
    obelisk_sim.func @f(
        %ctx: !obelisk_sim.context {obelisk_sim.capture_kind = 0 : i32},
        %this: !obelisk_sim.class_handle<@C>
          {obelisk_sim.capture_kind = 1 : i32})
        attributes {code_unit_id = 1 : i64, entry_kind = 8 : i32} {
      // expected-error @below {{field watches require a managed reference}}
      %watch = obelisk_sim.managed.watch field %this :
        !obelisk_sim.class_handle<@C>
      obelisk_sim.return
    }
  }
}

// -----

module {
  obelisk_sim.design @bad_container_size_watch {
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
      // expected-error @below {{container-size watches require a dynamic, queue, or associative array handle}}
      %watch = obelisk_sim.managed.watch container_size %field :
        !obelisk_sim.managed_ref<i64, @C>
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

// -----

module {
  obelisk_sim.design @bad_interface_method_slot {
    obelisk_sim.scope.decl 0
    obelisk_sim.class.decl @I id 1 {
      is_abstract = true, is_final = false, is_interface = true
    }
    // expected-error @below {{interface virtual methods require the interface dispatch slot}}
    obelisk_sim.class.method @I_f of @I slot 0 signature_id 17
        interface_ordinal 0 :
      (!obelisk_sim.context, !obelisk_sim.class_handle<@I>) -> i64 {
        is_final = false, is_pure = true, is_static = false,
        is_task = false, is_virtual = true
      }
  }
}

// -----

module {
  obelisk_sim.design @missing_interface_method_ordinal {
    obelisk_sim.scope.decl 0
    obelisk_sim.class.decl @I id 1 {
      is_abstract = true, is_final = false, is_interface = true
    }
    // expected-error @below {{interface virtual methods require a 32-bit interface ordinal}}
    obelisk_sim.class.method @I_f of @I slot 4294967295 signature_id 17 :
      (!obelisk_sim.context, !obelisk_sim.class_handle<@I>) -> i64 {
        is_final = false, is_pure = true, is_static = false,
        is_task = false, is_virtual = true
      }
  }
}

// -----

module {
  // expected-error @below {{interface I contains a non-dense method ordinal set}}
  obelisk_sim.design @sparse_interface_method_ordinals {
    obelisk_sim.scope.decl 0
    obelisk_sim.class.decl @I id 1 {
      is_abstract = true, is_final = false, is_interface = true
    }
    obelisk_sim.class.method @I_f of @I slot 4294967295 signature_id 17
        interface_ordinal 1 :
      (!obelisk_sim.context, !obelisk_sim.class_handle<@I>) -> i64 {
        is_final = false, is_pure = true, is_static = false,
        is_task = false, is_virtual = true
      }
  }
}

// -----

module {
  obelisk_sim.design @duplicate_interface_method_ordinals {
    obelisk_sim.scope.decl 0
    obelisk_sim.class.decl @I id 1 {
      is_abstract = true, is_final = false, is_interface = true
    }
    obelisk_sim.class.method @I_f of @I slot 4294967295 signature_id 17
        interface_ordinal 0 :
      (!obelisk_sim.context, !obelisk_sim.class_handle<@I>) -> i64 {
        is_final = false, is_pure = true, is_static = false,
        is_task = false, is_virtual = true
      }
    // expected-error @below {{owner interface contains a duplicate method ordinal}}
    obelisk_sim.class.method @I_g of @I slot 4294967295 signature_id 18
        interface_ordinal 0 :
      (!obelisk_sim.context, !obelisk_sim.class_handle<@I>) -> i64 {
        is_final = false, is_pure = true, is_static = false,
        is_task = false, is_virtual = true
      }
  }
}

// -----

module {
  obelisk_sim.design @class_method_with_interface_ordinal {
    obelisk_sim.scope.decl 0
    obelisk_sim.class.decl @C id 1 {
      is_abstract = true, is_final = false, is_interface = false
    }
    // expected-error @below {{only interface virtual methods may have an interface ordinal}}
    obelisk_sim.class.method @C_f of @C slot 0 signature_id 17
        interface_ordinal 0 :
      (!obelisk_sim.context, !obelisk_sim.class_handle<@C>) -> i64 {
        is_final = false, is_pure = true, is_static = false,
        is_task = false, is_virtual = true
      }
  }
}

// -----

module {
  obelisk_sim.design @bad_class_method_slot {
    obelisk_sim.scope.decl 0
    obelisk_sim.code_unit.decl 1 in 0 function hierarchy "C.f"
    obelisk_sim.class.decl @C id 1 {
      is_abstract = false, is_final = false, is_interface = false
    }
    // expected-error @below {{non-interface virtual methods cannot use the interface dispatch slot}}
    obelisk_sim.class.method @C_f of @C slot 4294967295 signature_id 17
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
  }
}

// -----

module {
  obelisk_sim.design @oversized_class_method_slot {
    obelisk_sim.scope.decl 0
    obelisk_sim.class.decl @C id 1 {
      is_abstract = true, is_final = false, is_interface = false
    }
    // expected-error @below {{virtual-method slot exceeds the 32-bit dispatch ABI}}
    obelisk_sim.class.method @C_f of @C slot 4294967296 signature_id 17 :
      (!obelisk_sim.context, !obelisk_sim.class_handle<@C>) -> () {
        is_final = false, is_pure = true, is_static = false,
        is_task = true, is_virtual = true
      }
  }
}

// -----

module {
  obelisk_sim.design @duplicate_class_method_slot {
    obelisk_sim.scope.decl 0
    obelisk_sim.class.decl @C id 1 {
      is_abstract = true, is_final = false, is_interface = false
    }
    obelisk_sim.class.method @C_first of @C slot 0 signature_id 17 :
      (!obelisk_sim.context, !obelisk_sim.class_handle<@C>) -> i64 {
        is_final = false, is_pure = true, is_static = false,
        is_task = false, is_virtual = true
      }
    // expected-error @below {{owner class contains a duplicate virtual-method slot}}
    obelisk_sim.class.method @C_second of @C slot 0 signature_id 18 :
      (!obelisk_sim.context, !obelisk_sim.class_handle<@C>) -> i64 {
        is_final = false, is_pure = true, is_static = false,
        is_task = false, is_virtual = true
      }
  }
}

// -----

module {
  obelisk_sim.design @task_method_non_task_implementation {
    obelisk_sim.scope.decl 0
    obelisk_sim.code_unit.decl 1 in 0 function hierarchy "C.run"
    obelisk_sim.class.decl @C id 1 {
      is_abstract = false, is_final = false, is_interface = false
    }
    // expected-error @below {{implementation entry kind does not match the method kind}}
    obelisk_sim.class.method @C_run of @C slot 0 signature_id 17
        implemented_by @not_task :
      (!obelisk_sim.context, !obelisk_sim.class_handle<@C>) -> () {
        is_final = false, is_pure = false, is_static = false,
        is_task = true, is_virtual = true
      }
    obelisk_sim.func @not_task(
        %ctx: !obelisk_sim.context {obelisk_sim.capture_kind = 0 : i32},
        %this: !obelisk_sim.class_handle<@C>
          {obelisk_sim.capture_kind = 1 : i32})
        attributes {code_unit_id = 1 : i64, entry_kind = 8 : i32} {
      obelisk_sim.return
    }
  }
}

// -----

module {
  obelisk_sim.design @virtual_task_bad_arguments {
    obelisk_sim.scope.decl 0
    obelisk_sim.code_unit.decl 1 in 0 task hierarchy "C.run"
    obelisk_sim.code_unit.decl 2 in 0 initial hierarchy "root"
    obelisk_sim.class.decl @C id 1 {
      is_abstract = false, is_final = false, is_interface = false
    }
    obelisk_sim.class.method @C_run of @C slot 0 signature_id 17
        implemented_by @run :
      (!obelisk_sim.context, !obelisk_sim.class_handle<@C>, i64) -> () {
        is_final = false, is_pure = false, is_static = false,
        is_task = true, is_virtual = true
      }
    obelisk_sim.func @run(
        %ctx: !obelisk_sim.context {obelisk_sim.capture_kind = 0 : i32},
        %this: !obelisk_sim.class_handle<@C>
          {obelisk_sim.capture_kind = 1 : i32},
        %value: i64 {obelisk_sim.capture_kind = 1 : i32})
        attributes {code_unit_id = 1 : i64, entry_kind = 12 : i32} {
      obelisk_sim.return
    }
    obelisk_sim.func @root(
        %ctx: !obelisk_sim.context {obelisk_sim.capture_kind = 0 : i32})
        attributes {code_unit_id = 2 : i64, entry_kind = 1 : i32} {
      %object = obelisk_sim.class.alloc %ctx :
        !obelisk_sim.context -> !obelisk_sim.class_handle<@C>
      // expected-error @below {{receiver or arguments do not match the virtual task}}
      obelisk_sim.class.virtual_task_call
        %object[@C_run] slot 0 signature_id 17
        () arguments 0 to ^done :
        (!obelisk_sim.class_handle<@C>) -> ()
    ^done:
      obelisk_sim.return
    }
  }
}

// -----

module {
  obelisk_sim.design @virtual_task_method_kind {
    obelisk_sim.scope.decl 0
    obelisk_sim.code_unit.decl 1 in 0 function hierarchy "C.run"
    obelisk_sim.code_unit.decl 2 in 0 initial hierarchy "root"
    obelisk_sim.class.decl @C id 1 {
      is_abstract = false, is_final = false, is_interface = false
    }
    obelisk_sim.class.method @C_run of @C slot 0 signature_id 17
        implemented_by @run :
      (!obelisk_sim.context, !obelisk_sim.class_handle<@C>) -> () {
        is_final = false, is_pure = false, is_static = false,
        is_task = false, is_virtual = true
      }
    obelisk_sim.func @run(
        %ctx: !obelisk_sim.context {obelisk_sim.capture_kind = 0 : i32},
        %this: !obelisk_sim.class_handle<@C>
          {obelisk_sim.capture_kind = 1 : i32})
        attributes {code_unit_id = 1 : i64, entry_kind = 8 : i32} {
      obelisk_sim.return
    }
    obelisk_sim.func @root(
        %ctx: !obelisk_sim.context {obelisk_sim.capture_kind = 0 : i32})
        attributes {code_unit_id = 2 : i64, entry_kind = 1 : i32} {
      %object = obelisk_sim.class.null : !obelisk_sim.class_handle<@C>
      // expected-error @below {{method must name a compatible virtual task slot}}
      obelisk_sim.class.virtual_task_call
        %object[@C_run] slot 0 signature_id 17
        () arguments 0 to ^done :
        (!obelisk_sim.class_handle<@C>) -> ()
    ^done:
      obelisk_sim.return
    }
  }
}

// -----

module {
  obelisk_sim.design @virtual_task_observer {
    obelisk_sim.scope.decl 0
    obelisk_sim.code_unit.decl 1 in 0 task hierarchy "C.run"
    obelisk_sim.code_unit.decl 2 in 0 observer hierarchy "observe"
    obelisk_sim.class.decl @C id 1 {
      is_abstract = false, is_final = false, is_interface = false
    }
    obelisk_sim.class.method @C_run of @C slot 0 signature_id 17
        implemented_by @run :
      (!obelisk_sim.context, !obelisk_sim.class_handle<@C>) -> () {
        is_final = false, is_pure = false, is_static = false,
        is_task = true, is_virtual = true
      }
    obelisk_sim.func @run(
        %ctx: !obelisk_sim.context {obelisk_sim.capture_kind = 0 : i32},
        %this: !obelisk_sim.class_handle<@C>
          {obelisk_sim.capture_kind = 1 : i32})
        attributes {code_unit_id = 1 : i64, entry_kind = 12 : i32} {
      obelisk_sim.return
    }
    obelisk_sim.func @observe(
        %ctx: !obelisk_sim.context {obelisk_sim.capture_kind = 0 : i32}) -> i1
        attributes {code_unit_id = 2 : i64, entry_kind = 14 : i32} {
      %object = obelisk_sim.class.null : !obelisk_sim.class_handle<@C>
      // expected-error @below {{task calls are not permitted in an observer entry}}
      obelisk_sim.class.virtual_task_call
        %object[@C_run] slot 0 signature_id 17
        () arguments 0 to ^done :
        (!obelisk_sim.class_handle<@C>) -> ()
    ^done:
      %false = arith.constant false
      obelisk_sim.return %false : i1
    }
  }
}

// -----

module {
  obelisk_sim.design @direct_call_to_task {
    obelisk_sim.scope.decl 0
    obelisk_sim.code_unit.decl 1 in 0 task hierarchy "C.run"
    obelisk_sim.code_unit.decl 2 in 0 initial hierarchy "root"
    obelisk_sim.class.decl @C id 1 {
      is_abstract = false, is_final = false, is_interface = false
    }
    obelisk_sim.func @run(
        %ctx: !obelisk_sim.context {obelisk_sim.capture_kind = 0 : i32},
        %this: !obelisk_sim.class_handle<@C>
          {obelisk_sim.capture_kind = 1 : i32})
        attributes {code_unit_id = 1 : i64, entry_kind = 12 : i32} {
      obelisk_sim.return
    }
    obelisk_sim.func @root(
        %ctx: !obelisk_sim.context {obelisk_sim.capture_kind = 0 : i32})
        attributes {code_unit_id = 2 : i64, entry_kind = 1 : i32} {
      %object = obelisk_sim.class.null : !obelisk_sim.class_handle<@C>
      // expected-error @below {{must reference a zero-time function implementation}}
      obelisk_sim.class.direct_call @run %object() :
        (!obelisk_sim.class_handle<@C>) -> ()
      obelisk_sim.return
    }
  }
}
