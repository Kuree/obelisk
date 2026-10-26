// RUN: obelisk-opt %s -split-input-file -verify-diagnostics -o /dev/null
#bits = #simulation.vpi_type<kind = bit, isSigned = false, isFourState = false, range = [3, 0], children = [], childNames = []>
module {
 simulation.design @negative {
  simulation.scope.decl 0 hierarchy "$root"
  simulation.scope.decl 1 parent 0 hierarchy "top" vpi_kind 32
  simulation.scope.decl 2 parent 0 hierarchy "other" vpi_kind 32
  simulation.code_unit.decl 44 in 1 initial hierarchy "top.initial"
  simulation.code_unit.decl 45 in 1 function hierarchy "top.f"
  simulation.statement.decl 11 in 44 scope 1 type 19
  simulation.vpi_statement_relation.decl code_unit 44 type 24 selector 104 ordinal 0 modes 1 to 11
  simulation.vpi_call.decl @call id 1 in 44 scope 1 type 19 name "f" statement 11 within 11 callee 45
  simulation.storage.decl 0 in 1 : i4 design hierarchy "top.a" {vpi_type = #bits}
  // expected-error @+1 {{expression scope disagrees with its source occurrence}}
  simulation.vpi_expression.decl @arg id 1 type 7 in 2 owned_by #simulation.vpi_ref<kind = symbol, symbol = @call> selector 89 iterate ordinal 0 : i4 {vpi_type = #bits, immutable_value = #simulation.frozen_constant<value = [1 : i4, 0 : i4], isSigned = false> : i4}
  simulation.vpi_relation.decl #simulation.vpi_ref<kind = symbol, symbol = @call> selector 89 iterate ordinal 0 to #simulation.vpi_ref<kind = symbol, symbol = @arg>
 }
}

// -----

#bits = #simulation.vpi_type<kind = bit, isSigned = false, isFourState = false, range = [3, 0], children = [], childNames = []>
module {
 simulation.design @negative {
  simulation.scope.decl 0 hierarchy "$root"
  simulation.scope.decl 1 parent 0 hierarchy "top" vpi_kind 32
  simulation.scope.decl 2 parent 0 hierarchy "other" vpi_kind 32
  simulation.code_unit.decl 44 in 1 initial hierarchy "top.initial"
  simulation.code_unit.decl 45 in 1 function hierarchy "top.f"
  simulation.statement.decl 11 in 44 scope 1 type 19
  simulation.vpi_statement_relation.decl code_unit 44 type 24 selector 104 ordinal 0 modes 1 to 11
  simulation.vpi_call.decl @call id 1 in 44 scope 1 type 19 name "f" statement 11 within 11 callee 45
  simulation.storage.decl 0 in 1 : i4 design hierarchy "top.a" {vpi_type = #bits}
  // expected-error @+1 {{computed occurrence cannot have multiple owners}}
  simulation.vpi_expression.decl @arg id 1 type 7 in 1 owned_by #simulation.vpi_ref<kind = symbol, symbol = @call> selector 89 iterate ordinal 0 : i4 {vpi_type = #bits, immutable_value = #simulation.frozen_constant<value = [1 : i4, 0 : i4], isSigned = false> : i4}
  simulation.vpi_relation.decl #simulation.vpi_ref<kind = symbol, symbol = @call> selector 89 iterate ordinal 0 to #simulation.vpi_ref<kind = symbol, symbol = @arg>
  simulation.vpi_relation.decl #simulation.vpi_ref<kind = symbol, symbol = @call> selector 89 iterate ordinal 1 to #simulation.vpi_ref<kind = symbol, symbol = @arg>
 }
}

// -----

#bits = #simulation.vpi_type<kind = bit, isSigned = false, isFourState = false, range = [3, 0], children = [], childNames = []>
module {
 simulation.design @negative {
  simulation.scope.decl 0 hierarchy "$root"
  simulation.scope.decl 1 parent 0 hierarchy "top" vpi_kind 32
  simulation.scope.decl 2 parent 0 hierarchy "other" vpi_kind 32
  simulation.code_unit.decl 44 in 1 initial hierarchy "top.initial"
  simulation.code_unit.decl 45 in 1 function hierarchy "top.f"
  simulation.statement.decl 11 in 44 scope 1 type 19
  simulation.vpi_statement_relation.decl code_unit 44 type 24 selector 104 ordinal 0 modes 1 to 11
  simulation.vpi_call.decl @call id 1 in 44 scope 1 type 19 name "f" statement 11 within 11 callee 45
  simulation.storage.decl 0 in 1 : i4 design hierarchy "top.a" {vpi_type = #bits}
  simulation.vpi_relation.decl #simulation.vpi_ref<kind = symbol, symbol = @call> selector 89 iterate ordinal 0 to #simulation.vpi_ref<kind = storage, id = 0 : i64>
  // expected-error @+1 {{overlaps another target at the same source, selector, mode, and ordinal}}
  simulation.vpi_relation.decl #simulation.vpi_ref<kind = statement, id = 11 : i64> selector 89 iterate ordinal 0 to #simulation.vpi_ref<kind = storage, id = 0 : i64>
 }
}

// -----

#bits = #simulation.vpi_type<kind = bit, isSigned = false, isFourState = false, range = [3, 0], children = [], childNames = []>
module {
 simulation.design @negative {
  simulation.scope.decl 0 hierarchy "$root"
  simulation.scope.decl 1 parent 0 hierarchy "top" vpi_kind 32
  simulation.scope.decl 2 parent 0 hierarchy "other" vpi_kind 32
  simulation.code_unit.decl 44 in 1 initial hierarchy "top.initial"
  simulation.code_unit.decl 45 in 1 function hierarchy "top.f"
  simulation.statement.decl 11 in 44 scope 1 type 19
  simulation.vpi_statement_relation.decl code_unit 44 type 24 selector 104 ordinal 0 modes 1 to 11
  simulation.vpi_call.decl @call id 1 in 44 scope 1 type 19 name "f" statement 11 within 11 callee 45
  simulation.storage.decl 0 in 1 : i4 design hierarchy "top.a" {vpi_type = #bits}
  // expected-error @+1 {{ordinals must be dense from zero}}
  simulation.vpi_relation.decl #simulation.vpi_ref<kind = symbol, symbol = @call> selector 89 iterate ordinal 1 to #simulation.vpi_ref<kind = storage, id = 0 : i64>
 }
}
