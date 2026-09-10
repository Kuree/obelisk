// RUN: obelisk-opt %s --split-input-file --verify-diagnostics -o /dev/null

module {
  obelisk_sim.design @missing_direction {
    obelisk_sim.vpi_definition.decl @cell type 32 name "cell"
    // expected-error @+1 {{vpiIODecl member requires a direction}}
    obelisk_sim.vpi_definition_member.decl @a of @cell type 28 ordinal 0 name "a"
  }
}

// -----

module {
  obelisk_sim.design @duplicate_member_ordinal {
    obelisk_sim.scope.decl 0 hierarchy "$root"
    obelisk_sim.vpi_definition.decl @cell type 32 name "cell"
    obelisk_sim.vpi_definition_member.decl @a of @cell type 28 ordinal 0 name "a" direction input
    // expected-error @+1 {{duplicates member ordinal 0 in the same VPI definition}}
    obelisk_sim.vpi_definition_member.decl @b of @cell type 28 ordinal 0 name "b" direction output
  }
}

// -----

module {
  // expected-error @+1 {{VPI definition @cell member ordinals must be dense declaration order}}
  obelisk_sim.design @non_dense_member_ordinal {
    obelisk_sim.scope.decl 0 hierarchy "$root"
    obelisk_sim.vpi_definition.decl @cell type 32 name "cell"
    obelisk_sim.vpi_definition_member.decl @a of @cell type 28 ordinal 1 name "a" direction input
  }
}

// -----

module {
  obelisk_sim.design @duplicate_specialization_member {
    obelisk_sim.scope.decl 0 hierarchy "$root"
    obelisk_sim.vpi_definition.decl @cell type 32 name "cell"
    obelisk_sim.vpi_definition_member.decl @a of @cell type 28 ordinal 0 name "a" direction input
    obelisk_sim.vpi_definition_specialization.decl @spec of @cell
    obelisk_sim.vpi_definition_member.specialize @spec member @a type
        #obelisk_sim.vpi_type<kind = logic, isSigned = false,
          isFourState = true, range = [0, 0], children = [], childNames = []>
    // expected-error @+1 {{duplicates a member binding in the same VPI specialization}}
    obelisk_sim.vpi_definition_member.specialize @spec member @a type
        #obelisk_sim.vpi_type<kind = logic, isSigned = false,
          isFourState = true, range = [0, 0], children = [], childNames = []>
  }
}

// -----

module {
  obelisk_sim.design @unknown_member_definition {
    // expected-error @+1 {{references an unknown VPI definition}}
    obelisk_sim.vpi_definition_member.decl @a of @missing type 28 ordinal 0 name "a" direction input
  }
}

// -----

module {
  obelisk_sim.design @unknown_specialization_definition {
    // expected-error @+1 {{references an unknown VPI definition}}
    obelisk_sim.vpi_definition_specialization.decl @spec of @missing
  }
}

// -----

module {
  obelisk_sim.design @mismatched_specialization_member {
    obelisk_sim.vpi_definition.decl @left type 32 name "left"
    obelisk_sim.vpi_definition.decl @right type 32 name "right"
    obelisk_sim.vpi_definition_member.decl @a of @left type 28 ordinal 0 name "a" direction input
    obelisk_sim.vpi_definition_specialization.decl @right_spec of @right
    // expected-error @+1 {{member and specialization reference different VPI definitions}}
    obelisk_sim.vpi_definition_member.specialize @right_spec member @a type
        #obelisk_sim.vpi_type<kind = logic, isSigned = false,
          isFourState = true, range = [0, 0], children = [], childNames = []>
  }
}

// -----

module {
  obelisk_sim.design @scope_specialization_without_definition {
    obelisk_sim.vpi_definition.decl @cell type 32 name "cell"
    obelisk_sim.vpi_definition_specialization.decl @spec of @cell
    obelisk_sim.scope.decl 0 hierarchy "$root"
    // expected-error @+1 {{VPI specialization requires a VPI definition}}
    obelisk_sim.scope.decl 1 parent 0 hierarchy "top" vpi_kind 32 specialization @spec
  }
}

// -----

