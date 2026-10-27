// RUN: talos-opt %s -split-input-file -verify-diagnostics

module {
  simulation.design @d {
    simulation.scope.decl 0 hierarchy "$root"
    simulation.vpi_object.anchor @pkg id 0 type 600 in 0 ordinal 0 hierarchy "pkg" debug "pkg"
    simulation.vpi_object.anchor @base id 1 type 652 in 0 parent @pkg ordinal 0 hierarchy "pkg::Base" debug "Base"
    simulation.vpi_object.anchor @derived id 2 type 652 in 0 parent @pkg ordinal 1 hierarchy "pkg::Derived" debug "Derived"
    simulation.vpi_object.anchor @t id 3 type 609 in 0 parent @base ordinal 0 hierarchy "pkg::Base::T" debug "T" {unresolved_type_parameter}
    simulation.vpi_object.anchor @p id 4 type 41 in 0 parent @base ordinal 1 hierarchy "pkg::Base::P" debug "P" {unresolved_value_parameter}
    simulation.vpi_object.anchor @u id 5 type 609 in 0 parent @derived ordinal 0 hierarchy "pkg::Derived::U" debug "U" {unresolved_type_parameter}
    simulation.vpi_object.anchor @n id 6 type 41 in 0 parent @derived ordinal 1 hierarchy "pkg::Derived::N" debug "N" {unresolved_value_parameter}
    simulation.vpi_lexical_class_typespec.decl @type id 0 in 0 owner @derived definition @base debug "Base"
    simulation.vpi_class_extends.decl @clause id 7 in 0 owner @derived {base_typespec = @type}
    // expected-error @+1 {{explicit RHS and unavailable RHS are mutually exclusive}}
    simulation.vpi_class_parameter_assign.decl @a id 8 in 0 owner @type parameter @t ordinal 0 {rhs = @u, unavailable_rhs = "f()"}
    simulation.vpi_class_parameter_assign.decl @b id 9 in 0 owner @type parameter @p ordinal 1
  }
}

// -----

module {
  simulation.design @d {
    simulation.scope.decl 0 hierarchy "$root"
    simulation.vpi_object.anchor @pkg id 0 type 600 in 0 ordinal 0 hierarchy "pkg" debug "pkg"
    simulation.vpi_object.anchor @base id 1 type 652 in 0 parent @pkg ordinal 0 hierarchy "pkg::Base" debug "Base"
    simulation.vpi_object.anchor @derived id 2 type 652 in 0 parent @pkg ordinal 1 hierarchy "pkg::Derived" debug "Derived"
    simulation.vpi_object.anchor @t id 3 type 609 in 0 parent @base ordinal 0 hierarchy "pkg::Base::T" debug "T" {unresolved_type_parameter}
    simulation.vpi_object.anchor @p id 4 type 41 in 0 parent @base ordinal 1 hierarchy "pkg::Base::P" debug "P" {unresolved_value_parameter}
    simulation.vpi_object.anchor @u id 5 type 609 in 0 parent @derived ordinal 0 hierarchy "pkg::Derived::U" debug "U" {unresolved_type_parameter}
    simulation.vpi_object.anchor @n id 6 type 41 in 0 parent @derived ordinal 1 hierarchy "pkg::Derived::N" debug "N" {unresolved_value_parameter}
    simulation.vpi_lexical_class_typespec.decl @type id 0 in 0 owner @derived definition @base debug "Base"
    simulation.vpi_class_extends.decl @clause id 7 in 0 owner @derived {base_typespec = @type}
    // expected-error @+1 {{explicit RHS must match the exact formal parameter kind}}
    simulation.vpi_class_parameter_assign.decl @a id 8 in 0 owner @type parameter @t ordinal 0 {rhs = @n}
    simulation.vpi_class_parameter_assign.decl @b id 9 in 0 owner @type parameter @p ordinal 1
  }
}

// -----

