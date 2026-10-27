// RUN: talos-opt %s -split-input-file -verify-diagnostics

module {
  simulation.design @d {
    simulation.scope.decl 0
    simulation.vpi_object.anchor @pkg id 0 type 600 in 0 ordinal 0 hierarchy "pkg" debug "pkg"
    simulation.vpi_object.anchor @base id 1 type 652 in 0 parent @pkg ordinal 0 hierarchy "pkg::Base" debug "Base"
    simulation.vpi_object.anchor @derived id 2 type 652 in 0 parent @pkg ordinal 1 hierarchy "pkg::Derived" debug "Derived"
    simulation.vpi_object.anchor @t id 3 type 609 in 0 parent @base ordinal 0 hierarchy "pkg::Base::T" debug "T" {unresolved_type_parameter}
    // expected-error @+1 {{owner must be a lexical class definition}}
    simulation.vpi_lexical_class_typespec.decl @type id 0 in 0 owner @pkg definition @base debug "Base"
    simulation.vpi_class_extends.decl @clause id 4 in 0 owner @derived {base_typespec = @type}
    simulation.vpi_class_parameter_assign.decl @a id 5 in 0 owner @type parameter @t ordinal 0
  }
}

// -----

module {
  simulation.design @d {
    simulation.scope.decl 0
    simulation.vpi_object.anchor @pkg id 0 type 600 in 0 ordinal 0 hierarchy "pkg" debug "pkg"
    simulation.vpi_object.anchor @base id 1 type 652 in 0 parent @pkg ordinal 0 hierarchy "pkg::Base" debug "Base"
    simulation.vpi_object.anchor @derived id 2 type 652 in 0 parent @pkg ordinal 1 hierarchy "pkg::Derived" debug "Derived"
    simulation.vpi_object.anchor @t id 3 type 609 in 0 parent @base ordinal 0 hierarchy "pkg::Base::T" debug "T" {unresolved_type_parameter}
    // expected-error @+1 {{debug name must match the exact class declaration}}
    simulation.vpi_lexical_class_typespec.decl @type id 0 in 0 owner @derived definition @base debug "wrong"
    simulation.vpi_class_extends.decl @clause id 4 in 0 owner @derived {base_typespec = @type}
    simulation.vpi_class_parameter_assign.decl @a id 5 in 0 owner @type parameter @t ordinal 0
  }
}

// -----

module {
  simulation.design @d {
    simulation.scope.decl 0
    simulation.vpi_object.anchor @pkg id 0 type 600 in 0 ordinal 0 hierarchy "pkg" debug "pkg"
    simulation.vpi_object.anchor @base id 1 type 652 in 0 parent @pkg ordinal 0 hierarchy "pkg::Base" debug "Base"
    simulation.vpi_object.anchor @derived id 2 type 652 in 0 parent @pkg ordinal 1 hierarchy "pkg::Derived" debug "Derived"
    simulation.vpi_object.anchor @t id 3 type 609 in 0 parent @base ordinal 0 hierarchy "pkg::Base::T" debug "T" {unresolved_type_parameter}
    simulation.vpi_lexical_class_typespec.decl @type id 0 in 0 owner @derived definition @base debug "Base"
    // expected-error @+1 {{requires exactly one base typespec or unavailable base}}
    simulation.vpi_class_extends.decl @clause id 4 in 0 owner @derived
    simulation.vpi_class_parameter_assign.decl @a id 5 in 0 owner @type parameter @t ordinal 0
  }
}

// -----

module {
  simulation.design @d {
    simulation.scope.decl 0
    simulation.vpi_object.anchor @pkg id 0 type 600 in 0 ordinal 0 hierarchy "pkg" debug "pkg"
    simulation.vpi_object.anchor @base id 1 type 652 in 0 parent @pkg ordinal 0 hierarchy "pkg::Base" debug "Base"
    simulation.vpi_object.anchor @derived id 2 type 652 in 0 parent @pkg ordinal 1 hierarchy "pkg::Derived" debug "Derived"
    simulation.vpi_object.anchor @t id 3 type 609 in 0 parent @base ordinal 0 hierarchy "pkg::Base::T" debug "T" {unresolved_type_parameter}
    simulation.vpi_lexical_class_typespec.decl @type id 0 in 0 owner @derived definition @base debug "Base"
    // expected-error @+1 {{requires exactly one base typespec or unavailable base}}
    simulation.vpi_class_extends.decl @clause id 4 in 0 owner @derived {base_typespec = @type, unavailable_base = "T"}
    simulation.vpi_class_parameter_assign.decl @a id 5 in 0 owner @type parameter @t ordinal 0
  }
}