module {
  obelisk_sim.design @incomplete_specialization {
    obelisk_sim.scope.decl 0 hierarchy "$root"
    obelisk_sim.vpi_definition.decl @cell type 32 name "cell"
    obelisk_sim.vpi_definition_member.decl @a of @cell type 28 ordinal 0 name "a" direction input
    obelisk_sim.vpi_definition_member.decl @b of @cell type 28 ordinal 1 name "b" direction output
    // expected-error @+1 {{must bind every member in its VPI definition (expected 2, got 1)}}
    obelisk_sim.vpi_definition_specialization.decl @spec of @cell
    obelisk_sim.vpi_definition_member.specialize @spec member @a type
        #obelisk_sim.vpi_type<kind = logic, isSigned = false,
          isFourState = true, range = [0, 0], children = [], childNames = []>
  }
}

// -----

module {
  obelisk_sim.design @missing_scope_specialization {
    obelisk_sim.scope.decl 0 hierarchy "$root"
    obelisk_sim.vpi_definition.decl @cell type 32 name "cell"
    obelisk_sim.vpi_definition_member.decl @a of @cell type 28 ordinal 0 name "a" direction input
    // expected-error @+1 {{VPI definition with members requires a specialization}}
    obelisk_sim.scope.decl 1 parent 0 hierarchy "top" vpi_kind 32 definition @cell
  }
}

// -----

module {
  obelisk_sim.design @unknown_binding_scope {
    obelisk_sim.scope.decl 0 hierarchy "$root"
    obelisk_sim.vpi_definition.decl @cell type 32 name "cell"
    obelisk_sim.vpi_definition_member.decl @a of @cell type 28 ordinal 0
        name "a" direction input
    obelisk_sim.storage.decl 0 in 0 : i1 design hierarchy "a"
    // expected-error @+1 {{references an unknown scope ID}}
    obelisk_sim.vpi_definition_member.bind scope 1 member @a expr
        <kind = storage, id = 0 : i64>
  }
}

// -----

module {
  obelisk_sim.design @binding_definition_mismatch {
    obelisk_sim.scope.decl 0 hierarchy "$root"
    obelisk_sim.vpi_definition.decl @left type 32 name "left"
    obelisk_sim.vpi_definition.decl @right type 32 name "right"
    obelisk_sim.vpi_definition_member.decl @a of @right type 28 ordinal 0
        name "a" direction input
    obelisk_sim.scope.decl 1 parent 0 hierarchy "top" vpi_kind 32
        definition @left
    obelisk_sim.storage.decl 0 in 1 : i1 design hierarchy "top.a" {
      vpi_type = #obelisk_sim.vpi_type<kind = bit, isSigned = false,
          isFourState = false, range = [0, 0], children = [], childNames = []>
    }
    // expected-error @+1 {{member does not belong to the scope's VPI definition}}
    obelisk_sim.vpi_definition_member.bind scope 1 member @a expr
        <kind = storage, id = 0 : i64>
  }
}

// -----

module {
  obelisk_sim.design @unknown_expression_target {
    obelisk_sim.scope.decl 0 hierarchy "$root"
    obelisk_sim.vpi_definition.decl @cell type 32 name "cell"
    obelisk_sim.vpi_definition_member.decl @a of @cell type 28 ordinal 0
        name "a" direction input
    obelisk_sim.vpi_definition_specialization.decl @spec of @cell
    obelisk_sim.vpi_definition_member.specialize @spec member @a type
        #obelisk_sim.vpi_type<kind = bit, isSigned = false,
          isFourState = false, range = [0, 0], children = [], childNames = []>
    obelisk_sim.scope.decl 1 parent 0 hierarchy "top" vpi_kind 32
        definition @cell specialization @spec
    // expected-error @+1 {{references an unknown expression storage ID 99}}
    obelisk_sim.vpi_definition_member.bind scope 1 member @a expr
        <kind = storage, id = 99 : i64>
  }
}

// -----

