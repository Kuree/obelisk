// RUN: obelisk-opt %s --split-input-file --verify-diagnostics -o /dev/null

module {
  simulation.design @missing_direction {
    simulation.vpi_definition.decl @cell type 32 name "cell"
    // expected-error @+1 {{vpiIODecl member requires a direction}}
    simulation.vpi_definition_member.decl @a of @cell type 28 ordinal 0 name "a"
  }
}

// -----

module {
  simulation.design @duplicate_member_ordinal {
    simulation.scope.decl 0 hierarchy "$root"
    simulation.vpi_definition.decl @cell type 32 name "cell"
    simulation.vpi_definition_member.decl @a of @cell type 28 ordinal 0 name "a" direction input
    // expected-error @+1 {{duplicates member ordinal 0 in the same VPI definition}}
    simulation.vpi_definition_member.decl @b of @cell type 28 ordinal 0 name "b" direction output
  }
}

// -----

module {
  // expected-error @+1 {{VPI definition @cell member ordinals must be dense declaration order}}
  simulation.design @non_dense_member_ordinal {
    simulation.scope.decl 0 hierarchy "$root"
    simulation.vpi_definition.decl @cell type 32 name "cell"
    simulation.vpi_definition_member.decl @a of @cell type 28 ordinal 1 name "a" direction input
  }
}

// -----

module {
  simulation.design @duplicate_specialization_member {
    simulation.scope.decl 0 hierarchy "$root"
    simulation.vpi_definition.decl @cell type 32 name "cell"
    simulation.vpi_definition_member.decl @a of @cell type 28 ordinal 0 name "a" direction input
    simulation.vpi_definition_specialization.decl @spec of @cell
    simulation.vpi_definition_member.specialize @spec member @a type
        #simulation.vpi_type<kind = logic, isSigned = false,
          isFourState = true, range = [0, 0], children = [], childNames = []>
    // expected-error @+1 {{duplicates a member binding in the same VPI specialization}}
    simulation.vpi_definition_member.specialize @spec member @a type
        #simulation.vpi_type<kind = logic, isSigned = false,
          isFourState = true, range = [0, 0], children = [], childNames = []>
  }
}

// -----

module {
  simulation.design @unknown_member_definition {
    // expected-error @+1 {{references an unknown VPI definition}}
    simulation.vpi_definition_member.decl @a of @missing type 28 ordinal 0 name "a" direction input
  }
}

// -----

module {
  simulation.design @unknown_specialization_definition {
    // expected-error @+1 {{references an unknown VPI definition}}
    simulation.vpi_definition_specialization.decl @spec of @missing
  }
}

// -----

module {
  simulation.design @mismatched_specialization_member {
    simulation.vpi_definition.decl @left type 32 name "left"
    simulation.vpi_definition.decl @right type 32 name "right"
    simulation.vpi_definition_member.decl @a of @left type 28 ordinal 0 name "a" direction input
    simulation.vpi_definition_specialization.decl @right_spec of @right
    // expected-error @+1 {{member and specialization reference different VPI definitions}}
    simulation.vpi_definition_member.specialize @right_spec member @a type
        #simulation.vpi_type<kind = logic, isSigned = false,
          isFourState = true, range = [0, 0], children = [], childNames = []>
  }
}

// -----

module {
  simulation.design @scope_specialization_without_definition {
    simulation.vpi_definition.decl @cell type 32 name "cell"
    simulation.vpi_definition_specialization.decl @spec of @cell
    simulation.scope.decl 0 hierarchy "$root"
    // expected-error @+1 {{VPI specialization requires a VPI definition}}
    simulation.scope.decl 1 parent 0 hierarchy "top" vpi_kind 32 specialization @spec
  }
}

// -----

module {
  simulation.design @incomplete_specialization {
    simulation.scope.decl 0 hierarchy "$root"
    simulation.vpi_definition.decl @cell type 32 name "cell"
    simulation.vpi_definition_member.decl @a of @cell type 28 ordinal 0 name "a" direction input
    simulation.vpi_definition_member.decl @b of @cell type 28 ordinal 1 name "b" direction output
    // expected-error @+1 {{must bind every member in its VPI definition (expected 2, got 1)}}
    simulation.vpi_definition_specialization.decl @spec of @cell
    simulation.vpi_definition_member.specialize @spec member @a type
        #simulation.vpi_type<kind = logic, isSigned = false,
          isFourState = true, range = [0, 0], children = [], childNames = []>
  }
}

