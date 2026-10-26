// RUN: obelisk-opt %s --split-input-file --verify-diagnostics -o /dev/null

#int = #simulation.vpi_type<kind = int, isSigned = true, isFourState = false, range = [31, 0], children = [], childNames = []>
module {
 simulation.design @without_fact {
  simulation.scope.decl 0 hierarchy "$root"
  simulation.vpi_definition.decl @cell type 32 name "cell"
  simulation.vpi_definition_member.decl @T of @cell type 609 ordinal 0 name "T"
  // expected-error @+1 {{requires an available TypeParameter original default member}}
  simulation.vpi_definition_type_default @T type #int
 }
}

// -----

#int = #simulation.vpi_type<kind = int, isSigned = true, isFourState = false, range = [31, 0], children = [], childNames = []>
module {
 simulation.design @value_member {
  simulation.scope.decl 0 hierarchy "$root"
  simulation.vpi_definition.decl @cell type 32 name "cell"
  simulation.vpi_definition_member.decl @N of @cell type 41 ordinal 0 name "N" {has_original_default}
  // expected-error @+1 {{requires an available TypeParameter original default member}}
  simulation.vpi_definition_type_default @N type #int
 }
}

// -----

#int = #simulation.vpi_type<kind = int, isSigned = true, isFourState = false, range = [31, 0], children = [], childNames = []>
module {
 simulation.design @missing_member {
  simulation.scope.decl 0 hierarchy "$root"
  simulation.vpi_definition.decl @cell type 32 name "cell"
  // expected-error @+1 {{requires an available TypeParameter original default member}}
  simulation.vpi_definition_type_default @missing type #int
 }
}

// -----

#int = #simulation.vpi_type<kind = int, isSigned = true, isFourState = false, range = [31, 0], children = [], childNames = []>
module {
 simulation.design @named_type {
  simulation.scope.decl 0 hierarchy "$root"
  simulation.vpi_definition.decl @cell type 32 name "cell"
  simulation.vpi_definition_member.decl @T of @cell type 609 ordinal 0 name "T" {has_original_default}
  // expected-error @+1 {{VPI semantic type has an invalid named-type identity}}
  simulation.vpi_definition_type_default @T type #simulation.vpi_type<kind = int, isSigned = true, isFourState = false, name = "Alias", range = [31, 0], children = [], childNames = []>
 }
}

// -----

#int = #simulation.vpi_type<kind = int, isSigned = true, isFourState = false, range = [31, 0], children = [], childNames = []>
module {
 simulation.design @array_type {
  simulation.scope.decl 0 hierarchy "$root"
  simulation.vpi_definition.decl @cell type 32 name "cell"
  simulation.vpi_definition_member.decl @T of @cell type 609 ordinal 0 name "T" {has_original_default}
  // expected-error @+1 {{requires an original anonymous builtin leaf typespec}}
  simulation.vpi_definition_type_default @T type #simulation.vpi_type<kind = unpacked_array, isSigned = false, isFourState = false, range = [1, 0], children = [#int], childNames = []>
 }
}

// -----

#int = #simulation.vpi_type<kind = int, isSigned = true, isFourState = false, range = [31, 0], children = [], childNames = []>
module {
 simulation.design @duplicate_concrete {
  simulation.scope.decl 0 hierarchy "$root"
  simulation.vpi_definition.decl @cell type 32 name "cell"
  simulation.vpi_definition_member.decl @T of @cell type 609 ordinal 0 name "T" {has_original_default}
  simulation.vpi_definition_type_default @T type #int
  // expected-error @+1 {{duplicates a concrete type default root}}
  simulation.vpi_definition_type_default @T type #int
 }
}

// -----

#int = #simulation.vpi_type<kind = int, isSigned = true, isFourState = false, range = [31, 0], children = [], childNames = []>
module {
 simulation.design @mixed_roots {
  simulation.scope.decl 0 hierarchy "$root"
  simulation.vpi_definition.decl @cell type 32 name "cell"
  simulation.vpi_definition_member.decl @T of @cell type 609 ordinal 0 name "T"
  // expected-error @+1 {{original default fact requires exactly one source root}}
  simulation.vpi_definition_member.decl @U of @cell type 609 ordinal 1 name "U" {has_original_default}
  simulation.vpi_definition_type_default @U type #int
  simulation.vpi_relation.decl <kind = symbol, symbol = @U> selector 102 handle ordinal 0 to <kind = symbol, symbol = @T>
 }
}

// -----

#int = #simulation.vpi_type<kind = int, isSigned = true, isFourState = false, range = [31, 0], children = [], childNames = []>
module {
 simulation.design @forward_reference {
  simulation.scope.decl 0 hierarchy "$root"
  simulation.vpi_definition.decl @cell type 32 name "cell"
  simulation.vpi_definition_member.decl @T of @cell type 609 ordinal 0 name "T" {has_original_default}
  simulation.vpi_definition_member.decl @U of @cell type 609 ordinal 1 name "U"
  // expected-error @+1 {{default leaf must be an exact preceding parameter of the same kind}}
  simulation.vpi_relation.decl <kind = symbol, symbol = @T> selector 102 handle ordinal 0 to <kind = symbol, symbol = @U>
 }
}

// -----

#int = #simulation.vpi_type<kind = int, isSigned = true, isFourState = false, range = [31, 0], children = [], childNames = []>
module {
 simulation.design @foreign_definition {
  simulation.scope.decl 0 hierarchy "$root"
  simulation.vpi_definition.decl @cell type 32 name "cell"
  simulation.vpi_definition.decl @other type 32 name "other"
  simulation.vpi_definition_member.decl @A of @other type 609 ordinal 0 name "A"
  simulation.vpi_definition_member.decl @T of @cell type 609 ordinal 0 name "T" {has_original_default}
  // expected-error @+1 {{default leaf must be an exact preceding parameter of the same kind}}
  simulation.vpi_relation.decl <kind = symbol, symbol = @T> selector 102 handle ordinal 0 to <kind = symbol, symbol = @A>
 }
}

// -----

module {
 simulation.design @ranged_builtin {
  simulation.scope.decl 0 hierarchy "$root"
  simulation.vpi_definition.decl @cell type 32 name "cell"
  simulation.vpi_definition_member.decl @T of @cell type 609 ordinal 0 name "T" {has_original_default}
  // expected-error @+1 {{requires an original anonymous builtin leaf typespec}}
  simulation.vpi_definition_type_default @T type #simulation.vpi_type<kind = bit, isSigned = false, isFourState = false, range = [7, 0], children = [], childNames = []>
 }
}