module {
  obelisk_sim.design @statement_expression_target {
    obelisk_sim.scope.decl 0 hierarchy "$root"
    obelisk_sim.vpi_definition.decl @cell type 32 name "cell"
    obelisk_sim.vpi_definition_member.decl @a of @cell type 28 ordinal 0
        name "a" direction input
    obelisk_sim.vpi_definition_specialization.decl @spec of @cell
    obelisk_sim.vpi_definition_member.specialize @spec member @a type
        #obelisk_sim.vpi_type<kind = bit, isSigned = false,
          isFourState = false, range = [0, 0], children = [], childNames = []>
    obelisk_sim.scope.decl 1 parent 0 hierarchy "top" vpi_kind 32
        definition @cell specialization @spec
    // expected-error @+1 {{expression endpoint must be whole storage or a declared net}}
    obelisk_sim.vpi_definition_member.bind scope 1 member @a expr
        <kind = statement, id = 0 : i64>
  }
}

// -----

module {
  obelisk_sim.design @cross_scope_expression_target {
    obelisk_sim.scope.decl 0 hierarchy "$root"
    obelisk_sim.vpi_definition.decl @cell type 32 name "cell"
    obelisk_sim.vpi_definition_member.decl @a of @cell type 28 ordinal 0
        name "a" direction input
    obelisk_sim.vpi_definition_specialization.decl @spec of @cell
    obelisk_sim.vpi_definition_member.specialize @spec member @a type
        #obelisk_sim.vpi_type<kind = bit, isSigned = false,
          isFourState = false, range = [0, 0], children = [], childNames = []>
    obelisk_sim.scope.decl 1 parent 0 hierarchy "left" vpi_kind 32
        definition @cell specialization @spec
    obelisk_sim.scope.decl 2 parent 0 hierarchy "right" vpi_kind 32
        definition @cell specialization @spec
    obelisk_sim.storage.decl 0 in 2 : i1 design hierarchy "right.a" {
      vpi_type = #obelisk_sim.vpi_type<kind = bit, isSigned = false,
          isFourState = false, range = [0, 0], children = [], childNames = []>
    }
    // expected-error @+1 {{non-ref expression endpoint belongs to a different scope}}
    obelisk_sim.vpi_definition_member.bind scope 1 member @a expr
        <kind = storage, id = 0 : i64>
  }
}

// -----

module {
  obelisk_sim.design @expression_target_type_conversion {
    obelisk_sim.scope.decl 0 hierarchy "$root"
    obelisk_sim.vpi_definition.decl @cell type 32 name "cell"
    obelisk_sim.vpi_definition_member.decl @a of @cell type 28 ordinal 0
        name "a" direction input
    obelisk_sim.vpi_definition_specialization.decl @spec of @cell
    obelisk_sim.vpi_definition_member.specialize @spec member @a type
        #obelisk_sim.vpi_type<kind = bit, isSigned = false,
          isFourState = false, range = [0, 0], children = [], childNames = []>
    obelisk_sim.scope.decl 1 parent 0 hierarchy "top" vpi_kind 32
        definition @cell specialization @spec
    obelisk_sim.storage.decl 0 in 1 : !obelisk_sim.logic<1> design
        hierarchy "top.a" {
      vpi_type = #obelisk_sim.vpi_type<kind = logic, isSigned = false,
          isFourState = true, range = [0, 0], children = [], childNames = []>
    }
    obelisk_sim.vpi_definition_member.bind scope 1 member @a expr
        <kind = storage, id = 0 : i64>
  }
}

// -----

module {
  obelisk_sim.design @ref_actual_may_cross_scope {
    obelisk_sim.scope.decl 0 hierarchy "$root"
    obelisk_sim.vpi_definition.decl @cell type 32 name "cell"
    obelisk_sim.vpi_definition_member.decl @a of @cell type 28 ordinal 0
        name "a" direction ref
    obelisk_sim.vpi_definition_specialization.decl @spec of @cell
    obelisk_sim.vpi_definition_member.specialize @spec member @a type
        #obelisk_sim.vpi_type<kind = bit, isSigned = false,
          isFourState = false, range = [0, 0], children = [], childNames = []>
    obelisk_sim.scope.decl 1 parent 0 hierarchy "top" vpi_kind 32
        definition @cell specialization @spec
    obelisk_sim.storage.decl 0 in 0 : i1 design hierarchy "actual" {
      vpi_type = #obelisk_sim.vpi_type<kind = bit, isSigned = false,
          isFourState = false, range = [0, 0], children = [], childNames = []>
    }
    obelisk_sim.vpi_definition_member.bind scope 1 member @a expr
        <kind = storage, id = 0 : i64>
  }
}