// -----

module {
  simulation.design @missing_scope_specialization {
    simulation.scope.decl 0 hierarchy "$root"
    simulation.vpi_definition.decl @cell type 32 name "cell"
    simulation.vpi_definition_member.decl @a of @cell type 28 ordinal 0 name "a" direction input
    // expected-error @+1 {{VPI definition with members requires a specialization}}
    simulation.scope.decl 1 parent 0 hierarchy "top" vpi_kind 32 definition @cell
  }
}

// -----

module {
  simulation.design @negative_binding_scope {
    simulation.scope.decl 0 hierarchy "$root"
    simulation.vpi_definition.decl @cell type 32 name "cell"
    simulation.vpi_definition_member.decl @a of @cell type 28 ordinal 0
        name "a" direction input
    simulation.storage.decl 0 in 0 : i1 design hierarchy "a"
    // expected-error @+1 {{scope ID must be nonnegative}}
    simulation.vpi_definition_member.bind scope -1 member @a expr
        <kind = storage, id = 0 : i64>
  }
}

// -----

module {
  simulation.design @unknown_binding_scope {
    simulation.scope.decl 0 hierarchy "$root"
    simulation.vpi_definition.decl @cell type 32 name "cell"
    simulation.vpi_definition_member.decl @a of @cell type 28 ordinal 0
        name "a" direction input
    simulation.storage.decl 0 in 0 : i1 design hierarchy "a"
    // expected-error @+1 {{references an unknown scope ID}}
    simulation.vpi_definition_member.bind scope 1 member @a expr
        <kind = storage, id = 0 : i64>
  }
}

// -----

module {
  simulation.design @binding_definition_mismatch {
    simulation.scope.decl 0 hierarchy "$root"
    simulation.vpi_definition.decl @left type 32 name "left"
    simulation.vpi_definition.decl @right type 32 name "right"
    simulation.vpi_definition_member.decl @a of @right type 28 ordinal 0
        name "a" direction input
    simulation.scope.decl 1 parent 0 hierarchy "top" vpi_kind 32
        definition @left
    simulation.storage.decl 0 in 1 : i1 design hierarchy "top.a" {
      vpi_type = #simulation.vpi_type<kind = bit, isSigned = false,
          isFourState = false, range = [0, 0], children = [], childNames = []>
    }
    // expected-error @+1 {{member does not belong to the scope's VPI definition}}
    simulation.vpi_definition_member.bind scope 1 member @a expr
        <kind = storage, id = 0 : i64>
  }
}

// -----

module {
  simulation.design @unknown_expression_target {
    simulation.scope.decl 0 hierarchy "$root"
    simulation.vpi_definition.decl @cell type 32 name "cell"
    simulation.vpi_definition_member.decl @a of @cell type 28 ordinal 0
        name "a" direction input
    simulation.vpi_definition_specialization.decl @spec of @cell
    simulation.vpi_definition_member.specialize @spec member @a type
        #simulation.vpi_type<kind = bit, isSigned = false,
          isFourState = false, range = [0, 0], children = [], childNames = []>
    simulation.scope.decl 1 parent 0 hierarchy "top" vpi_kind 32
        definition @cell specialization @spec
    // expected-error @+1 {{references an unknown expression storage ID 99}}
    simulation.vpi_definition_member.bind scope 1 member @a expr
        <kind = storage, id = 99 : i64>
  }
}

// -----

module {
  simulation.design @statement_expression_target {
    simulation.scope.decl 0 hierarchy "$root"
    simulation.vpi_definition.decl @cell type 32 name "cell"
    simulation.vpi_definition_member.decl @a of @cell type 28 ordinal 0
        name "a" direction input
    simulation.vpi_definition_specialization.decl @spec of @cell
    simulation.vpi_definition_member.specialize @spec member @a type
        #simulation.vpi_type<kind = bit, isSigned = false,
          isFourState = false, range = [0, 0], children = [], childNames = []>
    simulation.scope.decl 1 parent 0 hierarchy "top" vpi_kind 32
        definition @cell specialization @spec
    // expected-error @+1 {{expression endpoint must be whole storage or a declared net}}
    simulation.vpi_definition_member.bind scope 1 member @a expr
        <kind = statement, id = 0 : i64>
  }
}

