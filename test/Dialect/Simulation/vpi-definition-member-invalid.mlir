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