// -----

module {
  simulation.design @d {
    simulation.scope.decl 0
    simulation.vpi_object.anchor @pkg id 0 type 600 in 0 ordinal 0 hierarchy "pkg" debug "pkg"
    simulation.vpi_object.anchor @base id 1 type 652 in 0 parent @pkg ordinal 0 hierarchy "pkg::Base" debug "Base"
    simulation.vpi_object.anchor @derived id 2 type 652 in 0 parent @pkg ordinal 1 hierarchy "pkg::Derived" debug "Derived"
    simulation.vpi_object.anchor @t id 3 type 609 in 0 parent @base ordinal 0 hierarchy "pkg::Base::T" debug "T" {unresolved_type_parameter}
    simulation.vpi_lexical_class_typespec.decl @type id 0 in 0 owner @derived definition @base debug "Base"
    // expected-error @+1 {{base typespec must be the lexical type occurrence owned by this class}}
    simulation.vpi_class_extends.decl @clause id 4 in 0 owner @base {base_typespec = @type}
    simulation.vpi_class_parameter_assign.decl @a id 5 in 0 owner @type parameter @t ordinal 0
  }
}

// -----

module {
  simulation.design @d {
    simulation.scope.decl 0
    simulation.vpi_object.anchor @pkg id 0 type 600 in 0 ordinal 0 hierarchy "pkg" debug "pkg"
    simulation.vpi_object.anchor @base id 1 type 652 in 0 parent @pkg ordinal 0 hierarchy "pkg::Base" debug "Base"
    simulation.vpi_object.anchor @derived id 2 type 652 in 0 parent @pkg ordinal 1 hierarchy "pkg::Derived" debug "Derived"
    simulation.vpi_object.anchor @t id 3 type 609 in 0 parent @base ordinal 0 hierarchy "pkg::Base::T" debug "T" {unresolved_type_parameter}
    simulation.vpi_lexical_class_typespec.decl @type id 0 in 0 owner @derived definition @base debug "Base"
    simulation.vpi_class_extends.decl @clause id 4 in 0 owner @derived {base_typespec = @type}
    // expected-error @+1 {{parameter must be declared in the exact base definition}}
    simulation.vpi_class_parameter_assign.decl @a id 5 in 0 owner @type parameter @derived ordinal 0
  }
}

// -----

module {
  simulation.design @d {
    simulation.scope.decl 0
    simulation.vpi_object.anchor @pkg id 0 type 600 in 0 ordinal 0 hierarchy "pkg" debug "pkg"
    simulation.vpi_object.anchor @base id 1 type 652 in 0 parent @pkg ordinal 0 hierarchy "pkg::Base" debug "Base"
    simulation.vpi_object.anchor @derived id 2 type 652 in 0 parent @pkg ordinal 1 hierarchy "pkg::Derived" debug "Derived"
    simulation.vpi_object.anchor @t id 3 type 609 in 0 parent @base ordinal 0 hierarchy "pkg::Base::T" debug "T" {unresolved_type_parameter}
    // expected-error @+1 {{class actual assignments must cover every non-local base parameter}}
    simulation.vpi_lexical_class_typespec.decl @type id 0 in 0 owner @derived definition @base debug "Base"
    simulation.vpi_class_extends.decl @clause id 4 in 0 owner @derived {base_typespec = @type}
  }
}

// -----

module {
  simulation.design @d {
    simulation.scope.decl 0
    simulation.vpi_object.anchor @pkg id 0 type 600 in 0 ordinal 0 hierarchy "pkg" debug "pkg"
    simulation.vpi_object.anchor @base id 1 type 652 in 0 parent @pkg ordinal 0 hierarchy "pkg::Base" debug "Base"
    simulation.vpi_object.anchor @derived id 2 type 652 in 0 parent @pkg ordinal 1 hierarchy "pkg::Derived" debug "Derived"
    simulation.vpi_object.anchor @t id 3 type 609 in 0 parent @base ordinal 0 hierarchy "pkg::Base::T" debug "T" {unresolved_type_parameter}
    simulation.vpi_lexical_class_typespec.decl @type id 0 in 0 owner @derived definition @base debug "Base"
    simulation.vpi_class_extends.decl @clause id 4 in 0 owner @derived {base_typespec = @type}
    // expected-error @+1 {{class assignments must preserve dense parameter declaration order}}
    simulation.vpi_class_parameter_assign.decl @a id 5 in 0 owner @type parameter @t ordinal 1
  }
}