// -----

module {
  simulation.design @cross_scope_expression_target {
    simulation.scope.decl 0 hierarchy "$root"
    simulation.vpi_definition.decl @cell type 32 name "cell"
    simulation.vpi_definition_member.decl @a of @cell type 28 ordinal 0
        name "a" direction input
    simulation.vpi_definition_specialization.decl @spec of @cell
    simulation.vpi_definition_member.specialize @spec member @a type
        #simulation.vpi_type<kind = bit, isSigned = false,
          isFourState = false, range = [0, 0], children = [], childNames = []>
    simulation.scope.decl 1 parent 0 hierarchy "left" vpi_kind 32
        definition @cell specialization @spec
    simulation.scope.decl 2 parent 0 hierarchy "right" vpi_kind 32
        definition @cell specialization @spec
    simulation.storage.decl 0 in 2 : i1 design hierarchy "right.a" {
      vpi_type = #simulation.vpi_type<kind = bit, isSigned = false,
          isFourState = false, range = [0, 0], children = [], childNames = []>
    }
    // expected-error @+1 {{non-ref expression endpoint belongs to a different scope}}
    simulation.vpi_definition_member.bind scope 1 member @a expr
        <kind = storage, id = 0 : i64>
  }
}

// -----

module {
  simulation.design @expression_target_type_conversion {
    simulation.scope.decl 0 hierarchy "$root"
    simulation.vpi_definition.decl @cell type 32 name "cell"
    simulation.vpi_definition_member.decl @a of @cell type 28 ordinal 0
        name "a" direction input
    simulation.vpi_definition_specialization.decl @spec of @cell
    simulation.vpi_definition_member.specialize @spec member @a type
        #simulation.vpi_type<kind = bit, isSigned = false,
          isFourState = false, range = [0, 0], children = [], childNames = []>
    simulation.scope.decl 1 parent 0 hierarchy "top" vpi_kind 32
        definition @cell specialization @spec
    simulation.storage.decl 0 in 1 : !simulation.logic<1> design
        hierarchy "top.a" {
      vpi_type = #simulation.vpi_type<kind = logic, isSigned = false,
          isFourState = true, range = [0, 0], children = [], childNames = []>
    }
    simulation.vpi_definition_member.bind scope 1 member @a expr
        <kind = storage, id = 0 : i64>
  }
}

// -----

module {
  simulation.design @ref_actual_may_cross_scope {
    simulation.scope.decl 0 hierarchy "$root"
    simulation.vpi_definition.decl @cell type 32 name "cell"
    simulation.vpi_definition_member.decl @a of @cell type 28 ordinal 0
        name "a" direction ref
    simulation.vpi_definition_specialization.decl @spec of @cell
    simulation.vpi_definition_member.specialize @spec member @a type
        #simulation.vpi_type<kind = bit, isSigned = false,
          isFourState = false, range = [0, 0], children = [], childNames = []>
    simulation.scope.decl 1 parent 0 hierarchy "top" vpi_kind 32
        definition @cell specialization @spec
    simulation.storage.decl 0 in 0 : i1 design hierarchy "actual" {
      vpi_type = #simulation.vpi_type<kind = bit, isSigned = false,
          isFourState = false, range = [0, 0], children = [], childNames = []>
    }
    simulation.vpi_definition_member.bind scope 1 member @a expr
        <kind = storage, id = 0 : i64>
  }
}

// -----

module {
  simulation.design @ref_actual_type_mismatch {
    simulation.scope.decl 0 hierarchy "$root"
    simulation.vpi_definition.decl @cell type 32 name "cell"
    simulation.vpi_definition_member.decl @a of @cell type 28 ordinal 0
        name "a" direction ref
    simulation.vpi_definition_specialization.decl @spec of @cell
    simulation.vpi_definition_member.specialize @spec member @a type
        #simulation.vpi_type<kind = bit, isSigned = false,
          isFourState = false, range = [0, 0], children = [], childNames = []>
    simulation.scope.decl 1 parent 0 hierarchy "top" vpi_kind 32
        definition @cell specialization @spec
    simulation.storage.decl 0 in 0 : i2 design hierarchy "actual" {
      vpi_type = #simulation.vpi_type<kind = bit, isSigned = false,
          isFourState = false, range = [1, 0], children = [], childNames = []>
    }
    // expected-error @+1 {{ref actual type does not match its specialized member type}}
    simulation.vpi_definition_member.bind scope 1 member @a expr
        <kind = storage, id = 0 : i64>
  }
}

