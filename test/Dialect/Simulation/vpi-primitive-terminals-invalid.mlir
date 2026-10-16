// RUN: obelisk-opt %s --split-input-file --verify-diagnostics

module {
  simulation.design @invalid {
    simulation.scope.decl 0 hierarchy "$root"
    simulation.scope.decl 1 parent 0 hierarchy "top" vpi_kind 32
    simulation.scope.decl 2 parent 0 hierarchy "other" vpi_kind 32
    simulation.vpi_object.anchor @top id 0 type 32 in 1 ordinal 0 hierarchy "top" debug "top" {backing = #simulation.vpi_backing<kind = scope, id = 1 : i64>}
    simulation.vpi_object.anchor @gate id 1 type 21 in 1 parent @top ordinal 0 hierarchy "top.g" debug "g" {primitive_input_count = 0 : i64}
    // expected-error @+1 {{primitive terminal requires exact input/output/inout direction}}
    simulation.vpi_object.anchor @term id 2 type 46 in 1 parent @gate ordinal 0 hierarchy "" debug "" {vpi_properties = #simulation.vpi_properties<[#simulation.vpi_property<selector = 20 : i32, value = 4 : i32>]>}
  }
}

// -----

module {
  simulation.design @invalid {
    simulation.scope.decl 0 hierarchy "$root"
    simulation.scope.decl 1 parent 0 hierarchy "top" vpi_kind 32
    simulation.scope.decl 2 parent 0 hierarchy "other" vpi_kind 32
    simulation.vpi_object.anchor @top id 0 type 32 in 1 ordinal 0 hierarchy "top" debug "top" {backing = #simulation.vpi_backing<kind = scope, id = 1 : i64>}
    simulation.vpi_object.anchor @gate id 1 type 21 in 1 parent @top ordinal 0 hierarchy "top.g" debug "g" {primitive_input_count = 0 : i64}
    // expected-error @+1 {{primitive terminal requires exact input/output/inout direction}}
    simulation.vpi_object.anchor @term id 2 type 46 in 1 parent @gate ordinal 0 hierarchy "" debug "" {vpi_properties = #simulation.vpi_properties<[]>}
  }
}

// -----

module {
  simulation.design @invalid {
    simulation.scope.decl 0 hierarchy "$root"
    simulation.scope.decl 1 parent 0 hierarchy "top" vpi_kind 32
    simulation.scope.decl 2 parent 0 hierarchy "other" vpi_kind 32
    simulation.vpi_object.anchor @top id 0 type 32 in 1 ordinal 0 hierarchy "top" debug "top" {backing = #simulation.vpi_backing<kind = scope, id = 1 : i64>}
    simulation.vpi_object.anchor @gate id 1 type 21 in 1 parent @top ordinal 0 hierarchy "top.g" debug "g" {primitive_input_count = 0 : i64}
    simulation.vpi_object.anchor @term id 2 type 46 in 1 parent @gate ordinal 0 hierarchy "" debug "" {vpi_properties = #simulation.vpi_properties<[#simulation.vpi_property<selector = 20 : i32, value = 2 : i32>]>}
    // expected-error @+1 {{primitive terminal inventory must preserve exact owner and index}}
    simulation.vpi_relation.decl <kind = symbol, symbol = @gate> selector 46 iterate ordinal 1 to <kind = symbol, symbol = @term>
  }
}

// -----

module {
  simulation.design @invalid {
    simulation.scope.decl 0 hierarchy "$root"
    simulation.scope.decl 1 parent 0 hierarchy "top" vpi_kind 32
    simulation.scope.decl 2 parent 0 hierarchy "other" vpi_kind 32
    simulation.vpi_object.anchor @top id 0 type 32 in 1 ordinal 0 hierarchy "top" debug "top" {backing = #simulation.vpi_backing<kind = scope, id = 1 : i64>}
    simulation.vpi_object.anchor @gate id 1 type 21 in 1 parent @top ordinal 0 hierarchy "top.g" debug "g" {primitive_input_count = 0 : i64}
    // expected-error @+1 {{has an illegal lexical parent kind}}
    simulation.vpi_object.anchor @term id 2 type 46 in 1 parent @top ordinal 1 hierarchy "" debug "" {vpi_properties = #simulation.vpi_properties<[#simulation.vpi_property<selector = 20 : i32, value = 2 : i32>]>}
  }
}

// -----

module {
  simulation.design @invalid {
    simulation.scope.decl 0 hierarchy "$root"
    simulation.scope.decl 1 parent 0 hierarchy "top" vpi_kind 32
    simulation.scope.decl 2 parent 0 hierarchy "other" vpi_kind 32
    simulation.vpi_object.anchor @top id 0 type 32 in 1 ordinal 0 hierarchy "top" debug "top" {backing = #simulation.vpi_backing<kind = scope, id = 1 : i64>}
    simulation.vpi_object.anchor @gate id 1 type 21 in 1 parent @top ordinal 0 hierarchy "top.g" debug "g" {primitive_input_count = 0 : i64}
    // expected-error @+1 {{source expression or primitive terminal requires unnamed immutable ownership}}
    simulation.vpi_object.anchor @term id 2 type 46 in 1 parent @gate ordinal 0 hierarchy "top.g.t" debug "t" {vpi_properties = #simulation.vpi_properties<[#simulation.vpi_property<selector = 20 : i32, value = 2 : i32>]>}
  }
}

// -----

module {
  simulation.design @invalid {
    simulation.scope.decl 0 hierarchy "$root"
    simulation.scope.decl 1 parent 0 hierarchy "top" vpi_kind 32
    simulation.scope.decl 2 parent 0 hierarchy "other" vpi_kind 32
    simulation.vpi_object.anchor @top id 0 type 32 in 1 ordinal 0 hierarchy "top" debug "top" {backing = #simulation.vpi_backing<kind = scope, id = 1 : i64>}
    simulation.vpi_object.anchor @gate id 1 type 21 in 1 parent @top ordinal 0 hierarchy "top.g" debug "g" {primitive_input_count = 0 : i64}
    // expected-error @+1 {{source expression scope must match its exact owner}}
    simulation.vpi_object.anchor @term id 2 type 46 in 2 parent @gate ordinal 0 hierarchy "" debug "" {vpi_properties = #simulation.vpi_properties<[#simulation.vpi_property<selector = 20 : i32, value = 2 : i32>]>}
  }
}

// -----

module {
  simulation.design @invalid {
    simulation.scope.decl 0 hierarchy "$root"
    simulation.scope.decl 1 parent 0 hierarchy "top" vpi_kind 32
    simulation.scope.decl 2 parent 0 hierarchy "other" vpi_kind 32
    simulation.vpi_object.anchor @top id 0 type 32 in 1 ordinal 0 hierarchy "top" debug "top" {backing = #simulation.vpi_backing<kind = scope, id = 1 : i64>}
    simulation.vpi_object.anchor @gate id 1 type 21 in 1 parent @top ordinal 0 hierarchy "top.g" debug "g" {primitive_input_count = 0 : i64}
    // expected-error @+1 {{primitive terminal requires no payload and an encodable index}}
    simulation.vpi_object.anchor @term id 2 type 46 in 1 parent @gate ordinal 2147483648 hierarchy "" debug "" {vpi_properties = #simulation.vpi_properties<[#simulation.vpi_property<selector = 20 : i32, value = 2 : i32>]>}
  }
}

// -----

module {
  simulation.design @invalid {
    simulation.scope.decl 0 hierarchy "$root"
    simulation.scope.decl 1 parent 0 hierarchy "top" vpi_kind 32
    simulation.scope.decl 2 parent 0 hierarchy "other" vpi_kind 32
    simulation.vpi_object.anchor @top id 0 type 32 in 1 ordinal 0 hierarchy "top" debug "top" {backing = #simulation.vpi_backing<kind = scope, id = 1 : i64>}
    simulation.vpi_object.anchor @gate id 1 type 21 in 1 parent @top ordinal 0 hierarchy "top.g" debug "g" {primitive_input_count = 0 : i64}
    simulation.vpi_object.anchor @term id 2 type 46 in 1 parent @gate ordinal 0 hierarchy "" debug "" {vpi_properties = #simulation.vpi_properties<[#simulation.vpi_property<selector = 20 : i32, value = 2 : i32>]>}
    simulation.vpi_object.anchor @other_gate id 3 type 21 in 1 parent @top ordinal 1 hierarchy "top.h" debug "h" {primitive_input_count = 0 : i64}
    // expected-error @+1 {{primitive terminal relation must name its exact owner}}
    simulation.vpi_relation.decl <kind = symbol, symbol = @term> selector 103 handle ordinal 0 to <kind = symbol, symbol = @other_gate>
  }
}

// -----

module {
  simulation.design @invalid {
    simulation.scope.decl 0 hierarchy "$root"
    simulation.scope.decl 1 parent 0 hierarchy "top" vpi_kind 32
    simulation.scope.decl 2 parent 0 hierarchy "other" vpi_kind 32
    simulation.vpi_object.anchor @top id 0 type 32 in 1 ordinal 0 hierarchy "top" debug "top" {backing = #simulation.vpi_backing<kind = scope, id = 1 : i64>}
    // expected-error @+1 {{primitive input count must match its terminal directions}}
    simulation.vpi_object.anchor @gate id 1 type 21 in 1 parent @top ordinal 0 hierarchy "top.g" debug "g" {primitive_input_count = 1 : i64}
    simulation.vpi_object.anchor @term id 2 type 46 in 1 parent @gate ordinal 0 hierarchy "" debug "" {vpi_properties = #simulation.vpi_properties<[#simulation.vpi_property<selector = 20 : i32, value = 2 : i32>]>}
  }
}
