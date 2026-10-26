// RUN: obelisk-opt %s --split-input-file --verify-diagnostics -o /dev/null
#word = #simulation.vpi_type<kind = int, isSigned = true, isFourState = false, range = [31, 0], children = [], childNames = []>
module {
 simulation.design @test {
  simulation.scope.decl 0 hierarchy "$root"
  simulation.scope.decl 1 parent 0 hierarchy "top" vpi_kind 32
  simulation.code_unit.decl 44 in 1 function hierarchy "top.f" debug "f"
  simulation.code_unit.decl 45 in 1 function hierarchy "top.other" debug "other"
  simulation.vpi_object.anchor @module id 0 type 32 in 1 ordinal 0 hierarchy "top" debug "top" {backing = #simulation.vpi_backing<kind = scope, id = 1 : i64>}
  simulation.vpi_object.anchor @f id 1 type 20 in 1 parent @module ordinal 0 hierarchy "top.f" debug "f" {backing = #simulation.vpi_backing<kind = code_unit, id = 44 : i64>}
  simulation.vpi_local.decl @result id 1 in 45 scope 1  ordinal 0 hierarchy "top.f.result" debug "f" : i32 {vpi_type = #word}
  // expected-error @+1 {{Return requires the function's exact root-scope return variable}}
  simulation.vpi_relation.decl <kind = symbol, symbol = @f> selector 666 handle ordinal 0 to <kind = symbol, symbol = @result>
 }
}

// -----

#word = #simulation.vpi_type<kind = int, isSigned = true, isFourState = false, range = [31, 0], children = [], childNames = []>
module {
 simulation.design @test {
  simulation.scope.decl 0 hierarchy "$root"
  simulation.scope.decl 1 parent 0 hierarchy "top" vpi_kind 32
  simulation.code_unit.decl 44 in 1 function hierarchy "top.f" debug "f"
  simulation.code_unit.decl 45 in 1 function hierarchy "top.other" debug "other"
  simulation.vpi_object.anchor @module id 0 type 32 in 1 ordinal 0 hierarchy "top" debug "top" {backing = #simulation.vpi_backing<kind = scope, id = 1 : i64>}
  simulation.vpi_object.anchor @f id 1 type 20 in 1 parent @module ordinal 0 hierarchy "top.f" debug "f" {backing = #simulation.vpi_backing<kind = code_unit, id = 44 : i64>}
  simulation.statement.decl 11 in 44 scope 1 type 33 name "block" {is_scope}
  simulation.statement_site.decl 1100 on 11 phase 0
  simulation.vpi_local.decl @result id 1 in 44 scope 1 within 11 ordinal 0 hierarchy "top.f.result" debug "f" : i32 {vpi_type = #word}
  // expected-error @+1 {{Return requires the function's exact root-scope return variable}}
  simulation.vpi_relation.decl <kind = symbol, symbol = @f> selector 666 handle ordinal 0 to <kind = symbol, symbol = @result>
 }
}

// -----

#word = #simulation.vpi_type<kind = int, isSigned = true, isFourState = false, range = [31, 0], children = [], childNames = []>
module {
 simulation.design @test {
  simulation.scope.decl 0 hierarchy "$root"
  simulation.scope.decl 1 parent 0 hierarchy "top" vpi_kind 32
  simulation.code_unit.decl 44 in 1 function hierarchy "top.f" debug "f"
  simulation.code_unit.decl 45 in 1 function hierarchy "top.other" debug "other"
  simulation.vpi_object.anchor @module id 0 type 32 in 1 ordinal 0 hierarchy "top" debug "top" {backing = #simulation.vpi_backing<kind = scope, id = 1 : i64>}
  simulation.vpi_object.anchor @f id 1 type 20 in 1 parent @module ordinal 0 hierarchy "top.f" debug "f" {backing = #simulation.vpi_backing<kind = code_unit, id = 44 : i64>}
  simulation.vpi_local.decl @result id 1 in 44 scope 1  ordinal 0 hierarchy "top.f.result" debug "other" : i32 {vpi_type = #word}
  // expected-error @+1 {{Return requires the function's exact root-scope return variable}}
  simulation.vpi_relation.decl <kind = symbol, symbol = @f> selector 666 handle ordinal 0 to <kind = symbol, symbol = @result>
 }
}

// -----

#word = #simulation.vpi_type<kind = int, isSigned = true, isFourState = false, range = [31, 0], children = [], childNames = []>
module {
 simulation.design @test {
  simulation.scope.decl 0 hierarchy "$root"
  simulation.scope.decl 1 parent 0 hierarchy "top" vpi_kind 32
  simulation.code_unit.decl 44 in 1 function hierarchy "top.f" debug "f" {vpi_properties = #simulation.vpi_properties<[#simulation.vpi_property<selector = 4 : i32, value = 0 : i32>]>}
  simulation.code_unit.decl 45 in 1 function hierarchy "top.other" debug "other"
  simulation.vpi_object.anchor @module id 0 type 32 in 1 ordinal 0 hierarchy "top" debug "top" {backing = #simulation.vpi_backing<kind = scope, id = 1 : i64>}
  simulation.vpi_object.anchor @f id 1 type 20 in 1 parent @module ordinal 0 hierarchy "top.f" debug "f" {backing = #simulation.vpi_backing<kind = code_unit, id = 44 : i64>}
  simulation.vpi_local.decl @result id 1 in 44 scope 1  ordinal 0 hierarchy "top.f.result" debug "f" : i32 {vpi_type = #word}
  // expected-error @+1 {{Return requires a nonvoid public function owner}}
  simulation.vpi_relation.decl <kind = symbol, symbol = @f> selector 666 handle ordinal 0 to <kind = symbol, symbol = @result>
 }
}

// -----

#word = #simulation.vpi_type<kind = int, isSigned = true, isFourState = false, range = [31, 0], children = [], childNames = []>
module {
 simulation.design @test {
  simulation.scope.decl 0 hierarchy "$root"
  simulation.scope.decl 1 parent 0 hierarchy "top" vpi_kind 32
  simulation.code_unit.decl 44 in 1 function hierarchy "top.f" debug "f"
  simulation.code_unit.decl 45 in 1 function hierarchy "top.other" debug "other"
  simulation.vpi_object.anchor @module id 0 type 32 in 1 ordinal 0 hierarchy "top" debug "top" {backing = #simulation.vpi_backing<kind = scope, id = 1 : i64>}
  simulation.vpi_object.anchor @f id 1 type 20 in 1 parent @module ordinal 0 hierarchy "top.f" debug "f" {backing = #simulation.vpi_backing<kind = code_unit, id = 44 : i64>}
  simulation.storage.decl 0 in 1 : i32 design hierarchy "top.f.result" debug "f" {vpi_type = #word}
  // expected-error @+1 {{Return requires the function's exact root-scope return variable}}
  simulation.vpi_relation.decl <kind = symbol, symbol = @f> selector 666 handle ordinal 0 to <kind = storage, id = 0 : i64>
 }
}

// -----

module {
 simulation.design @test {
  simulation.scope.decl 0 hierarchy "$root"
  simulation.scope.decl 1 parent 0 hierarchy "top" vpi_kind 32
  // expected-error @+1 {{fixed function Size is reserved for source-proven void returns}}
  simulation.code_unit.decl 44 in 1 function hierarchy "top.f" debug "f" {vpi_properties = #simulation.vpi_properties<[#simulation.vpi_property<selector = 4 : i32, value = 1 : i32>]>}
 }
}