// -----

module {
  simulation.design @ref_actual_must_be_variable_storage {
    simulation.scope.decl 0 hierarchy "$root"
    simulation.vpi_definition.decl @cell type 32 name "cell"
    simulation.vpi_definition_member.decl @a of @cell type 28 ordinal 0
        name "a" direction ref
    simulation.vpi_definition_specialization.decl @spec of @cell
    simulation.vpi_definition_member.specialize @spec member @a type
        #simulation.vpi_type<kind = bit, isSigned = false,
          isFourState = false, range = [0, 0], children = [], childNames = []>
    simulation.scope.decl 1 parent 0 hierarchy "top" vpi_kind 32
        definition @cell specialization @spec
    simulation.net.decl 0 in 0 : i1 design hierarchy "actual" {
      vpi_type = #simulation.vpi_type<kind = bit, isSigned = false,
          isFourState = false, range = [0, 0], children = [], childNames = []>
    }
    // expected-error @+1 {{expression endpoint is not legal for a VPI IO declaration}}
    simulation.vpi_definition_member.bind scope 1 member @a expr
        <kind = net, id = 0 : i64>
  }
}

// -----

module {
  simulation.design @ref_equivalent_typedef_spellings {
    simulation.scope.decl 0 hierarchy "$root"
    simulation.vpi_definition.decl @cell type 32 name "cell"
    simulation.vpi_definition_member.decl @a of @cell type 28 ordinal 0
        name "a" direction ref
    simulation.vpi_definition_specialization.decl @spec of @cell
    simulation.vpi_definition_member.specialize @spec member @a type
        #simulation.vpi_type<kind = bit, isSigned = false,
          isFourState = false, range = [0, 0], children = [], childNames = [],
          typedefAliases = [@formal_t]>
    simulation.scope.decl 1 parent 0 hierarchy "top" vpi_kind 32
        definition @cell specialization @spec
    simulation.vpi_object.anchor @top id 0 type 32 in 1 ordinal 0
        hierarchy "top" debug "top" {
      backing = #simulation.vpi_backing<kind = scope, id = 1 : i64>
    }
    simulation.vpi_typespec.decl @formal_t id 0 in 1 owner @top
        hierarchy "top.formal_t" debug "formal_t" {
      target_type = #simulation.vpi_type<kind = bit, isSigned = false,
          isFourState = false, range = [0, 0], children = [], childNames = [],
          typedefAliases = [@formal_t]>
    }
    simulation.vpi_typespec.decl @actual_t id 1 in 1 owner @top
        hierarchy "top.actual_t" debug "actual_t" {
      target_type = #simulation.vpi_type<kind = bit, isSigned = false,
          isFourState = false, range = [0, 0], children = [], childNames = [],
          typedefAliases = [@actual_t]>
    }
    simulation.storage.decl 0 in 0 : i1 design hierarchy "actual" {
      vpi_type = #simulation.vpi_type<kind = bit, isSigned = false,
          isFourState = false, range = [0, 0], children = [], childNames = [],
          typedefAliases = [@actual_t]>
    }
    simulation.vpi_definition_member.bind scope 1 member @a expr
        <kind = storage, id = 0 : i64>
  }
}

// -----

module {
  simulation.design @ref_virtual_interface_variable {
    simulation.scope.decl 0 hierarchy "$root"
    simulation.vpi_definition.decl @cell type 32 name "cell"
    simulation.vpi_definition_member.decl @a of @cell type 28 ordinal 0
        name "a" direction ref
    simulation.vpi_definition_specialization.decl @spec of @cell
    simulation.vpi_definition_member.specialize @spec member @a type
        #simulation.vpi_type<kind = virtual_interface, isSigned = false,
          isFourState = false, name = "iface_t", symbol = @iface_t,
          modport = "", range = [], children = [], childNames = []>
    simulation.scope.decl 1 parent 0 hierarchy "top" vpi_kind 32
        definition @cell specialization @spec
    simulation.vpi_object.anchor @top id 0 type 32 in 1 ordinal 0
        hierarchy "top" debug "top" {
      backing = #simulation.vpi_backing<kind = scope, id = 1 : i64>
    }
    simulation.vpi_typespec.decl @iface_t id 0 in 1 owner @top
        hierarchy "iface_t" debug "iface_t" {
      origin = 1 : i32,
      target_type = #simulation.vpi_type<kind = virtual_interface,
          isSigned = false, isFourState = false, name = "iface_t",
          symbol = @iface_t, modport = "", range = [], children = [],
          childNames = []>
    }
    simulation.storage.decl 0 in 0
        : !simulation.virtual_interface<"iface_t", ""> design
        hierarchy "actual" {
      vpi_type = #simulation.vpi_type<kind = virtual_interface,
          isSigned = false, isFourState = false, name = "iface_t",
          symbol = @iface_t, modport = "", range = [], children = [],
          childNames = []>
    }
    simulation.vpi_definition_member.bind scope 1 member @a expr
        <kind = storage, id = 0 : i64>
  }
}

