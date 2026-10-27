// RUN: talos-opt %s -split-input-file -verify-diagnostics -o /dev/null
#word = #simulation.vpi_type<kind = int, isSigned = true, isFourState = false, range = [31, 0], children = [], childNames = []>
module {
 simulation.design @zero {
  simulation.scope.decl 0 hierarchy "$root"
  simulation.scope.decl 1 parent 0 hierarchy "top" vpi_kind 32
  simulation.code_unit.decl 44 in 1 initial hierarchy "top.initial"
  simulation.code_unit.decl 45 in 1 task hierarchy "top.task"
  simulation.code_unit.decl 46 in 1 function hierarchy "top.function"
  simulation.statement.decl 11 in 45 scope 1 type 33 name "block" {is_scope}
  simulation.statement.decl 12 in 46 scope 1 type 33 name "other" {is_scope}
  simulation.statement.decl 13 in 45 scope 1 type 38 parent 11
  // expected-error @+1 {{local declaration and code-unit IDs must be nonzero}}
  simulation.vpi_local.decl @x id 0 in 45 scope 1 ordinal 0 hierarchy "top.task.x" debug "x" : i32 {vpi_type = #word}
 }
}

// -----

#word = #simulation.vpi_type<kind = int, isSigned = true, isFourState = false, range = [31, 0], children = [], childNames = []>
module {
 simulation.design @owner {
  simulation.scope.decl 0 hierarchy "$root"
  simulation.scope.decl 1 parent 0 hierarchy "top" vpi_kind 32
  simulation.code_unit.decl 44 in 1 initial hierarchy "top.initial"
  simulation.code_unit.decl 45 in 1 task hierarchy "top.task"
  simulation.code_unit.decl 46 in 1 function hierarchy "top.function"
  simulation.statement.decl 11 in 45 scope 1 type 33 name "block" {is_scope}
  simulation.statement.decl 12 in 46 scope 1 type 33 name "other" {is_scope}
  simulation.statement.decl 13 in 45 scope 1 type 38 parent 11
  // expected-error @+1 {{owner must name a code unit in its physical scope}}
  simulation.vpi_local.decl @x id 1 in 45 scope 0 ordinal 0 hierarchy "top.task.x" debug "x" : i32 {vpi_type = #word}
 }
}

// -----

#word = #simulation.vpi_type<kind = int, isSigned = true, isFourState = false, range = [31, 0], children = [], childNames = []>
module {
 simulation.design @nonscope {
  simulation.scope.decl 0 hierarchy "$root"
  simulation.scope.decl 1 parent 0 hierarchy "top" vpi_kind 32
  simulation.code_unit.decl 44 in 1 initial hierarchy "top.initial"
  simulation.code_unit.decl 45 in 1 task hierarchy "top.task"
  simulation.code_unit.decl 46 in 1 function hierarchy "top.function"
  simulation.statement.decl 11 in 45 scope 1 type 33 name "block" {is_scope}
  simulation.statement.decl 12 in 46 scope 1 type 33 name "other" {is_scope}
  simulation.statement.decl 13 in 45 scope 1 type 38 parent 11
  // expected-error @+1 {{statement scope must belong to the exact lexical code unit}}
  simulation.vpi_local.decl @x id 1 in 45 scope 1 within 13 ordinal 0 hierarchy "top.task.x" debug "x" : i32 {vpi_type = #word}
 }
}

// -----

#word = #simulation.vpi_type<kind = int, isSigned = true, isFourState = false, range = [31, 0], children = [], childNames = []>
module {
 simulation.design @foreignscope {
  simulation.scope.decl 0 hierarchy "$root"
  simulation.scope.decl 1 parent 0 hierarchy "top" vpi_kind 32
  simulation.code_unit.decl 44 in 1 initial hierarchy "top.initial"
  simulation.code_unit.decl 45 in 1 task hierarchy "top.task"
  simulation.code_unit.decl 46 in 1 function hierarchy "top.function"
  simulation.statement.decl 11 in 45 scope 1 type 33 name "block" {is_scope}
  simulation.statement.decl 12 in 46 scope 1 type 33 name "other" {is_scope}
  simulation.statement.decl 13 in 45 scope 1 type 38 parent 11
  // expected-error @+1 {{statement scope must belong to the exact lexical code unit}}
  simulation.vpi_local.decl @x id 1 in 45 scope 1 within 12 ordinal 0 hierarchy "top.task.x" debug "x" : i32 {vpi_type = #word}
 }
}

// -----

#word = #simulation.vpi_type<kind = int, isSigned = true, isFourState = false, range = [31, 0], children = [], childNames = []>
module {
 simulation.design @initial {
  simulation.scope.decl 0 hierarchy "$root"
  simulation.scope.decl 1 parent 0 hierarchy "top" vpi_kind 32
  simulation.code_unit.decl 44 in 1 initial hierarchy "top.initial"
  simulation.code_unit.decl 45 in 1 task hierarchy "top.task"
  simulation.code_unit.decl 46 in 1 function hierarchy "top.function"
  simulation.statement.decl 11 in 45 scope 1 type 33 name "block" {is_scope}
  simulation.statement.decl 12 in 46 scope 1 type 33 name "other" {is_scope}
  simulation.statement.decl 13 in 45 scope 1 type 38 parent 11
  // expected-error @+1 {{local without a statement scope requires a task or function owner}}
  simulation.vpi_local.decl @x id 1 in 44 scope 1 ordinal 0 hierarchy "top.task.x" debug "x" : i32 {vpi_type = #word}
 }
}

// -----

#word = #simulation.vpi_type<kind = int, isSigned = true, isFourState = false, range = [31, 0], children = [], childNames = []>
module {
 // expected-error @+1 {{procedural source declaration ordinals must be dense and unique within each scope}}
 simulation.design @sparse {
  simulation.scope.decl 0 hierarchy "$root"
  simulation.scope.decl 1 parent 0 hierarchy "top" vpi_kind 32
  simulation.code_unit.decl 44 in 1 initial hierarchy "top.initial"
  simulation.code_unit.decl 45 in 1 task hierarchy "top.task"
  simulation.code_unit.decl 46 in 1 function hierarchy "top.function"
  simulation.statement.decl 11 in 45 scope 1 type 33 name "block" {is_scope}
  simulation.statement.decl 12 in 46 scope 1 type 33 name "other" {is_scope}
  simulation.statement.decl 13 in 45 scope 1 type 38 parent 11
  simulation.vpi_local.decl @x id 1 in 45 scope 1 ordinal 1 hierarchy "top.task.x" debug "x" : i32 {vpi_type = #word}
 }
}

// -----

#word = #simulation.vpi_type<kind = int, isSigned = true, isFourState = false, range = [31, 0], children = [], childNames = []>
module {
 simulation.design @duplicate_id {
  simulation.scope.decl 0 hierarchy "$root"
  simulation.scope.decl 1 parent 0 hierarchy "top" vpi_kind 32
  simulation.code_unit.decl 44 in 1 initial hierarchy "top.initial"
  simulation.code_unit.decl 45 in 1 task hierarchy "top.task"
  simulation.code_unit.decl 46 in 1 function hierarchy "top.function"
  simulation.statement.decl 11 in 45 scope 1 type 33 name "block" {is_scope}
  simulation.statement.decl 12 in 46 scope 1 type 33 name "other" {is_scope}
  simulation.statement.decl 13 in 45 scope 1 type 38 parent 11
  simulation.vpi_local.decl @x id 1 in 45 scope 1 ordinal 0 hierarchy "top.task.x" debug "x" : i32 {vpi_type = #word}
  // expected-error @+1 {{duplicate VPI local declaration ID 1}}
  simulation.vpi_local.decl @y id 1 in 45 scope 1 ordinal 1 hierarchy "top.task.x" debug "y" : i32 {vpi_type = #word}
 }
}

// -----

#word = #simulation.vpi_type<kind = int, isSigned = true, isFourState = false, range = [31, 0], children = [], childNames = []>
module {
 simulation.design @duplicate_name {
  simulation.scope.decl 0 hierarchy "$root"
  simulation.scope.decl 1 parent 0 hierarchy "top" vpi_kind 32
  simulation.code_unit.decl 44 in 1 initial hierarchy "top.initial"
  simulation.code_unit.decl 45 in 1 task hierarchy "top.task"
  simulation.code_unit.decl 46 in 1 function hierarchy "top.function"
  simulation.statement.decl 11 in 45 scope 1 type 33 name "block" {is_scope}
  simulation.statement.decl 12 in 46 scope 1 type 33 name "other" {is_scope}
  simulation.statement.decl 13 in 45 scope 1 type 38 parent 11
  simulation.vpi_local.decl @x id 1 in 45 scope 1 ordinal 0 hierarchy "top.task.x" debug "x" : i32 {vpi_type = #word}
  // expected-error @+1 {{duplicates a declaration name in the same procedural scope}}
  simulation.vpi_local.decl @y id 2 in 45 scope 1 ordinal 1 hierarchy "top.task.x" debug "x" : i32 {vpi_type = #word}
 }
}

// -----

#word = #simulation.vpi_type<kind = int, isSigned = true, isFourState = false, range = [31, 0], children = [], childNames = []>
module {
 simulation.design @name {
  simulation.scope.decl 0 hierarchy "$root"
  simulation.scope.decl 1 parent 0 hierarchy "top" vpi_kind 32
  simulation.code_unit.decl 44 in 1 initial hierarchy "top.initial"
  simulation.code_unit.decl 45 in 1 task hierarchy "top.task"
  simulation.code_unit.decl 46 in 1 function hierarchy "top.function"
  simulation.statement.decl 11 in 45 scope 1 type 33 name "block" {is_scope}
  simulation.statement.decl 12 in 46 scope 1 type 33 name "other" {is_scope}
  simulation.statement.decl 13 in 45 scope 1 type 38 parent 11
  // expected-error @+1 {{vpiName must match the exact local declaration name}}
  simulation.vpi_local.decl @x id 1 in 45 scope 1 ordinal 0 hierarchy "top.task.x" debug "x" : i32 {vpi_type = #word, vpi_properties = #simulation.vpi_properties<[#simulation.vpi_property<selector = 2 : i32, value = "other">]>}
 }
}

// -----

#word = #simulation.vpi_type<kind = int, isSigned = true, isFourState = false, range = [31, 0], children = [], childNames = []>
module {
 simulation.design @automatic {
  simulation.scope.decl 0 hierarchy "$root"
  simulation.scope.decl 1 parent 0 hierarchy "top" vpi_kind 32
  simulation.code_unit.decl 44 in 1 initial hierarchy "top.initial"
  simulation.code_unit.decl 45 in 1 task hierarchy "top.task"
  simulation.code_unit.decl 46 in 1 function hierarchy "top.function"
  simulation.statement.decl 11 in 45 scope 1 type 33 name "block" {is_scope}
  simulation.statement.decl 12 in 46 scope 1 type 33 name "other" {is_scope}
  simulation.statement.decl 13 in 45 scope 1 type 38 parent 11
  // expected-error @+1 {{frame local declarations must be automatic}}
  simulation.vpi_local.decl @x id 1 in 45 scope 1 ordinal 0 hierarchy "top.task.x" debug "x" : i32 {vpi_type = #word, vpi_properties = #simulation.vpi_properties<[#simulation.vpi_property<selector = 50 : i32, value = false>]>}
 }
}
