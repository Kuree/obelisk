// RUN: obelisk-opt %s --split-input-file --verify-diagnostics -o /dev/null

#word = #simulation.vpi_type<kind = int, isSigned = true, isFourState = false, range = [31, 0], children = [], childNames = []>
module {
  simulation.design @types {
    simulation.scope.decl 0 hierarchy "$root"
    simulation.scope.decl 1 parent 0 hierarchy "top" vpi_kind 32
    simulation.scope.decl 2 parent 0 hierarchy "other" vpi_kind 32
    simulation.code_unit.decl 1 in 1 function hierarchy "top.f"
    simulation.vpi_object.anchor @top id 0 type 32 in 1 ordinal 0 hierarchy "top" debug "top" {backing = #simulation.vpi_backing<kind = scope, id = 1 : i64>}
    simulation.vpi_object.anchor @f id 1 type 20 in 1 parent @top ordinal 0 hierarchy "top.f" debug "f" {backing = #simulation.vpi_backing<kind = code_unit, id = 1 : i64>}
    simulation.statement.decl 10 in 1 scope 1 type 33 name "block" {is_scope}
    // expected-error @+1 {{requires exactly one symbolic or statement-scope owner}}
    simulation.vpi_typespec.decl @type id 0 in 1 hierarchy "top.f.block.T" debug "T" {target_type = #word}
  }
}

// -----

#word = #simulation.vpi_type<kind = int, isSigned = true, isFourState = false, range = [31, 0], children = [], childNames = []>
module {
  simulation.design @types {
    simulation.scope.decl 0 hierarchy "$root"
    simulation.scope.decl 1 parent 0 hierarchy "top" vpi_kind 32
    simulation.scope.decl 2 parent 0 hierarchy "other" vpi_kind 32
    simulation.code_unit.decl 1 in 1 function hierarchy "top.f"
    simulation.vpi_object.anchor @top id 0 type 32 in 1 ordinal 0 hierarchy "top" debug "top" {backing = #simulation.vpi_backing<kind = scope, id = 1 : i64>}
    simulation.vpi_object.anchor @f id 1 type 20 in 1 parent @top ordinal 0 hierarchy "top.f" debug "f" {backing = #simulation.vpi_backing<kind = code_unit, id = 1 : i64>}
    simulation.statement.decl 10 in 1 scope 1 type 33 name "block" {is_scope}
    // expected-error @+1 {{requires exactly one symbolic or statement-scope owner}}
    simulation.vpi_typespec.decl @type id 0 in 1 owner @f within 10 hierarchy "top.f.block.T" debug "T" {target_type = #word}
  }
}

// -----

#word = #simulation.vpi_type<kind = int, isSigned = true, isFourState = false, range = [31, 0], children = [], childNames = []>
module {
  simulation.design @types {
    simulation.scope.decl 0 hierarchy "$root"
    simulation.scope.decl 1 parent 0 hierarchy "top" vpi_kind 32
    simulation.scope.decl 2 parent 0 hierarchy "other" vpi_kind 32
    simulation.code_unit.decl 1 in 1 function hierarchy "top.f"
    simulation.vpi_object.anchor @top id 0 type 32 in 1 ordinal 0 hierarchy "top" debug "top" {backing = #simulation.vpi_backing<kind = scope, id = 1 : i64>}
    simulation.vpi_object.anchor @f id 1 type 20 in 1 parent @top ordinal 0 hierarchy "top.f" debug "f" {backing = #simulation.vpi_backing<kind = code_unit, id = 1 : i64>}
    simulation.statement.decl 10 in 1 scope 1 type 33 name "block" {is_scope}
    // expected-error @+1 {{references an unknown statement-scope owner}}
    simulation.vpi_typespec.decl @type id 0 in 1 within 99 hierarchy "top.f.block.T" debug "T" {target_type = #word}
  }
}

// -----

#word = #simulation.vpi_type<kind = int, isSigned = true, isFourState = false, range = [31, 0], children = [], childNames = []>
module {
  simulation.design @types {
    simulation.scope.decl 0 hierarchy "$root"
    simulation.scope.decl 1 parent 0 hierarchy "top" vpi_kind 32
    simulation.scope.decl 2 parent 0 hierarchy "other" vpi_kind 32
    simulation.code_unit.decl 1 in 1 function hierarchy "top.f"
    simulation.vpi_object.anchor @top id 0 type 32 in 1 ordinal 0 hierarchy "top" debug "top" {backing = #simulation.vpi_backing<kind = scope, id = 1 : i64>}
    simulation.vpi_object.anchor @f id 1 type 20 in 1 parent @top ordinal 0 hierarchy "top.f" debug "f" {backing = #simulation.vpi_backing<kind = code_unit, id = 1 : i64>}
    simulation.statement.decl 10 in 1 scope 1 type 33 name "block" {is_scope}
    // expected-error @+1 {{scope ID does not match the statement owner}}
    simulation.vpi_typespec.decl @type id 0 in 2 within 10 hierarchy "top.f.block.T" debug "T" {target_type = #word}
  }
}

// -----

#word = #simulation.vpi_type<kind = int, isSigned = true, isFourState = false, range = [31, 0], children = [], childNames = []>
module {
  simulation.design @types {
    simulation.scope.decl 0 hierarchy "$root"
    simulation.scope.decl 1 parent 0 hierarchy "top" vpi_kind 32
    simulation.scope.decl 2 parent 0 hierarchy "other" vpi_kind 32
    simulation.code_unit.decl 1 in 1 function hierarchy "top.f"
    simulation.vpi_object.anchor @top id 0 type 32 in 1 ordinal 0 hierarchy "top" debug "top" {backing = #simulation.vpi_backing<kind = scope, id = 1 : i64>}
    simulation.vpi_object.anchor @f id 1 type 20 in 1 parent @top ordinal 0 hierarchy "top.f" debug "f" {backing = #simulation.vpi_backing<kind = code_unit, id = 1 : i64>}
    simulation.statement.decl 10 in 1 scope 1 type 33 name "block" {is_scope}
    simulation.statement.decl 11 in 1 scope 1 type 38
    // expected-error @+1 {{owner must name a procedural statement scope}}
    simulation.vpi_typespec.decl @type id 0 in 1 within 11 hierarchy "top.f.block.T" debug "T" {target_type = #word}
  }
}

// -----

#word = #simulation.vpi_type<kind = int, isSigned = true, isFourState = false, range = [31, 0], children = [], childNames = []>
module {
  simulation.design @types {
    simulation.scope.decl 0 hierarchy "$root"
    simulation.scope.decl 1 parent 0 hierarchy "top" vpi_kind 32
    simulation.scope.decl 2 parent 0 hierarchy "other" vpi_kind 32
    simulation.code_unit.decl 1 in 1 function hierarchy "top.f"
    simulation.vpi_object.anchor @top id 0 type 32 in 1 ordinal 0 hierarchy "top" debug "top" {backing = #simulation.vpi_backing<kind = scope, id = 1 : i64>}
    simulation.vpi_object.anchor @f id 1 type 20 in 1 parent @top ordinal 0 hierarchy "top.f" debug "f" {backing = #simulation.vpi_backing<kind = code_unit, id = 1 : i64>}
    simulation.statement.decl 10 in 1 scope 1 type 33 name "block" {is_scope}
    // expected-error @+1 {{statement-scope ownership is only valid for typedef and anonymous enum typespecs}}
    simulation.vpi_typespec.decl @type id 0 in 1 within 10 hierarchy "top.f.block.T" debug "T" {origin = 5 : i32, target_type = #word}
  }
}

// -----

#word = #simulation.vpi_type<kind = int, isSigned = true, isFourState = false, range = [31, 0], children = [], childNames = []>
module {
  simulation.design @types {
    simulation.scope.decl 0 hierarchy "$root"
    simulation.scope.decl 1 parent 0 hierarchy "top" vpi_kind 32
    simulation.scope.decl 2 parent 0 hierarchy "other" vpi_kind 32
    simulation.code_unit.decl 1 in 1 function hierarchy "top.f"
    simulation.vpi_object.anchor @top id 0 type 32 in 1 ordinal 0 hierarchy "top" debug "top" {backing = #simulation.vpi_backing<kind = scope, id = 1 : i64>}
    simulation.vpi_object.anchor @f id 1 type 20 in 1 parent @top ordinal 0 hierarchy "top.f" debug "f" {backing = #simulation.vpi_backing<kind = code_unit, id = 1 : i64>}
    simulation.statement.decl 10 in 1 scope 1 type 33 name "block" {is_scope}
    simulation.vpi_typespec.decl @first id 0 in 1 within 10 hierarchy "top.f.block.T" debug "T" {target_type = #word}
    // expected-error @+1 {{duplicates a typedef name in its lexical owner}}
    simulation.vpi_typespec.decl @second id 1 in 1 within 10 hierarchy "top.f.block.other" debug "T" {target_type = #word}
  }
}

// -----
#word = #simulation.vpi_type<kind = int, isSigned = true, isFourState = false, range = [31, 0], children = [], childNames = []>
#enum = #simulation.vpi_type<kind = enum, isSigned = true, isFourState = false, name = "E", range = [], children = [#word], childNames = []>
module {
 simulation.design @bad_enum {
  simulation.scope.decl 0 hierarchy "$root"
  simulation.scope.decl 1 parent 0 hierarchy "top" vpi_kind 32
  simulation.vpi_object.anchor @top id 0 type 32 in 1 ordinal 0 hierarchy "top" debug "top" {backing = #simulation.vpi_backing<kind = scope, id = 1 : i64>}
  simulation.vpi_typespec.decl @E id 0 in 1 owner @top hierarchy "top.E" debug "E" {target_type = #enum}
  // expected-error @+1 {{immutable enum value must match its packed enum base}}
  simulation.vpi_enum_const.decl 0 enum @E ordinal 0 name "A" value "1" {immutable_value = #simulation.frozen_constant<value = [1 : i32, 0 : i32], isSigned = false> : i32}
 }
}

// -----
#word = #simulation.vpi_type<kind = int, isSigned = true, isFourState = false, range = [31, 0], children = [], childNames = []>
#enum = #simulation.vpi_type<kind = enum, isSigned = true, isFourState = false, name = "E", range = [], children = [#word], childNames = []>
module {
 simulation.design @bad_enum {
  simulation.scope.decl 0 hierarchy "$root"
  simulation.scope.decl 1 parent 0 hierarchy "top" vpi_kind 32
  simulation.vpi_object.anchor @top id 0 type 32 in 1 ordinal 0 hierarchy "top" debug "top" {backing = #simulation.vpi_backing<kind = scope, id = 1 : i64>}
  simulation.vpi_typespec.decl @E id 0 in 1 owner @top hierarchy "top.E" debug "E" {target_type = #enum}
  // expected-error @+1 {{does not normalize to executable type 'i16'}}
  simulation.vpi_enum_const.decl 0 enum @E ordinal 0 name "A" value "1" {immutable_value = #simulation.frozen_constant<value = [1 : i16, 0 : i16], isSigned = true> : i16}
 }
}