// -----

module {
  simulation.design @duplicate_instance_member_binding {
    simulation.scope.decl 0 hierarchy "$root"
    simulation.vpi_definition.decl @cell type 32 name "cell"
    simulation.vpi_definition_member.decl @a of @cell type 28 ordinal 0
        name "a" direction input
    simulation.vpi_definition_specialization.decl @spec of @cell
    simulation.vpi_definition_member.specialize @spec member @a type
        #simulation.vpi_type<kind = bit, isSigned = false,
          isFourState = false, range = [0, 0], children = [], childNames = []>
    simulation.scope.decl 1 parent 0 hierarchy "top" vpi_kind 32
        definition @cell specialization @spec
    simulation.storage.decl 0 in 1 : i1 design hierarchy "top.a" {
      vpi_type = #simulation.vpi_type<kind = bit, isSigned = false,
          isFourState = false, range = [0, 0], children = [], childNames = []>
    }
    simulation.vpi_definition_member.bind scope 1 member @a expr
        <kind = storage, id = 0 : i64>
    // expected-error @+1 {{duplicates an instance binding for the same VPI member}}
    simulation.vpi_definition_member.bind scope 1 member @a expr
        <kind = storage, id = 0 : i64>
  }
}

// -----

module {
  simulation.design @ref_relation_without_binding {
    simulation.scope.decl 0 hierarchy "$root"
    simulation.vpi_definition.decl @cell type 32 name "cell"
    simulation.vpi_definition_member.decl @a of @cell type 28 ordinal 0
        name "a" direction ref
    simulation.vpi_definition_specialization.decl @spec of @cell
    simulation.vpi_definition_member.specialize @spec member @a type
        #simulation.vpi_type<kind = bit, isSigned = false,
          isFourState = false, range = [0, 0], children = [], childNames = []>
    simulation.scope.decl 1 parent 0 hierarchy "top" vpi_kind 32
        definition @cell specialization @spec
    simulation.storage.decl 0 in 1 : i1 design hierarchy "top.a"
    simulation.port.decl 0 in 1 source 0 net = false at 0 : i1 inout
        ordinal 0 hierarchy "top.a" debug "a"
    // expected-error @+1 {{source member has no expression endpoint in this instance}}
    simulation.vpi_definition_member.relation scope 1 member @a
        selector 44 iterate ordinal 0 to <kind = port, id = 0 : i64>
  }
}

// -----

module {
  simulation.design @ref_relation_non_port_target {
    simulation.scope.decl 0 hierarchy "$root"
    simulation.vpi_definition.decl @cell type 32 name "cell"
    simulation.vpi_definition_member.decl @a of @cell type 28 ordinal 0
        name "a" direction ref
    simulation.vpi_definition_specialization.decl @spec of @cell
    simulation.vpi_definition_member.specialize @spec member @a type
        #simulation.vpi_type<kind = bit, isSigned = false,
          isFourState = false, range = [0, 0], children = [], childNames = []>
    simulation.scope.decl 1 parent 0 hierarchy "top" vpi_kind 32
        definition @cell specialization @spec
    simulation.storage.decl 0 in 0 : i1 design hierarchy "actual" {
      vpi_type = #simulation.vpi_type<kind = bit, isSigned = false,
          isFourState = false, range = [0, 0], children = [], childNames = []>
    }
    simulation.vpi_definition_member.bind scope 1 member @a expr
        <kind = storage, id = 0 : i64>
    // expected-error @+1 {{target must be a canonical VPI port}}
    simulation.vpi_definition_member.relation scope 1 member @a
        selector 44 iterate ordinal 0 to <kind = storage, id = 0 : i64>
  }
}

