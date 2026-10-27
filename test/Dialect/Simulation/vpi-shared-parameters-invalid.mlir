// RUN: talos-opt %s --split-input-file --verify-diagnostics -o /dev/null

module {
  simulation.design @parameter_direction {
    simulation.scope.decl 0 hierarchy "$root"
    simulation.vpi_definition.decl @cell type 32 name "cell"
    // expected-error @+1 {{parameter declaration cannot carry an IO direction}}
    simulation.vpi_definition_member.decl @N of @cell type 41 ordinal 0 name "N" direction input
  }
}

// -----

module {
  simulation.design @type_range {
    simulation.scope.decl 0 hierarchy "$root"
    simulation.vpi_definition.decl @cell type 32 name "cell"
    // expected-error @+1 {{type parameter cannot carry a value range}}
    simulation.vpi_definition_member.decl @T of @cell type 609 ordinal 0 name "T" {has_explicit_parameter_range}
  }
}

// -----

module {
  simulation.design @io_facts {
    simulation.scope.decl 0 hierarchy "$root"
    simulation.vpi_definition.decl @cell type 32 name "cell"
    // expected-error @+1 {{IO declaration cannot carry parameter facts}}
    simulation.vpi_definition_member.decl @p of @cell type 28 ordinal 0 name "p" direction input {is_local_param}
  }
}

// -----

module {
  simulation.design @unsupported_kind {
    simulation.scope.decl 0 hierarchy "$root"
    simulation.vpi_definition.decl @cell type 32 name "cell"
    // expected-error @+1 {{requires IODecl, Parameter, or TypeParameter kind}}
    simulation.vpi_definition_member.decl @p of @cell type 25 ordinal 0 name "p"
  }
}

// -----

module {
  simulation.design @duplicate_name {
    simulation.scope.decl 0 hierarchy "$root"
    simulation.vpi_definition.decl @cell type 32 name "cell"
    simulation.vpi_definition_member.decl @N of @cell type 41 ordinal 0 name "N"
    // expected-error @+1 {{duplicates a declaration name in the same VPI definition}}
    simulation.vpi_definition_member.decl @T of @cell type 609 ordinal 1 name "N"
  }
}

// -----

module {
  simulation.design @missing_value {
    simulation.scope.decl 0 hierarchy "$root"
    simulation.vpi_definition.decl @cell type 32 name "cell"
    simulation.vpi_definition_member.decl @N of @cell type 41 ordinal 0 name "N"
    simulation.vpi_definition_specialization.decl @spec of @cell
    // expected-error @+1 {{exactly value parameters require an immutable value}}
    simulation.vpi_definition_member.specialize @spec member @N type #simulation.vpi_type<kind = int, isSigned = true, isFourState = false, range = [31, 0], children = [], childNames = []>
  }
}

// -----

module {
  simulation.design @type_with_value {
    simulation.scope.decl 0 hierarchy "$root"
    simulation.vpi_definition.decl @cell type 32 name "cell"
    simulation.vpi_definition_member.decl @N of @cell type 609 ordinal 0 name "N"
    simulation.vpi_definition_specialization.decl @spec of @cell
    // expected-error @+1 {{exactly value parameters require an immutable value}}
    simulation.vpi_definition_member.specialize @spec member @N type #simulation.vpi_type<kind = int, isSigned = true, isFourState = false, range = [31, 0], children = [], childNames = []> {immutable_value = #simulation.frozen_constant<value = [4 : i32, 0 : i32], isSigned = true> : i32}
  }
}

// -----

module {
  simulation.design @signedness {
    simulation.scope.decl 0 hierarchy "$root"
    simulation.vpi_definition.decl @cell type 32 name "cell"
    simulation.vpi_definition_member.decl @N of @cell type 41 ordinal 0 name "N"
    simulation.vpi_definition_specialization.decl @spec of @cell
    // expected-error @+1 {{parameter value and effective type signedness differ}}
    simulation.vpi_definition_member.specialize @spec member @N type #simulation.vpi_type<kind = int, isSigned = true, isFourState = false, range = [31, 0], children = [], childNames = []> {immutable_value = #simulation.frozen_constant<value = [4 : i32, 0 : i32], isSigned = false> : i32}
  }
}

// -----

module {
  simulation.design @mixed_inventory {
    simulation.scope.decl 0 hierarchy "$root"
    simulation.vpi_definition.decl @cell type 32 name "cell"
    simulation.vpi_definition_member.decl @N of @cell type 41 ordinal 0 name "N"
    simulation.vpi_definition_specialization.decl @spec of @cell
    simulation.vpi_definition_member.specialize @spec member @N type #simulation.vpi_type<kind = int, isSigned = true, isFourState = false, range = [31, 0], children = [], childNames = []> {immutable_value = #simulation.frozen_constant<value = [4 : i32, 0 : i32], isSigned = true> : i32}
    simulation.scope.decl 1 parent 0 hierarchy "top" vpi_kind 32 definition @cell specialization @spec
    simulation.vpi_object.anchor @top id 0 type 32 in 1 ordinal 0 hierarchy "top" debug "top" {backing = #simulation.vpi_backing<kind = scope, id = 1 : i64>}
    // expected-error @+1 {{parameter inventory cannot mix shared definition members and instance anchors}}
    simulation.vpi_object.anchor @N2 id 1 type 41 in 1 parent @top ordinal 0 hierarchy "top.N2" debug "N2" {vpi_type = #simulation.vpi_type<kind = int, isSigned = true, isFourState = false, range = [31, 0], children = [], childNames = []>, immutable_value = #simulation.frozen_constant<value = [4 : i32, 0 : i32], isSigned = true> : i32}
  }
}

// -----
module {
  simulation.design @scalar_range {
    simulation.scope.decl 0 hierarchy "$root"
    simulation.vpi_definition.decl @cell type 32 name "cell"
    simulation.vpi_definition_member.decl @N of @cell type 41 ordinal 0 name "N" {has_explicit_parameter_range}
    simulation.vpi_definition_specialization.decl @spec of @cell
    // expected-error @+1 {{explicit parameter range requires a packed-array semantic root}}
    simulation.vpi_definition_member.specialize @spec member @N type #simulation.vpi_type<kind = int, isSigned = true, isFourState = false, range = [31, 0], children = [], childNames = []> {immutable_value = #simulation.frozen_constant<value = [4 : i32, 0 : i32], isSigned = true> : i32}
  }
}