module {
  simulation.design @d {
    simulation.scope.decl 0 hierarchy "$root"
    simulation.vpi_object.anchor @pkg id 0 type 600 in 0 ordinal 0 hierarchy "pkg" debug "pkg"
    simulation.vpi_object.anchor @base id 1 type 652 in 0 parent @pkg ordinal 0 hierarchy "pkg::Base" debug "Base"
    simulation.vpi_object.anchor @derived id 2 type 652 in 0 parent @pkg ordinal 1 hierarchy "pkg::Derived" debug "Derived"
    simulation.vpi_object.anchor @t id 3 type 609 in 0 parent @base ordinal 0 hierarchy "pkg::Base::T" debug "T" {unresolved_type_parameter}
    simulation.vpi_object.anchor @p id 4 type 41 in 0 parent @base ordinal 1 hierarchy "pkg::Base::P" debug "P" {unresolved_value_parameter}
    simulation.vpi_object.anchor @u id 5 type 609 in 0 parent @derived ordinal 0 hierarchy "pkg::Derived::U" debug "U" {unresolved_type_parameter}
    simulation.vpi_object.anchor @n id 6 type 41 in 0 parent @derived ordinal 1 hierarchy "pkg::Derived::N" debug "N" {unresolved_value_parameter}
    simulation.vpi_lexical_class_typespec.decl @type id 0 in 0 owner @derived definition @base debug "Base"
    simulation.vpi_class_extends.decl @clause id 7 in 0 owner @derived {base_typespec = @type}
    // expected-error @+1 {{actual reference must name a parameter in the derived class}}
    simulation.vpi_class_parameter_assign.decl @a id 8 in 0 owner @type parameter @t ordinal 0 {rhs = @t}
    simulation.vpi_class_parameter_assign.decl @b id 9 in 0 owner @type parameter @p ordinal 1
  }
}

// -----