// -----

module {
  simulation.design @ref_relation_illegal_selector {
    simulation.scope.decl 0 hierarchy "$root"
    simulation.vpi_definition.decl @cell type 32 name "cell"
    simulation.vpi_definition_member.decl @a of @cell type 28 ordinal 0
        name "a" direction ref
    simulation.vpi_definition_specialization.decl @spec of @cell
    simulation.vpi_definition_member.specialize @spec member @a type
        #simulation.vpi_type<kind = bit, isSigned = false,
          isFourState = false, range = [0, 0], children = [], childNames = []>
    simulation.scope.decl 1 parent 0 hierarchy "top" vpi_kind 32
        definition @cell specialization @spec
    simulation.storage.decl 0 in 0 : i1 design hierarchy "actual" {
      vpi_type = #simulation.vpi_type<kind = bit, isSigned = false,
          isFourState = false, range = [0, 0], children = [], childNames = []>
    }
    simulation.port.decl 0 in 1 source 0 net = false at 0 : i1 inout
        ordinal 0 hierarchy "top.a" debug "a"
    simulation.vpi_definition_member.bind scope 1 member @a expr
        <kind = storage, id = 0 : i64>
    // expected-error @+1 {{relation is not legal for a generated RefObj traversal}}
    simulation.vpi_definition_member.relation scope 1 member @a
        selector 77 iterate ordinal 0 to <kind = port, id = 0 : i64>
  }
}

// -----

module {
  simulation.design @ref_portinst_same_scope {
    simulation.scope.decl 0 hierarchy "$root"
    simulation.vpi_definition.decl @cell type 32 name "cell"
    simulation.vpi_definition_member.decl @a of @cell type 28 ordinal 0
        name "a" direction ref
    simulation.vpi_definition_specialization.decl @spec of @cell
    simulation.vpi_definition_member.specialize @spec member @a type
        #simulation.vpi_type<kind = bit, isSigned = false,
          isFourState = false, range = [0, 0], children = [], childNames = []>
    simulation.scope.decl 1 parent 0 hierarchy "top" vpi_kind 32
        definition @cell specialization @spec
    simulation.storage.decl 0 in 0 : i1 design hierarchy "actual" {
      vpi_type = #simulation.vpi_type<kind = bit, isSigned = false,
          isFourState = false, range = [0, 0], children = [], childNames = []>
    }
    simulation.port.decl 0 in 1 source 0 net = false at 0 : i1 input
        ordinal 0 hierarchy "top.a" debug "a"
    simulation.vpi_definition_member.bind scope 1 member @a expr
        <kind = storage, id = 0 : i64>
    // expected-error @+1 {{vpiPortInst target must belong to a descendant instance}}
    simulation.vpi_definition_member.relation scope 1 member @a
        selector 98 iterate ordinal 0 to <kind = port, id = 0 : i64>
  }
}

// -----

module {
  simulation.design @ref_owning_port_direction {
    simulation.scope.decl 0 hierarchy "$root"
    simulation.vpi_definition.decl @cell type 32 name "cell"
    simulation.vpi_definition_member.decl @a of @cell type 28 ordinal 0
        name "a" direction ref
    simulation.vpi_definition_specialization.decl @spec of @cell
    simulation.vpi_definition_member.specialize @spec member @a type
        #simulation.vpi_type<kind = bit, isSigned = false,
          isFourState = false, range = [0, 0], children = [], childNames = []>
    simulation.scope.decl 1 parent 0 hierarchy "top" vpi_kind 32
        definition @cell specialization @spec
    simulation.storage.decl 0 in 0 : i1 design hierarchy "actual" {
      vpi_type = #simulation.vpi_type<kind = bit, isSigned = false,
          isFourState = false, range = [0, 0], children = [], childNames = []>
    }
    simulation.port.decl 0 in 1 source 0 net = false at 0 : i1 input
        ordinal 0 hierarchy "top.a" debug "a"
    simulation.vpi_definition_member.bind scope 1 member @a expr
        <kind = storage, id = 0 : i64>
    // expected-error @+1 {{owning ref port must have inout execution direction}}
    simulation.vpi_definition_member.relation scope 1 member @a
        selector 44 iterate ordinal 0 to <kind = port, id = 0 : i64>
  }
}

