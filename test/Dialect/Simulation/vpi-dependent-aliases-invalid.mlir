// RUN: obelisk-opt %s -split-input-file -verify-diagnostics

module {
  simulation.design @d {
    simulation.scope.decl 0
    simulation.vpi_object.anchor @pkg id 0 type 600 in 0 ordinal 0 hierarchy "pkg" debug "pkg"
    simulation.vpi_object.anchor @class id 1 type 652 in 0 parent @pkg ordinal 0 hierarchy "pkg::G" debug "G"
    simulation.vpi_object.anchor @t id 2 type 609 in 0 parent @class ordinal 0 hierarchy "pkg::G::T" debug "T" {unresolved_type_parameter}
    // expected-error @+1 {{owner must be a lexical class definition}}
    simulation.vpi_unresolved_type_alias.decl @a id 0 in 0 owner @pkg alias @t hierarchy "pkg::G::A" debug "A"
  }
}

// -----

module {
  simulation.design @d {
    simulation.scope.decl 0
    simulation.vpi_object.anchor @pkg id 0 type 600 in 0 ordinal 0 hierarchy "pkg" debug "pkg"
    simulation.vpi_object.anchor @class id 1 type 652 in 0 parent @pkg ordinal 0 hierarchy "pkg::G" debug "G"
    simulation.vpi_object.anchor @t id 2 type 609 in 0 parent @class ordinal 0 hierarchy "pkg::G::T" debug "T" {unresolved_type_parameter}
    // expected-error @+1 {{target must be an unresolved type parameter or alias}}
    simulation.vpi_unresolved_type_alias.decl @a id 0 in 0 owner @class alias @class hierarchy "pkg::G::A" debug "A"
  }
}

// -----

module {
  simulation.design @d {
    simulation.scope.decl 0
    simulation.vpi_object.anchor @pkg id 0 type 600 in 0 ordinal 0 hierarchy "pkg" debug "pkg"
    simulation.vpi_object.anchor @class id 1 type 652 in 0 parent @pkg ordinal 0 hierarchy "pkg::G" debug "G"
    simulation.vpi_object.anchor @t id 2 type 609 in 0 parent @class ordinal 0 hierarchy "pkg::G::T" debug "T" {unresolved_type_parameter}
    simulation.vpi_object.anchor @other id 3 type 652 in 0 parent @pkg ordinal 1 hierarchy "pkg::H" debug "H"
    // expected-error @+1 {{target must belong to the same lexical class}}
    simulation.vpi_unresolved_type_alias.decl @a id 0 in 0 owner @other alias @t hierarchy "pkg::G::A" debug "A"
  }
}

// -----

module {
  simulation.design @d {
    simulation.scope.decl 0
    simulation.vpi_object.anchor @pkg id 0 type 600 in 0 ordinal 0 hierarchy "pkg" debug "pkg"
    simulation.vpi_object.anchor @class id 1 type 652 in 0 parent @pkg ordinal 0 hierarchy "pkg::G" debug "G"
    simulation.vpi_object.anchor @t id 2 type 609 in 0 parent @class ordinal 0 hierarchy "pkg::G::T" debug "T" {unresolved_type_parameter}
    // expected-error @+1 {{target must precede the typedef declaration}}
    simulation.vpi_unresolved_type_alias.decl @a id 0 in 0 owner @class alias @a hierarchy "pkg::G::A" debug "A"
  }
}

// -----

module {
  simulation.design @d {
    simulation.scope.decl 0
    simulation.vpi_object.anchor @pkg id 0 type 600 in 0 ordinal 0 hierarchy "pkg" debug "pkg"
    simulation.vpi_object.anchor @class id 1 type 652 in 0 parent @pkg ordinal 0 hierarchy "pkg::G" debug "G"
    simulation.vpi_object.anchor @t id 2 type 609 in 0 parent @class ordinal 0 hierarchy "pkg::G::T" debug "T" {unresolved_type_parameter}
    // expected-error @+1 {{target must precede the typedef declaration}}
    simulation.vpi_unresolved_type_alias.decl @a id 0 in 0 owner @class alias @b hierarchy "pkg::G::A" debug "A"
    simulation.vpi_unresolved_type_alias.decl @b id 1 in 0 owner @class alias @t hierarchy "pkg::G::A" debug "A"
  }
}

// -----

module {
  simulation.design @d {
    simulation.scope.decl 0
    simulation.vpi_object.anchor @pkg id 0 type 600 in 0 ordinal 0 hierarchy "pkg" debug "pkg"
    simulation.vpi_object.anchor @class id 1 type 652 in 0 parent @pkg ordinal 0 hierarchy "pkg::G" debug "G"
    simulation.vpi_object.anchor @t id 2 type 609 in 0 parent @class ordinal 0 hierarchy "pkg::G::T" debug "T" {unresolved_type_parameter}
    // expected-error @+1 {{unresolved typedef cannot carry an effective type or parameter payload}}
    simulation.vpi_unresolved_type_alias.decl @a id 0 in 0 owner @class alias @t hierarchy "pkg::G::A" debug "A" {constant_value = 0 : i32}
  }
}