module {
  simulation.design @d {
    simulation.scope.decl 0 hierarchy "$root"
    simulation.vpi_object.anchor @pkg id 0 type 600 in 0 ordinal 0 hierarchy "pkg" debug "pkg"
    simulation.vpi_object.anchor @base id 1 type 652 in 0 parent @pkg ordinal 0 hierarchy "pkg::Base" debug "Base"
    simulation.vpi_object.anchor @derived id 2 type 652 in 0 parent @pkg ordinal 1 hierarchy "pkg::Derived" debug "Derived"
    simulation.vpi_object.anchor @t id 3 type 609 in 0 parent @base ordinal 0 hierarchy "pkg::Base::T" debug "T" {unresolved_type_parameter}
    simulation.vpi_object.anchor @p id 4 type 41 in 0 parent @base ordinal 1 hierarchy "pkg::Base::P" debug "P" {unresolved_value_parameter}
    simulation.vpi_object.anchor @u id 5 type 609 in 0 parent @derived ordinal 0 hierarchy "pkg::Derived::U" debug "U" {unresolved_type_parameter, vpi_properties = #simulation.vpi_properties<[#simulation.vpi_property<selector = 70 : i32, value = true>]>}
    simulation.vpi_object.anchor @n id 6 type 41 in 0 parent @derived ordinal 1 hierarchy "pkg::Derived::N" debug "N" {unresolved_value_parameter}
    simulation.vpi_lexical_class_typespec.decl @type id 0 in 0 owner @derived definition @base debug "Base"
    simulation.vpi_class_extends.decl @clause id 7 in 0 owner @derived {base_typespec = @type}
    // expected-error @+1 {{actual reference cannot name a body local parameter}}
    simulation.vpi_class_parameter_assign.decl @a id 8 in 0 owner @type parameter @t ordinal 0 {rhs = @u}
    simulation.vpi_class_parameter_assign.decl @b id 9 in 0 owner @type parameter @p ordinal 1
  }
}

// -----

module {
  simulation.design @d {
    simulation.scope.decl 0 hierarchy "$root"
    simulation.vpi_object.anchor @pkg id 0 type 600 in 0 ordinal 0 hierarchy "pkg" debug "pkg"
    simulation.vpi_object.anchor @base id 1 type 652 in 0 parent @pkg ordinal 0 hierarchy "pkg::Base" debug "Base"
    simulation.vpi_object.anchor @derived id 2 type 652 in 0 parent @pkg ordinal 1 hierarchy "pkg::Derived" debug "Derived"
    simulation.vpi_object.anchor @t id 3 type 609 in 0 parent @base ordinal 0 hierarchy "pkg::Base::T" debug "T" {unresolved_type_parameter}
    simulation.vpi_object.anchor @p id 4 type 41 in 0 parent @base ordinal 1 hierarchy "pkg::Base::P" debug "P" {unresolved_value_parameter}
    simulation.vpi_object.anchor @u id 5 type 609 in 0 parent @derived ordinal 0 hierarchy "pkg::Derived::U" debug "U" {unresolved_type_parameter}
    simulation.vpi_object.anchor @n id 6 type 41 in 0 parent @derived ordinal 1 hierarchy "pkg::Derived::N" debug "N" {unresolved_value_parameter}
    simulation.vpi_lexical_class_typespec.decl @type id 0 in 0 owner @derived definition @base debug "Base"
    simulation.vpi_class_extends.decl @clause id 7 in 0 owner @derived {base_typespec = @type}
    // expected-error @+1 {{unavailable source syntax must be nonempty without NUL bytes}}
    simulation.vpi_class_parameter_assign.decl @a id 8 in 0 owner @type parameter @t ordinal 0 {unavailable_rhs = ""}
    simulation.vpi_class_parameter_assign.decl @b id 9 in 0 owner @type parameter @p ordinal 1
  }
}

// -----

module {
  simulation.design @d {
    simulation.scope.decl 0 hierarchy "$root"
    simulation.vpi_object.anchor @pkg id 0 type 600 in 0 ordinal 0 hierarchy "pkg" debug "pkg"
    simulation.vpi_object.anchor @base id 1 type 652 in 0 parent @pkg ordinal 0 hierarchy "pkg::Base" debug "Base"
    simulation.vpi_object.anchor @derived id 2 type 652 in 0 parent @pkg ordinal 1 hierarchy "pkg::Derived" debug "Derived"
    simulation.vpi_object.anchor @t id 3 type 609 in 0 parent @base ordinal 0 hierarchy "pkg::Base::T" debug "T" {unresolved_type_parameter}
    simulation.vpi_object.anchor @p id 4 type 41 in 0 parent @base ordinal 1 hierarchy "pkg::Base::P" debug "P" {unresolved_value_parameter}
    simulation.vpi_object.anchor @u id 5 type 609 in 0 parent @derived ordinal 0 hierarchy "pkg::Derived::U" debug "U" {unresolved_type_parameter}
    simulation.vpi_object.anchor @n id 6 type 41 in 0 parent @derived ordinal 1 hierarchy "pkg::Derived::N" debug "N" {unresolved_value_parameter}
    simulation.vpi_lexical_class_typespec.decl @type id 0 in 0 owner @derived definition @base debug "Base"
    simulation.vpi_class_extends.decl @clause id 7 in 0 owner @derived {base_typespec = @type}
    simulation.vpi_class_parameter_assign.decl @a id 8 in 0 owner @type parameter @t ordinal 0
    simulation.vpi_class_parameter_assign.decl @b id 9 in 0 owner @type parameter @p ordinal 1
    // expected-error @+1 {{source actual typespec must be its owner's exact RHS}}
    simulation.vpi_typespec.decl @int id 1 in 0 owner @a hierarchy "" debug "" {origin = 5 : i32, target_type = #simulation.vpi_type<kind = int, isSigned = true, isFourState = false, range = [31, 0], children = [], childNames = []>}
  }
}

// -----

module {
  simulation.design @d {
    simulation.scope.decl 0 hierarchy "$root"
    simulation.vpi_object.anchor @pkg id 0 type 600 in 0 ordinal 0 hierarchy "pkg" debug "pkg"
    simulation.vpi_object.anchor @base id 1 type 652 in 0 parent @pkg ordinal 0 hierarchy "pkg::Base" debug "Base"
    simulation.vpi_object.anchor @derived id 2 type 652 in 0 parent @pkg ordinal 1 hierarchy "pkg::Derived" debug "Derived"
    simulation.vpi_object.anchor @t id 3 type 609 in 0 parent @base ordinal 0 hierarchy "pkg::Base::T" debug "T" {unresolved_type_parameter}
    simulation.vpi_object.anchor @p id 4 type 41 in 0 parent @base ordinal 1 hierarchy "pkg::Base::P" debug "P" {unresolved_value_parameter}
    simulation.vpi_object.anchor @u id 5 type 609 in 0 parent @derived ordinal 0 hierarchy "pkg::Derived::U" debug "U" {unresolved_type_parameter}
    simulation.vpi_object.anchor @n id 6 type 41 in 0 parent @derived ordinal 1 hierarchy "pkg::Derived::N" debug "N" {unresolved_value_parameter}
    simulation.vpi_lexical_class_typespec.decl @type id 0 in 0 owner @derived definition @base debug "Base"
    simulation.vpi_class_extends.decl @clause id 7 in 0 owner @derived {base_typespec = @type}
    simulation.vpi_class_parameter_assign.decl @a id 8 in 0 owner @type parameter @t ordinal 0
    simulation.vpi_class_parameter_assign.decl @b id 9 in 0 owner @type parameter @p ordinal 1
    // expected-error @+1 {{class actual owner requires source-default origin}}
    simulation.vpi_typespec.decl @int id 1 in 0 owner @a hierarchy "hidden" debug "hidden" {target_type = #simulation.vpi_type<kind = int, isSigned = true, isFourState = false, range = [31, 0], children = [], childNames = []>}
  }
}

// -----

module {
  simulation.design @d {
    simulation.scope.decl 0 hierarchy "$root"
    simulation.scope.decl 1 parent 0 hierarchy "unrelated" vpi_kind 32
    simulation.vpi_object.anchor @pkg id 0 type 600 in 0 ordinal 0 hierarchy "pkg" debug "pkg"
    simulation.vpi_object.anchor @base id 1 type 652 in 0 parent @pkg ordinal 0 hierarchy "pkg::Base" debug "Base"
    simulation.vpi_object.anchor @derived id 2 type 652 in 0 parent @pkg ordinal 1 hierarchy "pkg::Derived" debug "Derived"
    simulation.vpi_object.anchor @t id 3 type 609 in 0 parent @base ordinal 0 hierarchy "pkg::Base::T" debug "T" {unresolved_type_parameter}
    simulation.vpi_object.anchor @p id 4 type 41 in 0 parent @base ordinal 1 hierarchy "pkg::Base::P" debug "P" {unresolved_value_parameter}
    simulation.vpi_object.anchor @u id 5 type 609 in 0 parent @derived ordinal 0 hierarchy "pkg::Derived::U" debug "U" {unresolved_type_parameter}
    simulation.vpi_object.anchor @n id 6 type 41 in 0 parent @derived ordinal 1 hierarchy "pkg::Derived::N" debug "N" {unresolved_value_parameter}
    simulation.vpi_lexical_class_typespec.decl @type id 0 in 0 owner @derived definition @base debug "Base"
    simulation.vpi_class_extends.decl @clause id 7 in 0 owner @derived {base_typespec = @type}
    simulation.vpi_class_parameter_assign.decl @a id 8 in 0 owner @type parameter @t ordinal 0
    simulation.vpi_class_parameter_assign.decl @b id 9 in 0 owner @type parameter @p ordinal 1 {rhs = @add}
    simulation.vpi_object.anchor @add id 10 type 39 in 0 parent @b ordinal 0 hierarchy "" debug "" {vpi_properties = #simulation.vpi_properties<[#simulation.vpi_property<selector = 39 : i32, value = 24 : i32>]>}
    // expected-error @+1 {{source expression scope must match its exact owner}}
    simulation.vpi_object.anchor @literal id 11 type 7 in 1 parent @add ordinal 0 hierarchy "" debug "" {vpi_type = #simulation.vpi_type<kind = int, isSigned = true, isFourState = false, range = [31, 0], children = [], childNames = []>, immutable_value = #simulation.frozen_constant<value = [2 : i32, 0 : i32], isSigned = true> : i32, vpi_properties = #simulation.vpi_properties<[#simulation.vpi_property<selector = 40 : i32, value = 7 : i32>]>}
    simulation.vpi_relation.decl <kind = symbol, symbol = @add> selector 97 iterate ordinal 0 to <kind = symbol, symbol = @n>
    simulation.vpi_relation.decl <kind = symbol, symbol = @add> selector 97 iterate ordinal 1 to <kind = symbol, symbol = @literal>
  }
}