// -----

module {
  // expected-error @+1 {{VPI definition-member relation ordinals must be dense}}
  simulation.design @ref_relation_non_dense_ordinal {
    simulation.scope.decl 0 hierarchy "$root"
    simulation.vpi_definition.decl @cell type 32 name "cell"
    simulation.vpi_definition_member.decl @a of @cell type 28 ordinal 0
        name "a" direction ref
    simulation.vpi_definition_specialization.decl @spec of @cell
    simulation.vpi_definition_member.specialize @spec member @a type
        #simulation.vpi_type<kind = bit, isSigned = false,
          isFourState = false, range = [0, 0], children = [], childNames = []>
    simulation.scope.decl 1 parent 0 hierarchy "top" vpi_kind 32
        definition @cell specialization @spec
    simulation.storage.decl 0 in 0 : i1 design hierarchy "actual" {
      vpi_type = #simulation.vpi_type<kind = bit, isSigned = false,
          isFourState = false, range = [0, 0], children = [], childNames = []>
    }
    simulation.port.decl 0 in 1 source 0 net = false at 0 : i1 inout
        ordinal 0 hierarchy "top.a" debug "a"
    simulation.vpi_definition_member.bind scope 1 member @a expr
        <kind = storage, id = 0 : i64>
    simulation.vpi_definition_member.relation scope 1 member @a
        selector 44 iterate ordinal 1 to <kind = port, id = 0 : i64>
  }
}

// -----

module {
  simulation.design @ref_relation_unknown_scope {
    simulation.vpi_definition.decl @cell type 32 name "cell"
    simulation.vpi_definition_member.decl @a of @cell type 28 ordinal 0
        name "a" direction ref
    // expected-error @+1 {{references an unknown scope ID}}
    simulation.vpi_definition_member.relation scope 99 member @a
        selector 44 iterate ordinal 0 to <kind = port, id = 0 : i64>
  }
}

// -----

module {
  simulation.design @relation_source_is_not_ref {
    simulation.scope.decl 0 hierarchy "$root"
    simulation.vpi_definition.decl @cell type 32 name "cell"
    simulation.vpi_definition_member.decl @a of @cell type 28 ordinal 0
        name "a" direction input
    simulation.vpi_definition_specialization.decl @spec of @cell
    simulation.vpi_definition_member.specialize @spec member @a type
        #simulation.vpi_type<kind = bit, isSigned = false,
          isFourState = false, range = [0, 0], children = [], childNames = []>
    simulation.scope.decl 1 parent 0 hierarchy "top" vpi_kind 32
        definition @cell specialization @spec
    simulation.storage.decl 0 in 1 : i1 design hierarchy "top.a" {
      vpi_type = #simulation.vpi_type<kind = bit, isSigned = false,
          isFourState = false, range = [0, 0], children = [], childNames = []>
    }
    simulation.port.decl 0 in 1 source 0 net = false at 0 : i1 input
        ordinal 0 hierarchy "top.a" debug "a"
    simulation.vpi_definition_member.bind scope 1 member @a expr
        <kind = storage, id = 0 : i64>
    // expected-error @+1 {{source member does not materialize a RefObj}}
    simulation.vpi_definition_member.relation scope 1 member @a
        selector 44 iterate ordinal 0 to <kind = port, id = 0 : i64>
  }
}

// -----

module {
  simulation.design @ref_relation_unknown_port {
    simulation.scope.decl 0 hierarchy "$root"
    simulation.vpi_definition.decl @cell type 32 name "cell"
    simulation.vpi_definition_member.decl @a of @cell type 28 ordinal 0
        name "a" direction ref
    simulation.vpi_definition_specialization.decl @spec of @cell
    simulation.vpi_definition_member.specialize @spec member @a type
        #simulation.vpi_type<kind = bit, isSigned = false,
          isFourState = false, range = [0, 0], children = [], childNames = []>
    simulation.scope.decl 1 parent 0 hierarchy "top" vpi_kind 32
        definition @cell specialization @spec
    simulation.storage.decl 0 in 0 : i1 design hierarchy "actual" {
      vpi_type = #simulation.vpi_type<kind = bit, isSigned = false,
          isFourState = false, range = [0, 0], children = [], childNames = []>
    }
    simulation.vpi_definition_member.bind scope 1 member @a expr
        <kind = storage, id = 0 : i64>
    // expected-error @+1 {{references an unknown port target ID}}
    simulation.vpi_definition_member.relation scope 1 member @a
        selector 44 iterate ordinal 0 to <kind = port, id = 99 : i64>
  }
}

