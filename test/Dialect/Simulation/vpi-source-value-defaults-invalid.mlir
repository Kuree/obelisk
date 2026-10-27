// RUN: talos-opt %s -split-input-file -verify-diagnostics

module {
  simulation.design @d {
    simulation.scope.decl 0
    simulation.vpi_object.anchor @pkg id 0 type 600 in 0 ordinal 0 hierarchy "pkg" debug "pkg"
    simulation.vpi_object.anchor @class id 1 type 652 in 0 parent @pkg ordinal 0 hierarchy "pkg::G" debug "G"
    // expected-error @+1 {{unresolved value parameter requires a lexical owner and no effective payload}}
    simulation.vpi_object.anchor @p id 2 type 41 in 0 parent @class ordinal 0 hierarchy "pkg::G::P" debug "P" {unresolved_value_parameter, vpi_type = #simulation.vpi_type<kind = int, isSigned = true, isFourState = false, range = [31, 0], children = [], childNames = []>, immutable_value = #simulation.frozen_constant<value = [1 : i32, 0 : i32], isSigned = true> : i32}
  }
}

// -----

module {
  simulation.design @d {
    simulation.scope.decl 0
    simulation.vpi_object.anchor @pkg id 0 type 600 in 0 ordinal 0 hierarchy "pkg" debug "pkg"
    simulation.vpi_object.anchor @class id 1 type 652 in 0 parent @pkg ordinal 0 hierarchy "pkg::G" debug "G"
    // expected-error @+1 {{unresolved value parameter requires a lexical class definition owner}}
    simulation.vpi_object.anchor @p id 2 type 41 in 0 parent @pkg ordinal 0 hierarchy "pkg::P" debug "P" {unresolved_value_parameter}
  }
}

// -----

module {
  simulation.design @d {
    simulation.scope.decl 0
    simulation.vpi_object.anchor @pkg id 0 type 600 in 0 ordinal 0 hierarchy "pkg" debug "pkg"
    simulation.vpi_object.anchor @class id 1 type 652 in 0 parent @pkg ordinal 0 hierarchy "pkg::G" debug "G"
    simulation.vpi_object.anchor @p id 2 type 41 in 0 parent @class ordinal 0 hierarchy "pkg::G::P" debug "P" {unresolved_value_parameter}
    // expected-error @+1 {{source constant requires an exact immutable value}}
    simulation.vpi_object.anchor @e id 3 type 7 in 0 parent @p ordinal 0 hierarchy "" debug ""
  }
}

// -----

module {
  simulation.design @d {
    simulation.scope.decl 0
    simulation.vpi_object.anchor @pkg id 0 type 600 in 0 ordinal 0 hierarchy "pkg" debug "pkg"
    simulation.vpi_object.anchor @class id 1 type 652 in 0 parent @pkg ordinal 0 hierarchy "pkg::G" debug "G"
    simulation.vpi_object.anchor @p id 2 type 41 in 0 parent @class ordinal 0 hierarchy "pkg::G::P" debug "P" {unresolved_value_parameter}
    // expected-error @+1 {{source expression or primitive terminal requires unnamed immutable ownership}}
    simulation.vpi_object.anchor @e id 3 type 7 in 0 parent @p ordinal 0 hierarchy "named" debug ""
  }
}

// -----

module {
  simulation.design @d {
    simulation.scope.decl 0
    simulation.vpi_object.anchor @pkg id 0 type 600 in 0 ordinal 0 hierarchy "pkg" debug "pkg"
    simulation.vpi_object.anchor @class id 1 type 652 in 0 parent @pkg ordinal 0 hierarchy "pkg::G" debug "G"
    simulation.vpi_object.anchor @p id 2 type 41 in 0 parent @class ordinal 0 hierarchy "pkg::G::P" debug "P" {unresolved_value_parameter}
    // expected-error @+1 {{source integer literal requires an unaliased scalar integral type}}
    simulation.vpi_object.anchor @e id 3 type 7 in 0 parent @p ordinal 0 hierarchy "" debug "" {vpi_type = #simulation.vpi_type<kind = shortreal, isSigned = false, isFourState = false, range = [], children = [], childNames = []>, immutable_value = #simulation.frozen_constant<value = [1 : i32, 0 : i32], isSigned = false> : i32, vpi_properties = #simulation.vpi_properties<[#simulation.vpi_property<selector = 40 : i32, value = 7 : i32>]>}
    simulation.vpi_relation.decl <kind = symbol, symbol = @p> selector 102 handle ordinal 0 to <kind = symbol, symbol = @e>
  }
}

// -----

module {
  simulation.design @d {
    simulation.scope.decl 0
    simulation.vpi_object.anchor @pkg id 0 type 600 in 0 ordinal 0 hierarchy "pkg" debug "pkg"
    simulation.vpi_object.anchor @class id 1 type 652 in 0 parent @pkg ordinal 0 hierarchy "pkg::G" debug "G"
    simulation.vpi_object.anchor @p id 2 type 41 in 0 parent @class ordinal 0 hierarchy "pkg::G::P" debug "P" {unresolved_value_parameter}
    // expected-error @+1 {{source expression requires one owning default or operand relation}}
    simulation.vpi_object.anchor @e id 3 type 7 in 0 parent @p ordinal 0 hierarchy "" debug "" {vpi_type = #simulation.vpi_type<kind = int, isSigned = true, isFourState = false, range = [31, 0], children = [], childNames = []>, immutable_value = #simulation.frozen_constant<value = [1 : i32, 0 : i32], isSigned = true> : i32, vpi_properties = #simulation.vpi_properties<[#simulation.vpi_property<selector = 40 : i32, value = 7 : i32>]>}
  }
}

// -----


module {
  simulation.design @d {
    simulation.scope.decl 0
    simulation.vpi_object.anchor @pkg id 0 type 600 in 0 ordinal 0 hierarchy "pkg" debug "pkg"
    simulation.vpi_object.anchor @class id 1 type 652 in 0 parent @pkg ordinal 0 hierarchy "pkg::G" debug "G"
    simulation.vpi_object.anchor @p id 2 type 41 in 0 parent @class ordinal 0 hierarchy "pkg::G::P" debug "P" {unresolved_value_parameter}
    simulation.vpi_typespec.decl @word id 0 in 0 owner @pkg hierarchy "pkg::word" debug "word" {target_type = #simulation.vpi_type<kind = int, isSigned = true, isFourState = false, range = [31, 0], children = [], childNames = []>}
    // expected-error @+1 {{source integer literal requires an unaliased scalar integral type}}
    simulation.vpi_object.anchor @e id 3 type 7 in 0 parent @p ordinal 0 hierarchy "" debug "" {vpi_type = #simulation.vpi_type<kind = int, isSigned = true, isFourState = false, range = [31, 0], children = [], childNames = [], typedefAliases = [@word]>, immutable_value = #simulation.frozen_constant<value = [1 : i32, 0 : i32], isSigned = true> : i32, vpi_properties = #simulation.vpi_properties<[#simulation.vpi_property<selector = 40 : i32, value = 7 : i32>]>}
  }
}

// -----


module {
  simulation.design @d {
    simulation.scope.decl 0
    simulation.vpi_object.anchor @pkg id 0 type 600 in 0 ordinal 0 hierarchy "pkg" debug "pkg"
    simulation.vpi_object.anchor @class id 1 type 652 in 0 parent @pkg ordinal 0 hierarchy "pkg::G" debug "G"
    simulation.vpi_object.anchor @p id 2 type 41 in 0 parent @class ordinal 0 hierarchy "pkg::G::P" debug "P" {unresolved_value_parameter}
    // expected-error @+1 {{source reference requires an exact preceding parameter in the same declaration scope}}
    simulation.vpi_relation.decl <kind = symbol, symbol = @p> selector 102 handle ordinal 0 to <kind = symbol, symbol = @p>
  }
}

// -----

module {
  simulation.design @d {
    simulation.scope.decl 0
    simulation.vpi_object.anchor @pkg id 0 type 600 in 0 ordinal 0 hierarchy "pkg" debug "pkg"
    simulation.vpi_object.anchor @class id 1 type 652 in 0 parent @pkg ordinal 0 hierarchy "pkg::G" debug "G"
    simulation.vpi_object.anchor @p id 2 type 41 in 0 parent @class ordinal 0 hierarchy "pkg::G::P" debug "P" {unresolved_value_parameter}
    simulation.vpi_object.anchor @q id 3 type 41 in 0 parent @class ordinal 1 hierarchy "pkg::G::Q" debug "Q" {unresolved_value_parameter}
    simulation.vpi_object.anchor @add id 4 type 39 in 0 parent @q ordinal 0 hierarchy "" debug "" {vpi_properties = #simulation.vpi_properties<[#simulation.vpi_property<selector = 39 : i32, value = 24 : i32>]>}
    simulation.vpi_object.anchor @literal id 5 type 7 in 0 parent @add ordinal 0 hierarchy "" debug "" {vpi_type = #simulation.vpi_type<kind = int, isSigned = true, isFourState = false, range = [31, 0], children = [], childNames = []>, immutable_value = #simulation.frozen_constant<value = [2 : i32, 0 : i32], isSigned = true> : i32, vpi_properties = #simulation.vpi_properties<[#simulation.vpi_property<selector = 40 : i32, value = 7 : i32>]>}
    simulation.vpi_relation.decl <kind = symbol, symbol = @q> selector 102 handle ordinal 0 to <kind = symbol, symbol = @add>
    simulation.vpi_relation.decl <kind = symbol, symbol = @add> selector 97 iterate ordinal 0 to <kind = symbol, symbol = @p>
    // expected-error @+1 {{ordinals must be dense from zero for each source, selector, and access mode}}
    simulation.vpi_relation.decl <kind = symbol, symbol = @add> selector 97 iterate ordinal 2 to <kind = symbol, symbol = @literal>
  }
}

// -----

module {
  simulation.design @d {
    simulation.scope.decl 0
    simulation.vpi_object.anchor @pkg id 0 type 600 in 0 ordinal 0 hierarchy "pkg" debug "pkg"
    simulation.vpi_object.anchor @class id 1 type 652 in 0 parent @pkg ordinal 0 hierarchy "pkg::G" debug "G"
    simulation.vpi_object.anchor @p id 2 type 41 in 0 parent @class ordinal 0 hierarchy "pkg::G::P" debug "P" {unresolved_value_parameter}
    simulation.vpi_object.anchor @q id 3 type 41 in 0 parent @class ordinal 1 hierarchy "pkg::G::Q" debug "Q" {unresolved_value_parameter}
    simulation.vpi_object.anchor @add id 4 type 39 in 0 parent @q ordinal 0 hierarchy "" debug "" {vpi_properties = #simulation.vpi_properties<[#simulation.vpi_property<selector = 39 : i32, value = 24 : i32>]>}
    simulation.vpi_object.anchor @literal id 5 type 7 in 0 parent @add ordinal 1 hierarchy "" debug "" {vpi_type = #simulation.vpi_type<kind = int, isSigned = true, isFourState = false, range = [31, 0], children = [], childNames = []>, immutable_value = #simulation.frozen_constant<value = [2 : i32, 0 : i32], isSigned = true> : i32, vpi_properties = #simulation.vpi_properties<[#simulation.vpi_property<selector = 40 : i32, value = 7 : i32>]>}
    simulation.vpi_relation.decl <kind = symbol, symbol = @q> selector 102 handle ordinal 0 to <kind = symbol, symbol = @add>
    simulation.vpi_relation.decl <kind = symbol, symbol = @add> selector 97 iterate ordinal 0 to <kind = symbol, symbol = @p>
    // expected-error @+1 {{source expression ownership ordinals must follow owned operand order}}
    simulation.vpi_relation.decl <kind = symbol, symbol = @add> selector 97 iterate ordinal 1 to <kind = symbol, symbol = @literal>
  }
}

// -----

module {
  simulation.design @d {
    simulation.scope.decl 0
    simulation.vpi_object.anchor @pkg id 0 type 600 in 0 ordinal 0 hierarchy "pkg" debug "pkg"
    simulation.vpi_object.anchor @class id 1 type 652 in 0 parent @pkg ordinal 0 hierarchy "pkg::G" debug "G"
    simulation.vpi_object.anchor @p id 2 type 41 in 0 parent @class ordinal 0 hierarchy "pkg::G::P" debug "P" {unresolved_value_parameter}
    simulation.vpi_object.anchor @q id 3 type 41 in 0 parent @class ordinal 1 hierarchy "pkg::G::Q" debug "Q" {unresolved_value_parameter}
    simulation.vpi_object.anchor @add id 4 type 39 in 0 parent @q ordinal 0 hierarchy "" debug "" {vpi_properties = #simulation.vpi_properties<[#simulation.vpi_property<selector = 39 : i32, value = 24 : i32>]>}
    simulation.vpi_object.anchor @literal id 5 type 7 in 0 parent @add ordinal 0 hierarchy "" debug "" {vpi_type = #simulation.vpi_type<kind = int, isSigned = true, isFourState = false, range = [31, 0], children = [], childNames = []>, immutable_value = #simulation.frozen_constant<value = [2 : i32, 0 : i32], isSigned = true> : i32, vpi_properties = #simulation.vpi_properties<[#simulation.vpi_property<selector = 40 : i32, value = 7 : i32>]>}
    simulation.vpi_relation.decl <kind = symbol, symbol = @q> selector 102 handle ordinal 0 to <kind = symbol, symbol = @add>
    simulation.vpi_relation.decl <kind = symbol, symbol = @add> selector 97 iterate ordinal 0 to <kind = symbol, symbol = @literal>
    // expected-error @+1 {{duplicates a target in the same source, selector, and access mode}}
    simulation.vpi_relation.decl <kind = symbol, symbol = @add> selector 97 iterate ordinal 1 to <kind = symbol, symbol = @literal>
  }
}
