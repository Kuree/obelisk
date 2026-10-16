// RUN: obelisk-opt %s -split-input-file -verify-diagnostics -o /dev/null
#word = #simulation.vpi_type<kind = int, isSigned = true, isFourState = false, range = [31, 0], children = [], childNames = []>
#array = #simulation.vpi_type<kind = unpacked_array, isSigned = false, isFourState = false, range = [1, 0], children = [#simulation.vpi_type<kind = unpacked_array, isSigned = false, isFourState = false, range = [2, 3], children = [#word], childNames = []>], childNames = []>
module {
 simulation.design @test {
  simulation.scope.decl 0 hierarchy "$root"
  simulation.scope.decl 1 parent 0 hierarchy "top" vpi_kind 32
  simulation.code_unit.decl 44 in 1 initial hierarchy "top.initial"
  simulation.statement.decl 11 in 44 scope 1 type 675 {is_scope}
  simulation.statement.decl 12 in 44 scope 1 type 675 {is_scope}
  simulation.storage.decl 0 in 1 : !simulation.unpacked_array<1 : 0 x !simulation.unpacked_array<2 : 3 x i32>> design hierarchy "top.a" debug "a" {vpi_type = #array}
  simulation.vpi_local.decl @i id 1 in 44 scope 1 within 11 ordinal 0 hierarchy "top.i" debug "i" : i32 {vpi_type = #word}
  // expected-error @+1 {{foreach statement ID must be positive}}
  simulation.vpi_foreach.decl 0 collection <kind = storage, id = 0 : i64> indices [@i, unit]
 }
}

// -----

#word = #simulation.vpi_type<kind = int, isSigned = true, isFourState = false, range = [31, 0], children = [], childNames = []>
#array = #simulation.vpi_type<kind = unpacked_array, isSigned = false, isFourState = false, range = [1, 0], children = [#simulation.vpi_type<kind = unpacked_array, isSigned = false, isFourState = false, range = [2, 3], children = [#word], childNames = []>], childNames = []>
module {
 simulation.design @test {
  simulation.scope.decl 0 hierarchy "$root"
  simulation.scope.decl 1 parent 0 hierarchy "top" vpi_kind 32
  simulation.code_unit.decl 44 in 1 initial hierarchy "top.initial"
  simulation.statement.decl 11 in 44 scope 1 type 675 {is_scope}
  simulation.statement.decl 12 in 44 scope 1 type 675 {is_scope}
  simulation.storage.decl 0 in 1 : !simulation.unpacked_array<1 : 0 x !simulation.unpacked_array<2 : 3 x i32>> design hierarchy "top.a" debug "a" {vpi_type = #array}
  simulation.vpi_local.decl @i id 1 in 44 scope 1 within 11 ordinal 0 hierarchy "top.i" debug "i" : i32 {vpi_type = #word}
  // expected-error @+1 {{requires a bounded nonempty index list}}
  simulation.vpi_foreach.decl 11 collection <kind = storage, id = 0 : i64> indices []
 }
}

// -----

#word = #simulation.vpi_type<kind = int, isSigned = true, isFourState = false, range = [31, 0], children = [], childNames = []>
#array = #simulation.vpi_type<kind = unpacked_array, isSigned = false, isFourState = false, range = [1, 0], children = [#simulation.vpi_type<kind = unpacked_array, isSigned = false, isFourState = false, range = [2, 3], children = [#word], childNames = []>], childNames = []>
module {
 simulation.design @test {
  simulation.scope.decl 0 hierarchy "$root"
  simulation.scope.decl 1 parent 0 hierarchy "top" vpi_kind 32
  simulation.code_unit.decl 44 in 1 initial hierarchy "top.initial"
  simulation.statement.decl 11 in 44 scope 1 type 675 {is_scope}
  simulation.statement.decl 12 in 44 scope 1 type 675 {is_scope}
  simulation.storage.decl 0 in 1 : !simulation.unpacked_array<1 : 0 x !simulation.unpacked_array<2 : 3 x i32>> design hierarchy "top.a" debug "a" {vpi_type = #array}
  simulation.vpi_local.decl @i id 1 in 44 scope 1 within 11 ordinal 0 hierarchy "top.i" debug "i" : i32 {vpi_type = #word}
  // expected-error @+1 {{index must be an exact local symbol or unit skip}}
  simulation.vpi_foreach.decl 11 collection <kind = storage, id = 0 : i64> indices [1 : i32]
 }
}

// -----

#word = #simulation.vpi_type<kind = int, isSigned = true, isFourState = false, range = [31, 0], children = [], childNames = []>
#array = #simulation.vpi_type<kind = unpacked_array, isSigned = false, isFourState = false, range = [1, 0], children = [#simulation.vpi_type<kind = unpacked_array, isSigned = false, isFourState = false, range = [2, 3], children = [#word], childNames = []>], childNames = []>
module {
 simulation.design @test {
  simulation.scope.decl 0 hierarchy "$root"
  simulation.scope.decl 1 parent 0 hierarchy "top" vpi_kind 32
  simulation.code_unit.decl 44 in 1 initial hierarchy "top.initial"
  simulation.statement.decl 11 in 44 scope 1 type 675 {is_scope}
  simulation.statement.decl 12 in 44 scope 1 type 675 {is_scope}
  simulation.storage.decl 0 in 1 : !simulation.unpacked_array<1 : 0 x !simulation.unpacked_array<2 : 3 x i32>> design hierarchy "top.a" debug "a" {vpi_type = #array}
  simulation.vpi_local.decl @i id 1 in 44 scope 1 within 11 ordinal 0 hierarchy "top.i" debug "i" : i32 {vpi_type = #word}
  // expected-error @+1 {{index must name an exact local declaration}}
  simulation.vpi_foreach.decl 11 collection <kind = storage, id = 0 : i64> indices [@missing]
 }
}

// -----

#word = #simulation.vpi_type<kind = int, isSigned = true, isFourState = false, range = [31, 0], children = [], childNames = []>
#array = #simulation.vpi_type<kind = unpacked_array, isSigned = false, isFourState = false, range = [1, 0], children = [#simulation.vpi_type<kind = unpacked_array, isSigned = false, isFourState = false, range = [2, 3], children = [#word], childNames = []>], childNames = []>
module {
 simulation.design @test {
  simulation.scope.decl 0 hierarchy "$root"
  simulation.scope.decl 1 parent 0 hierarchy "top" vpi_kind 32
  simulation.code_unit.decl 44 in 1 initial hierarchy "top.initial"
  simulation.statement.decl 11 in 44 scope 1 type 675 {is_scope}
  simulation.statement.decl 12 in 44 scope 1 type 675 {is_scope}
  simulation.storage.decl 0 in 1 : !simulation.unpacked_array<1 : 0 x !simulation.unpacked_array<2 : 3 x i32>> design hierarchy "top.a" debug "a" {vpi_type = #array}
  simulation.vpi_local.decl @i id 1 in 44 scope 1 within 11 ordinal 0 hierarchy "top.i" debug "i" : i32 {vpi_type = #word}
  // expected-error @+1 {{collection must name a whole fixed-array variable}}
  simulation.vpi_foreach.decl 11 collection <kind = statement, id = 11 : i64> indices [@i, unit]
 }
}

// -----

#word = #simulation.vpi_type<kind = int, isSigned = true, isFourState = false, range = [31, 0], children = [], childNames = []>
#array = #simulation.vpi_type<kind = unpacked_array, isSigned = false, isFourState = false, range = [1, 0], children = [#simulation.vpi_type<kind = unpacked_array, isSigned = false, isFourState = false, range = [2, 3], children = [#word], childNames = []>], childNames = []>
module {
 simulation.design @test {
  simulation.scope.decl 0 hierarchy "$root"
  simulation.scope.decl 1 parent 0 hierarchy "top" vpi_kind 32
  simulation.code_unit.decl 44 in 1 initial hierarchy "top.initial"
  simulation.statement.decl 11 in 44 scope 1 type 675 {is_scope}
  simulation.statement.decl 12 in 44 scope 1 type 675 {is_scope}
  simulation.storage.decl 0 in 1 : !simulation.unpacked_array<1 : 0 x !simulation.unpacked_array<2 : 3 x i32>> design hierarchy "top.a" debug "a" {vpi_type = #array}
  simulation.vpi_local.decl @i id 1 in 44 scope 1 within 11 ordinal 0 hierarchy "top.i" debug "i" : i32 {vpi_type = #word}
  // expected-error @+1 {{collection must prove all retained fixed-array dimensions}}
  simulation.vpi_foreach.decl 11 collection <kind = storage, id = 0 : i64> indices [@i, unit, unit]
 }
}

// -----

#word = #simulation.vpi_type<kind = int, isSigned = true, isFourState = false, range = [31, 0], children = [], childNames = []>
#array = #simulation.vpi_type<kind = unpacked_array, isSigned = false, isFourState = false, range = [1, 0], children = [#simulation.vpi_type<kind = unpacked_array, isSigned = false, isFourState = false, range = [2, 3], children = [#word], childNames = []>], childNames = []>
module {
 simulation.design @test {
  simulation.scope.decl 0 hierarchy "$root"
  simulation.scope.decl 1 parent 0 hierarchy "top" vpi_kind 32
  simulation.code_unit.decl 44 in 1 initial hierarchy "top.initial"
  simulation.statement.decl 11 in 44 scope 1 type 4 {is_scope}
  simulation.statement.decl 12 in 44 scope 1 type 675 {is_scope}
  simulation.storage.decl 0 in 1 : !simulation.unpacked_array<1 : 0 x !simulation.unpacked_array<2 : 3 x i32>> design hierarchy "top.a" debug "a" {vpi_type = #array}
  simulation.vpi_local.decl @i id 1 in 44 scope 1 within 11 ordinal 0 hierarchy "top.i" debug "i" : i32 {vpi_type = #word}
  // expected-error @+1 {{requires one exact retained foreach statement}}
  simulation.vpi_foreach.decl 11 collection <kind = storage, id = 0 : i64> indices [@i, unit]
 }
}

// -----

#word = #simulation.vpi_type<kind = int, isSigned = true, isFourState = false, range = [31, 0], children = [], childNames = []>
#array = #simulation.vpi_type<kind = unpacked_array, isSigned = false, isFourState = false, range = [1, 0], children = [#simulation.vpi_type<kind = unpacked_array, isSigned = false, isFourState = false, range = [2, 3], children = [#word], childNames = []>], childNames = []>
module {
 simulation.design @test {
  simulation.scope.decl 0 hierarchy "$root"
  simulation.scope.decl 1 parent 0 hierarchy "top" vpi_kind 32
  simulation.code_unit.decl 44 in 1 initial hierarchy "top.initial"
  simulation.statement.decl 11 in 44 scope 1 type 675 {is_scope}
  simulation.statement.decl 12 in 44 scope 1 type 675 {is_scope}
  simulation.storage.decl 0 in 1 : !simulation.unpacked_array<1 : 0 x !simulation.unpacked_array<2 : 3 x i32>> design hierarchy "top.a" debug "a" {vpi_type = #array}
  simulation.vpi_local.decl @i id 1 in 44 scope 1 within 11 ordinal 0 hierarchy "top.i" debug "i" : i32 {vpi_type = #word}
  // expected-error @+1 {{index must be a unique int declaration of this foreach scope}}
  simulation.vpi_foreach.decl 11 collection <kind = storage, id = 0 : i64> indices [@i, @i]
 }
}

// -----

#word = #simulation.vpi_type<kind = int, isSigned = true, isFourState = false, range = [31, 0], children = [], childNames = []>
#array = #simulation.vpi_type<kind = unpacked_array, isSigned = false, isFourState = false, range = [1, 0], children = [#simulation.vpi_type<kind = unpacked_array, isSigned = false, isFourState = false, range = [2, 3], children = [#word], childNames = []>], childNames = []>
module {
 simulation.design @test {
  simulation.scope.decl 0 hierarchy "$root"
  simulation.scope.decl 1 parent 0 hierarchy "top" vpi_kind 32
  simulation.code_unit.decl 44 in 1 initial hierarchy "top.initial"
  simulation.statement.decl 11 in 44 scope 1 type 675 {is_scope}
  simulation.statement.decl 12 in 44 scope 1 type 675 {is_scope}
  simulation.storage.decl 0 in 1 : !simulation.unpacked_array<1 : 0 x !simulation.unpacked_array<2 : 3 x i32>> design hierarchy "top.a" debug "a" {vpi_type = #array}
  simulation.vpi_local.decl @i id 1 in 44 scope 1 within 12 ordinal 0 hierarchy "top.i" debug "i" : i32 {vpi_type = #word}
  // expected-error @+1 {{index must be a unique int declaration of this foreach scope}}
  simulation.vpi_foreach.decl 11 collection <kind = storage, id = 0 : i64> indices [@i, unit]
 }
}

// -----

#word = #simulation.vpi_type<kind = int, isSigned = true, isFourState = false, range = [31, 0], children = [], childNames = []>
#array = #simulation.vpi_type<kind = unpacked_array, isSigned = false, isFourState = false, range = [1, 0], children = [#simulation.vpi_type<kind = unpacked_array, isSigned = false, isFourState = false, range = [2, 3], children = [#word], childNames = []>], childNames = []>
module {
 simulation.design @test {
  simulation.scope.decl 0 hierarchy "$root"
  simulation.scope.decl 1 parent 0 hierarchy "top" vpi_kind 32
  simulation.code_unit.decl 44 in 1 initial hierarchy "top.initial"
  simulation.statement.decl 11 in 44 scope 1 type 675 {is_scope}
  simulation.statement.decl 12 in 44 scope 1 type 675 {is_scope}
  simulation.storage.decl 0 in 1 : !simulation.unpacked_array<1 : 0 x !simulation.unpacked_array<2 : 3 x i32>> design hierarchy "top.a" debug "a" {vpi_type = #array}
  simulation.vpi_local.decl @i id 1 in 44 scope 1 within 11 ordinal 0 hierarchy "top.i" debug "i" : i32 {vpi_type = #word}
  // expected-error @+1 {{collection must prove all retained fixed-array dimensions}}
  simulation.vpi_foreach.decl 11 collection <kind = storage, id = 99 : i64> indices [@i, unit]
 }
}

// -----

#word = #simulation.vpi_type<kind = int, isSigned = true, isFourState = false, range = [31, 0], children = [], childNames = []>
#array = #simulation.vpi_type<kind = unpacked_array, isSigned = false, isFourState = false, range = [1, 0], children = [#simulation.vpi_type<kind = unpacked_array, isSigned = false, isFourState = false, range = [2, 3], children = [#word], childNames = []>], childNames = []>
module {
 simulation.design @test {
  simulation.scope.decl 0 hierarchy "$root"
  simulation.scope.decl 1 parent 0 hierarchy "top" vpi_kind 32
  simulation.code_unit.decl 44 in 1 initial hierarchy "top.initial"
  simulation.statement.decl 11 in 44 scope 1 type 675 {is_scope}
  simulation.statement.decl 12 in 44 scope 1 type 675 {is_scope}
  simulation.storage.decl 0 in 1 : !simulation.unpacked_array<1 : 0 x !simulation.unpacked_array<2 : 3 x i32>> design hierarchy "top.a" debug "a" {vpi_type = #array}
  simulation.vpi_local.decl @i id 1 in 44 scope 1 within 11 ordinal 0 hierarchy "top.i" debug "i" : i32 {vpi_type = #word}
  // expected-error @+1 {{symbolic collection must name an exact local declaration}}
  simulation.vpi_foreach.decl 11 collection <kind = symbol, symbol = @missing> indices [@i, unit]
 }
}

// -----

#word = #simulation.vpi_type<kind = int, isSigned = true, isFourState = false, range = [31, 0], children = [], childNames = []>
#array = #simulation.vpi_type<kind = unpacked_array, isSigned = false, isFourState = false, range = [1, 0], children = [#simulation.vpi_type<kind = unpacked_array, isSigned = false, isFourState = false, range = [2, 3], children = [#word], childNames = []>], childNames = []>
module {
 simulation.design @test {
  simulation.scope.decl 0 hierarchy "$root"
  simulation.scope.decl 1 parent 0 hierarchy "top" vpi_kind 32
  simulation.code_unit.decl 44 in 1 initial hierarchy "top.initial"
  simulation.statement.decl 11 in 44 scope 1 type 675 {is_scope}
  simulation.statement.decl 12 in 44 scope 1 type 675 {is_scope}
  simulation.storage.decl 0 in 1 : !simulation.unpacked_array<1 : 0 x !simulation.unpacked_array<2 : 3 x i32>> design hierarchy "top.a" debug "a" {vpi_type = #array}
  // expected-error @+1 {{foreach local declaration is absent from its index list}}
  simulation.vpi_local.decl @i id 1 in 44 scope 1 within 11 ordinal 0 hierarchy "top.i" debug "i" : i32 {vpi_type = #word}
  simulation.vpi_foreach.decl 11 collection <kind = storage, id = 0 : i64> indices [unit]
 }
}