// -----

module {
  simulation.design @d {
    simulation.scope.decl 0
    simulation.vpi_object.anchor @pkg id 0 type 600 in 0 ordinal 0 hierarchy "pkg" debug "pkg"
    simulation.vpi_object.anchor @class id 1 type 652 in 0 parent @pkg ordinal 0 hierarchy "pkg::G" debug "G"
    simulation.vpi_object.anchor @t id 2 type 609 in 0 parent @class ordinal 0 hierarchy "pkg::G::T" debug "T" {unresolved_type_parameter}
    simulation.vpi_unresolved_type_alias.decl @a id 0 in 0 owner @class alias @t hierarchy "pkg::G::A" debug "A"
    // expected-error @+1 {{duplicates a typedef name in its lexical owner}}
    simulation.vpi_unresolved_type_alias.decl @b id 1 in 0 owner @class alias @t hierarchy "pkg::G::A" debug "A"
  }
}

// -----

module {
  simulation.design @d {
    simulation.scope.decl 0
    simulation.vpi_object.anchor @pkg id 0 type 600 in 0 ordinal 0 hierarchy "pkg" debug "pkg"
    simulation.vpi_object.anchor @class id 1 type 652 in 0 parent @pkg ordinal 0 hierarchy "pkg::G" debug "G"
    simulation.vpi_object.anchor @t id 2 type 609 in 0 parent @class ordinal 0 hierarchy "pkg::G::T" debug "T" {unresolved_type_parameter}
    simulation.vpi_unresolved_type_alias.decl @a id 0 in 0 owner @class alias @t hierarchy "pkg::G::A" debug "A"
    // expected-error @+1 {{duplicate typespec ID}}
    simulation.vpi_unresolved_type_alias.decl @b id 0 in 0 owner @class alias @t hierarchy "pkg::G::B" debug "B"
  }
}

// -----

module {
  simulation.design @d {
    simulation.scope.decl 0
    simulation.vpi_object.anchor @pkg id 0 type 600 in 0 ordinal 0 hierarchy "pkg" debug "pkg"
    simulation.vpi_object.anchor @class id 1 type 652 in 0 parent @pkg ordinal 0 hierarchy "pkg::G" debug "G"
    simulation.vpi_object.anchor @t id 2 type 609 in 0 parent @class ordinal 0 hierarchy "pkg::G::T" debug "T" {unresolved_type_parameter}
    simulation.vpi_unresolved_type_alias.decl @a id 0 in 0 owner @class alias @t hierarchy "pkg::G::A" debug "A"
    // expected-error @+1 {{unresolved typedef relations are supplied by its declaration}}
    simulation.vpi_relation.decl <kind = symbol, symbol = @a> selector 102 handle ordinal 0 to <kind = symbol, symbol = @t>
  }
}

// -----

module {
  simulation.design @d {
    simulation.scope.decl 0
    simulation.vpi_object.anchor @pkg id 0 type 600 in 0 ordinal 0 hierarchy "pkg" debug "pkg"
    simulation.vpi_object.anchor @class id 1 type 652 in 0 parent @pkg ordinal 0 hierarchy "pkg::G" debug "G"
    simulation.vpi_object.anchor @t id 2 type 609 in 0 parent @class ordinal 0 hierarchy "pkg::G::T" debug "T" {unresolved_type_parameter}
    simulation.vpi_unresolved_type_alias.decl @a id 0 in 0 owner @class alias @t hierarchy "pkg::G::A" debug "A"
    // expected-error @+1 {{unresolved typedef is not a parameter declaration}}
    simulation.vpi_relation.decl <kind = symbol, symbol = @class> selector 41 iterate ordinal 0 to <kind = symbol, symbol = @a>
  }
}

// -----

module {
  simulation.design @d {
    simulation.scope.decl 0
    simulation.vpi_object.anchor @pkg id 0 type 600 in 0 ordinal 0 hierarchy "pkg" debug "pkg"
    simulation.vpi_object.anchor @class id 1 type 652 in 0 parent @pkg ordinal 0 hierarchy "pkg::G" debug "G"
    simulation.vpi_object.anchor @t id 2 type 609 in 0 parent @class ordinal 0 hierarchy "pkg::G::T" debug "T" {unresolved_type_parameter}
    // expected-error @+1 {{requires nonempty source typedef names without NUL bytes}}
    simulation.vpi_unresolved_type_alias.decl @a id 0 in 0 owner @class alias @t hierarchy "pkg::G::A" debug "A\00bad"
  }
}