// -----

module {
  simulation.design @ref_relation_duplicate_inverse {
    simulation.scope.decl 0 hierarchy "$root"
    simulation.vpi_definition.decl @cell type 32 name "cell"
    simulation.vpi_definition_member.decl @a of @cell type 28 ordinal 0
        name "a" direction ref
    simulation.vpi_definition_member.decl @b of @cell type 28 ordinal 1
        name "b" direction ref
    simulation.vpi_definition_specialization.decl @spec of @cell
    simulation.vpi_definition_member.specialize @spec member @a type
        #simulation.vpi_type<kind = bit, isSigned = false,
          isFourState = false, range = [0, 0], children = [], childNames = []>
    simulation.vpi_definition_member.specialize @spec member @b type
        #simulation.vpi_type<kind = bit, isSigned = false,
          isFourState = false, range = [0, 0], children = [], childNames = []>
    simulation.scope.decl 1 parent 0 hierarchy "top" vpi_kind 32
        definition @cell specialization @spec
    simulation.storage.decl 0 in 0 : i1 design hierarchy "actual" {
      vpi_type = #simulation.vpi_type<kind = bit, isSigned = false,
          isFourState = false, range = [0, 0], children = [], childNames = []>
    }
    simulation.port.decl 0 in 1 source 0 net = false at 0 : i1 inout
        ordinal 0 hierarchy "top.a" debug "a"
    simulation.vpi_definition_member.bind scope 1 member @a expr
        <kind = storage, id = 0 : i64>
    simulation.vpi_definition_member.bind scope 1 member @b expr
        <kind = storage, id = 0 : i64>
    simulation.vpi_definition_member.relation scope 1 member @a
        selector 44 iterate ordinal 0 to <kind = port, id = 0 : i64>
    // expected-error @+1 {{target port already has a reference connection}}
    simulation.vpi_definition_member.relation scope 1 member @b
        selector 44 iterate ordinal 0 to <kind = port, id = 0 : i64>
  }
}

// -----

module {
  simulation.design @ref_relation_duplicate_ordinal {
    simulation.scope.decl 0 hierarchy "$root"
    simulation.vpi_definition.decl @cell type 32 name "cell"
    simulation.vpi_definition_member.decl @a of @cell type 28 ordinal 0
        name "a" direction ref
    simulation.vpi_definition_specialization.decl @spec of @cell
    simulation.vpi_definition_member.specialize @spec member @a type
        #simulation.vpi_type<kind = bit, isSigned = false,
          isFourState = false, range = [0, 0], children = [], childNames = []>
    simulation.scope.decl 1 parent 0 hierarchy "top" vpi_kind 32
        definition @cell specialization @spec
    simulation.scope.decl 2 parent 1 hierarchy "top.left" vpi_kind 32
    simulation.scope.decl 3 parent 1 hierarchy "top.right" vpi_kind 32
    simulation.storage.decl 0 in 0 : i1 design hierarchy "actual" {
      vpi_type = #simulation.vpi_type<kind = bit, isSigned = false,
          isFourState = false, range = [0, 0], children = [], childNames = []>
    }
    simulation.storage.decl 1 in 2 : i1 design hierarchy "top.left.a"
    simulation.storage.decl 2 in 3 : i1 design hierarchy "top.right.a"
    simulation.port.decl 0 in 2 source 1 net = false at 0 : i1 input
        ordinal 0 hierarchy "top.left.a" debug "a"
    simulation.port.decl 1 in 3 source 2 net = false at 0 : i1 input
        ordinal 0 hierarchy "top.right.a" debug "a"
    simulation.vpi_definition_member.bind scope 1 member @a expr
        <kind = storage, id = 0 : i64>
    simulation.vpi_definition_member.relation scope 1 member @a
        selector 98 iterate ordinal 0 to <kind = port, id = 0 : i64>
    // expected-error @+1 {{duplicates an ordinal in the same relation}}
    simulation.vpi_definition_member.relation scope 1 member @a
        selector 98 iterate ordinal 0 to <kind = port, id = 1 : i64>
  }
}