// -----

module {
  simulation.design @d {
    simulation.scope.decl 0
    simulation.vpi_object.anchor @pkg id 0 type 600 in 0 ordinal 0 hierarchy "pkg" debug "pkg"
    simulation.vpi_object.anchor @base id 1 type 652 in 0 parent @pkg ordinal 0 hierarchy "pkg::Base" debug "Base"
    simulation.vpi_object.anchor @derived id 2 type 652 in 0 parent @pkg ordinal 1 hierarchy "pkg::Derived" debug "Derived"
    simulation.vpi_object.anchor @t id 3 type 609 in 0 parent @base ordinal 0 hierarchy "pkg::Base::T" debug "T" {unresolved_type_parameter}
    // expected-error @+1 {{source inheritance cannot carry an effective type, value, or arbitrary properties}}
    simulation.vpi_lexical_class_typespec.decl @type id 0 in 0 owner @derived definition @base debug "Base" {constant_value = 1 : i32}
    simulation.vpi_class_extends.decl @clause id 4 in 0 owner @derived {base_typespec = @type}
    simulation.vpi_class_parameter_assign.decl @a id 5 in 0 owner @type parameter @t ordinal 0
  }
}

// -----

module {
  simulation.design @d {
    simulation.scope.decl 0
    simulation.vpi_object.anchor @pkg id 0 type 600 in 0 ordinal 0 hierarchy "pkg" debug "pkg"
    simulation.vpi_object.anchor @base id 1 type 652 in 0 parent @pkg ordinal 0 hierarchy "pkg::Base" debug "Base"
    simulation.vpi_object.anchor @derived id 2 type 652 in 0 parent @pkg ordinal 1 hierarchy "pkg::Derived" debug "Derived"
    simulation.vpi_object.anchor @t id 3 type 609 in 0 parent @base ordinal 0 hierarchy "pkg::Base::T" debug "T" {unresolved_type_parameter}
    simulation.vpi_lexical_class_typespec.decl @type id 0 in 0 owner @derived definition @base debug "Base"
    simulation.vpi_class_extends.decl @clause id 4 in 0 owner @derived {base_typespec = @type}
    simulation.vpi_class_parameter_assign.decl @a id 5 in 0 owner @type parameter @t ordinal 0
    // expected-error @+1 {{source inheritance relations are supplied by their typed declarations}}
    simulation.vpi_relation.decl <kind = symbol, symbol = @a> selector 77 handle ordinal 0 to <kind = symbol, symbol = @t>
  }
}

// -----

module {
  simulation.design @d {
    simulation.scope.decl 0
    simulation.vpi_object.anchor @pkg id 0 type 600 in 0 ordinal 0 hierarchy "pkg" debug "pkg"
    simulation.vpi_object.anchor @c id 1 type 652 in 0 parent @pkg ordinal 0 hierarchy "pkg::C" debug "C"
    simulation.class.decl @class id 1 {debug_name = "C", is_abstract = false, is_final = false, is_interface = false}
    simulation.vpi_typespec.decl @specialization id 0 in 0 owner @c hierarchy "pkg::C$int" debug "C" {
      origin = 3 : i32,
      target_type = #simulation.vpi_type<kind = class, isSigned = false, isFourState = false, symbol = @class, range = [], children = [], childNames = []>
    }
    simulation.vpi_object.anchor @t id 2 type 609 in 0 parent @c ordinal 0 hierarchy "pkg::C::T" debug "T" {unresolved_type_parameter}
    // expected-error @+1 {{class parameter inventory is supplied by exact declaration owners}}
    simulation.vpi_relation.decl <kind = symbol, symbol = @specialization> selector 41 iterate ordinal 0 to <kind = symbol, symbol = @t>
  }
}
