// RUN: obelisk-opt %s -split-input-file -verify-diagnostics

module {
  simulation.design @d {
    simulation.scope.decl 0
    simulation.vpi_object.anchor @pkg id 0 type 600 in 0 ordinal 0 hierarchy "pkg" debug "pkg"
    simulation.vpi_object.anchor @class id 1 type 652 in 0 parent @pkg ordinal 0 hierarchy "pkg::G" debug "G"
    // expected-error @+1 {{unresolved type parameter requires a lexical owner and no effective payload}}
    simulation.vpi_object.anchor @t id 2 type 609 in 0 parent @class ordinal 0 hierarchy "pkg::G::T" debug "T" {unresolved_type_parameter, vpi_type = #simulation.vpi_type<kind = int, isSigned = true, isFourState = false, range = [31, 0], children = [], childNames = []>}
  }
}

// -----

module {
  simulation.design @d {
    simulation.scope.decl 0
    simulation.vpi_object.anchor @pkg id 0 type 600 in 0 ordinal 0 hierarchy "pkg" debug "pkg"
    simulation.vpi_object.anchor @class id 1 type 652 in 0 parent @pkg ordinal 0 hierarchy "pkg::G" debug "G"
    // expected-error @+1 {{unresolved type parameter requires a lexical class definition owner}}
    simulation.vpi_object.anchor @t id 2 type 609 in 0 parent @pkg ordinal 1 hierarchy "pkg::T" debug "T" {unresolved_type_parameter}
  }
}

// -----

module {
  simulation.design @d {
    simulation.scope.decl 0
    simulation.vpi_object.anchor @pkg id 0 type 600 in 0 ordinal 0 hierarchy "pkg" debug "pkg"
    simulation.vpi_object.anchor @class id 1 type 652 in 0 parent @pkg ordinal 0 hierarchy "pkg::G" debug "G"
    // expected-error @+1 {{unavailable default requires a parameter and nonempty original syntax}}
    simulation.vpi_object.anchor @t id 2 type 609 in 0 parent @class ordinal 0 hierarchy "pkg::G::T" debug "T" {unresolved_type_parameter, unavailable_default = ""}
  }
}

// -----

module {
  simulation.design @d {
    simulation.scope.decl 0
    simulation.vpi_object.anchor @pkg id 0 type 600 in 0 ordinal 0 hierarchy "pkg" debug "pkg"
    simulation.vpi_object.anchor @class id 1 type 652 in 0 parent @pkg ordinal 0 hierarchy "pkg::G" debug "G"
    simulation.vpi_object.anchor @t id 2 type 609 in 0 parent @class ordinal 0 hierarchy "pkg::G::T" debug "T" {unresolved_type_parameter, unavailable_default = "T[3:0]"}
    simulation.vpi_typespec.decl @default id 0 in 0 owner @t hierarchy "" debug "" {origin = 5 : i32, target_type = #simulation.vpi_type<kind = int, isSigned = true, isFourState = false, range = [31, 0], children = [], childNames = []>}
    // expected-error @+1 {{unavailable source default cannot have an Expr relation}}
    simulation.vpi_relation.decl <kind = symbol, symbol = @t> selector 102 handle ordinal 0 to <kind = symbol, symbol = @default>
  }
}

// -----

module {
  simulation.design @d {
    simulation.scope.decl 0
    simulation.vpi_object.anchor @pkg id 0 type 600 in 0 ordinal 0 hierarchy "pkg" debug "pkg"
    simulation.vpi_object.anchor @class id 1 type 652 in 0 parent @pkg ordinal 0 hierarchy "pkg::G" debug "G"
    simulation.vpi_object.anchor @t id 2 type 609 in 0 parent @class ordinal 0 hierarchy "pkg::G::T" debug "T" {unresolved_type_parameter}
    simulation.vpi_typespec.decl @default id 0 in 0 owner @t hierarchy "" debug "" {origin = 5 : i32, target_type = #simulation.vpi_type<kind = int, isSigned = true, isFourState = false, range = [31, 0], children = [], childNames = []>}
    // expected-error @+1 {{unresolved type parameter cannot reference an effective typespec}}
    simulation.vpi_relation.decl <kind = symbol, symbol = @t> selector 605 handle ordinal 0 to <kind = symbol, symbol = @default>
  }
}

// -----

module {
  simulation.design @d {
    simulation.scope.decl 0
    simulation.vpi_object.anchor @pkg id 0 type 600 in 0 ordinal 0 hierarchy "pkg" debug "pkg"
    simulation.vpi_object.anchor @class id 1 type 652 in 0 parent @pkg ordinal 0 hierarchy "pkg::G" debug "G"
    // expected-error @+1 {{source default requires a type parameter or type actual owner}}
    simulation.vpi_typespec.decl @default id 0 in 0 owner @class hierarchy "" debug "" {origin = 5 : i32, target_type = #simulation.vpi_type<kind = int, isSigned = true, isFourState = false, range = [31, 0], children = [], childNames = []>}
  }
}