// -----

module {
  obelisk_sim.design @ref_actual_type_mismatch {
    obelisk_sim.scope.decl 0 hierarchy "$root"
    obelisk_sim.vpi_definition.decl @cell type 32 name "cell"
    obelisk_sim.vpi_definition_member.decl @a of @cell type 28 ordinal 0
        name "a" direction ref
    obelisk_sim.vpi_definition_specialization.decl @spec of @cell
    obelisk_sim.vpi_definition_member.specialize @spec member @a type
        #obelisk_sim.vpi_type<kind = bit, isSigned = false,
          isFourState = false, range = [0, 0], children = [], childNames = []>
    obelisk_sim.scope.decl 1 parent 0 hierarchy "top" vpi_kind 32
        definition @cell specialization @spec
    obelisk_sim.storage.decl 0 in 0 : i2 design hierarchy "actual" {
      vpi_type = #obelisk_sim.vpi_type<kind = bit, isSigned = false,
          isFourState = false, range = [1, 0], children = [], childNames = []>
    }
    // expected-error @+1 {{ref actual type does not match its specialized member type}}
    obelisk_sim.vpi_definition_member.bind scope 1 member @a expr
        <kind = storage, id = 0 : i64>
  }
}

// -----

module {
  obelisk_sim.design @ref_actual_must_be_variable_storage {
    obelisk_sim.scope.decl 0 hierarchy "$root"
    obelisk_sim.vpi_definition.decl @cell type 32 name "cell"
    obelisk_sim.vpi_definition_member.decl @a of @cell type 28 ordinal 0
        name "a" direction ref
    obelisk_sim.vpi_definition_specialization.decl @spec of @cell
    obelisk_sim.vpi_definition_member.specialize @spec member @a type
        #obelisk_sim.vpi_type<kind = bit, isSigned = false,
          isFourState = false, range = [0, 0], children = [], childNames = []>
    obelisk_sim.scope.decl 1 parent 0 hierarchy "top" vpi_kind 32
        definition @cell specialization @spec
    obelisk_sim.net.decl 0 in 0 : i1 design hierarchy "actual" {
      vpi_type = #obelisk_sim.vpi_type<kind = bit, isSigned = false,
          isFourState = false, range = [0, 0], children = [], childNames = []>
    }
    // expected-error @+1 {{expression endpoint is not legal for a VPI IO declaration}}
    obelisk_sim.vpi_definition_member.bind scope 1 member @a expr
        <kind = net, id = 0 : i64>
  }
}

// -----

module {
  obelisk_sim.design @ref_equivalent_typedef_spellings {
    obelisk_sim.scope.decl 0 hierarchy "$root"
    obelisk_sim.vpi_definition.decl @cell type 32 name "cell"
    obelisk_sim.vpi_definition_member.decl @a of @cell type 28 ordinal 0
        name "a" direction ref
    obelisk_sim.vpi_definition_specialization.decl @spec of @cell
    obelisk_sim.vpi_definition_member.specialize @spec member @a type
        #obelisk_sim.vpi_type<kind = bit, isSigned = false,
          isFourState = false, range = [0, 0], children = [], childNames = [],
          typedefAliases = [@formal_t]>
    obelisk_sim.scope.decl 1 parent 0 hierarchy "top" vpi_kind 32
        definition @cell specialization @spec
    obelisk_sim.vpi_object.anchor @top id 0 type 32 in 1 ordinal 0
        hierarchy "top" debug "top" {
      backing = #obelisk_sim.vpi_backing<kind = scope, id = 1 : i64>
    }
    obelisk_sim.vpi_typespec.decl @formal_t id 0 in 1 owner @top
        hierarchy "top.formal_t" debug "formal_t" {
      target_type = #obelisk_sim.vpi_type<kind = bit, isSigned = false,
          isFourState = false, range = [0, 0], children = [], childNames = [],
          typedefAliases = [@formal_t]>
    }
    obelisk_sim.vpi_typespec.decl @actual_t id 1 in 1 owner @top
        hierarchy "top.actual_t" debug "actual_t" {
      target_type = #obelisk_sim.vpi_type<kind = bit, isSigned = false,
          isFourState = false, range = [0, 0], children = [], childNames = [],
          typedefAliases = [@actual_t]>
    }
    obelisk_sim.storage.decl 0 in 0 : i1 design hierarchy "actual" {
      vpi_type = #obelisk_sim.vpi_type<kind = bit, isSigned = false,
          isFourState = false, range = [0, 0], children = [], childNames = [],
          typedefAliases = [@actual_t]>
    }
    obelisk_sim.vpi_definition_member.bind scope 1 member @a expr
        <kind = storage, id = 0 : i64>
  }
}

