// RUN: talos-opt %s --split-input-file --verify-diagnostics -o /dev/null

module {
  simulation.design @missing_type {
    simulation.scope.decl 0 hierarchy "$root"
    // expected-error @+1 {{primitive definition requires a primitive type}}
    simulation.vpi_definition.decl @d type 21 name "and"
  }
}

// -----

module {
  simulation.design @wrong_kind {
    simulation.scope.decl 0 hierarchy "$root"
    // expected-error @+1 {{primitive type, object kind, and definition name must agree}}
    simulation.vpi_definition.decl @d type 55 name "and" primitive_type 1
  }
}

// -----

module {
  simulation.design @wrong_name {
    simulation.scope.decl 0 hierarchy "$root"
    // expected-error @+1 {{primitive type, object kind, and definition name must agree}}
    simulation.vpi_definition.decl @d type 21 name "or" primitive_type 1
  }
}

// -----

module {
  simulation.design @udp_subtype {
    simulation.scope.decl 0 hierarchy "$root"
    // expected-error @+1 {{primitive type, object kind, and definition name must agree}}
    simulation.vpi_definition.decl @d type 21 name "and" primitive_type 28
  }
}

// -----

module {
  simulation.design @primitive_location {
    simulation.scope.decl 0 hierarchy "$root"
    // expected-error @+1 {{built-in primitive definitions have no source definition location}}
    simulation.vpi_definition.decl @d type 21 name "and" definition_loc loc("gate.sv":1:1) primitive_type 1
  }
}

// -----

module {
  simulation.design @module_subtype {
    simulation.scope.decl 0 hierarchy "$root"
    // expected-error @+1 {{only primitive definitions may specify a primitive type}}
    simulation.vpi_definition.decl @d type 32 name "cell" primitive_type 1
  }
}

// -----

module {
  simulation.design @member {
    simulation.scope.decl 0 hierarchy "$root"
    simulation.vpi_definition.decl @d type 21 name "and" primitive_type 1
    // expected-error @+1 {{built-in primitive definitions cannot have members}}
    simulation.vpi_definition_member.decl @m of @d type 28 ordinal 0 name "a" direction input
  }
}

// -----

module {
  simulation.design @specialization {
    simulation.scope.decl 0 hierarchy "$root"
    simulation.vpi_definition.decl @d type 21 name "and" primitive_type 1
    // expected-error @+1 {{built-in primitive definitions cannot have specializations}}
    simulation.vpi_definition_specialization.decl @s of @d
  }
}

// -----

module {
  simulation.design @missing_source {
    simulation.scope.decl 0 hierarchy "$root"
    simulation.vpi_definition.decl @d type 21 name "and" primitive_type 1
    // expected-error @+1 {{requires a primitive anchor and a shared definition}}
    simulation.vpi_definition.bind @missing to @d
  }
}

// -----

module {
  simulation.design @scope_source {
    simulation.scope.decl 0 hierarchy "$root"
    simulation.vpi_definition.decl @d type 21 name "and" primitive_type 1
    simulation.scope.decl 1 parent 0 hierarchy "top" vpi_kind 32
    simulation.vpi_object.anchor @top id 0 type 32 in 1 ordinal 0 hierarchy "top" debug "top" {backing = #simulation.vpi_backing<kind = scope, id = 1 : i64>}
    simulation.vpi_object.anchor @g id 1 type 21 in 1 parent @top ordinal 0 hierarchy "top.g" debug "g" {primitive_input_count = 0 : i64}
    // expected-error @+1 {{primitive source and shared definition kinds must agree}}
    simulation.vpi_definition.bind @top to @d
  }
}

// -----

module {
  simulation.design @wrong_binding_kind {
    simulation.scope.decl 0 hierarchy "$root"
    simulation.vpi_definition.decl @d type 55 name "tran" primitive_type 22
    simulation.scope.decl 1 parent 0 hierarchy "top" vpi_kind 32
    simulation.vpi_object.anchor @top id 0 type 32 in 1 ordinal 0 hierarchy "top" debug "top" {backing = #simulation.vpi_backing<kind = scope, id = 1 : i64>}
    simulation.vpi_object.anchor @g id 1 type 21 in 1 parent @top ordinal 0 hierarchy "top.g" debug "g" {primitive_input_count = 0 : i64}
    // expected-error @+1 {{primitive source and shared definition kinds must agree}}
    simulation.vpi_definition.bind @g to @d
  }
}

// -----

module {
  simulation.design @duplicate_binding {
    simulation.scope.decl 0 hierarchy "$root"
    simulation.vpi_definition.decl @d type 21 name "and" primitive_type 1
    simulation.scope.decl 1 parent 0 hierarchy "top" vpi_kind 32
    simulation.vpi_object.anchor @top id 0 type 32 in 1 ordinal 0 hierarchy "top" debug "top" {backing = #simulation.vpi_backing<kind = scope, id = 1 : i64>}
    simulation.vpi_object.anchor @g id 1 type 21 in 1 parent @top ordinal 0 hierarchy "top.g" debug "g" {primitive_input_count = 0 : i64}
    simulation.vpi_definition.bind @g to @d
    // expected-error @+1 {{duplicate primitive definition binding}}
    simulation.vpi_definition.bind @g to @d
  }
}