// -----

module {
  obelisk_sim.design @ref_virtual_interface_variable {
    obelisk_sim.scope.decl 0 hierarchy "$root"
    obelisk_sim.vpi_definition.decl @cell type 32 name "cell"
    obelisk_sim.vpi_definition_member.decl @a of @cell type 28 ordinal 0
        name "a" direction ref
    obelisk_sim.vpi_definition_specialization.decl @spec of @cell
    obelisk_sim.vpi_definition_member.specialize @spec member @a type
        #obelisk_sim.vpi_type<kind = virtual_interface, isSigned = false,
          isFourState = false, name = "iface_t", symbol = @iface_t,
          modport = "", range = [], children = [], childNames = []>
    obelisk_sim.scope.decl 1 parent 0 hierarchy "top" vpi_kind 32
        definition @cell specialization @spec
    obelisk_sim.vpi_object.anchor @top id 0 type 32 in 1 ordinal 0
        hierarchy "top" debug "top" {
      backing = #obelisk_sim.vpi_backing<kind = scope, id = 1 : i64>
    }
    obelisk_sim.vpi_typespec.decl @iface_t id 0 in 1 owner @top
        hierarchy "iface_t" debug "iface_t" {
      origin = 1 : i32,
      target_type = #obelisk_sim.vpi_type<kind = virtual_interface,
          isSigned = false, isFourState = false, name = "iface_t",
          symbol = @iface_t, modport = "", range = [], children = [],
          childNames = []>
    }
    obelisk_sim.storage.decl 0 in 0
        : !obelisk_sim.virtual_interface<"iface_t", ""> design
        hierarchy "actual" {
      vpi_type = #obelisk_sim.vpi_type<kind = virtual_interface,
          isSigned = false, isFourState = false, name = "iface_t",
          symbol = @iface_t, modport = "", range = [], children = [],
          childNames = []>
    }
    obelisk_sim.vpi_definition_member.bind scope 1 member @a expr
        <kind = storage, id = 0 : i64>
  }
}

// -----

module {
  obelisk_sim.design @duplicate_instance_member_binding {
    obelisk_sim.scope.decl 0 hierarchy "$root"
    obelisk_sim.vpi_definition.decl @cell type 32 name "cell"
    obelisk_sim.vpi_definition_member.decl @a of @cell type 28 ordinal 0
        name "a" direction input
    obelisk_sim.vpi_definition_specialization.decl @spec of @cell
    obelisk_sim.vpi_definition_member.specialize @spec member @a type
        #obelisk_sim.vpi_type<kind = bit, isSigned = false,
          isFourState = false, range = [0, 0], children = [], childNames = []>
    obelisk_sim.scope.decl 1 parent 0 hierarchy "top" vpi_kind 32
        definition @cell specialization @spec
    obelisk_sim.storage.decl 0 in 1 : i1 design hierarchy "top.a" {
      vpi_type = #obelisk_sim.vpi_type<kind = bit, isSigned = false,
          isFourState = false, range = [0, 0], children = [], childNames = []>
    }
    obelisk_sim.vpi_definition_member.bind scope 1 member @a expr
        <kind = storage, id = 0 : i64>
    // expected-error @+1 {{duplicates an instance binding for the same VPI member}}
    obelisk_sim.vpi_definition_member.bind scope 1 member @a expr
        <kind = storage, id = 0 : i64>
  }
}
