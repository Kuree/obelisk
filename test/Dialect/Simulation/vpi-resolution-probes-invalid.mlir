// RUN: obelisk-opt %s --split-input-file --verify-diagnostics
module {
  simulation.design @invalid {
    simulation.scope.decl 0 hierarchy "$root"
    simulation.scope.decl 1 parent 0 hierarchy "top" vpi_kind 32
    simulation.vpi_object.anchor @top id 2 type 32 in 1 ordinal 0 hierarchy "top" debug "top" {backing = #simulation.vpi_backing<kind = scope, id = 1 : i64>}
    simulation.code_unit.decl 44 in 1 continuous hierarchy "top.$resolution" {internal}
    simulation.code_unit.decl 45 in 1 function hierarchy "top.resolve"
    simulation.code_unit.decl 46 in 1 function hierarchy "top.other"
    simulation.vpi_object.anchor @resolver id 0 type 20 in 1 parent @top ordinal 0 hierarchy "top.resolve" debug "resolve" {backing = #simulation.vpi_backing<kind = code_unit, id = 45 : i64>}
    simulation.vpi_nettype.decl @nt id 0 in 1 owner @top hierarchy "top.nt" debug "nt" {
      resolution_function = @resolver,
      target_type = #simulation.vpi_type<kind = logic, isSigned = false, isFourState = true, range = [1, 0], children = [], childNames = []>
    }
    simulation.net.decl 0 in 1 : !simulation.logic<2> design {vpi_type = #simulation.vpi_type<kind = logic, isSigned = false, isFourState = true, range = [1, 0], children = [], childNames = []>, nettype = @nt, vpi_properties = #simulation.vpi_properties<[#simulation.vpi_property<selector = 22 : i32, value = 14 : i32>]>}
    simulation.func @body(%ctx: !simulation.context {simulation.capture_kind = 0 : i32}) attributes {entry_kind = 7 : i32, code_unit_id = 44 : i64} {
      // expected-error @+1 {{net ID must be nonnegative}}
      %ticket = simulation.vpi.resolution.prepare %ctx net -1
      simulation.return
    }
    simulation.func private @other(%ctx: !simulation.context {simulation.capture_kind = 0 : i32}) attributes {entry_kind = 8 : i32, code_unit_id = 46 : i64} {
      simulation.return
    }
  }
}

// -----

module {
  simulation.design @invalid {
    simulation.scope.decl 0 hierarchy "$root"
    simulation.scope.decl 1 parent 0 hierarchy "top" vpi_kind 32
    simulation.vpi_object.anchor @top id 2 type 32 in 1 ordinal 0 hierarchy "top" debug "top" {backing = #simulation.vpi_backing<kind = scope, id = 1 : i64>}
    simulation.code_unit.decl 44 in 1 continuous hierarchy "top.$resolution" {internal}
    simulation.code_unit.decl 45 in 1 function hierarchy "top.resolve"
    simulation.code_unit.decl 46 in 1 function hierarchy "top.other"
    simulation.vpi_object.anchor @resolver id 0 type 20 in 1 parent @top ordinal 0 hierarchy "top.resolve" debug "resolve" {backing = #simulation.vpi_backing<kind = code_unit, id = 45 : i64>}
    simulation.vpi_nettype.decl @nt id 0 in 1 owner @top hierarchy "top.nt" debug "nt" {
      resolution_function = @resolver,
      target_type = #simulation.vpi_type<kind = logic, isSigned = false, isFourState = true, range = [1, 0], children = [], childNames = []>
    }
    simulation.net.decl 0 in 1 : !simulation.logic<2> design {vpi_type = #simulation.vpi_type<kind = logic, isSigned = false, isFourState = true, range = [1, 0], children = [], childNames = []>, nettype = @nt, vpi_properties = #simulation.vpi_properties<[#simulation.vpi_property<selector = 22 : i32, value = 14 : i32>]>}
    simulation.func @body(%ctx: !simulation.context {simulation.capture_kind = 0 : i32}) attributes {entry_kind = 7 : i32, code_unit_id = 44 : i64} {
      // expected-error @+1 {{references an unknown resolution net ID}}
      %ticket = simulation.vpi.resolution.prepare %ctx net 99
      simulation.return
    }
    simulation.func private @other(%ctx: !simulation.context {simulation.capture_kind = 0 : i32}) attributes {entry_kind = 8 : i32, code_unit_id = 46 : i64} {
      simulation.return
    }
  }
}

// -----

module {
  simulation.design @invalid {
    simulation.scope.decl 0 hierarchy "$root"
    simulation.scope.decl 1 parent 0 hierarchy "top" vpi_kind 32
    simulation.vpi_object.anchor @top id 2 type 32 in 1 ordinal 0 hierarchy "top" debug "top" {backing = #simulation.vpi_backing<kind = scope, id = 1 : i64>}
    simulation.code_unit.decl 44 in 1 continuous hierarchy "top.$resolution" {internal}
    simulation.code_unit.decl 45 in 1 function hierarchy "top.resolve"
    simulation.code_unit.decl 46 in 1 function hierarchy "top.other"
    simulation.vpi_object.anchor @resolver id 0 type 20 in 1 parent @top ordinal 0 hierarchy "top.resolve" debug "resolve" {backing = #simulation.vpi_backing<kind = code_unit, id = 45 : i64>}
    simulation.vpi_nettype.decl @nt id 0 in 1 owner @top hierarchy "top.nt" debug "nt" {
      resolution_function = @resolver,
      target_type = #simulation.vpi_type<kind = logic, isSigned = false, isFourState = true, range = [1, 0], children = [], childNames = []>
    }
    simulation.net.decl 0 in 1 : !simulation.logic<2> design {}
    simulation.func @body(%ctx: !simulation.context {simulation.capture_kind = 0 : i32}) attributes {entry_kind = 7 : i32, code_unit_id = 44 : i64} {
      // expected-error @+1 {{net requires an exact resolution-function binding}}
      %ticket = simulation.vpi.resolution.prepare %ctx net 0
      simulation.return
    }
    simulation.func private @other(%ctx: !simulation.context {simulation.capture_kind = 0 : i32}) attributes {entry_kind = 8 : i32, code_unit_id = 46 : i64} {
      simulation.return
    }
  }
}

// -----

module {
  simulation.design @invalid {
    simulation.scope.decl 0 hierarchy "$root"
    simulation.scope.decl 1 parent 0 hierarchy "top" vpi_kind 32
    simulation.vpi_object.anchor @top id 2 type 32 in 1 ordinal 0 hierarchy "top" debug "top" {backing = #simulation.vpi_backing<kind = scope, id = 1 : i64>}
    simulation.code_unit.decl 44 in 1 continuous hierarchy "top.$resolution" {internal}
    simulation.code_unit.decl 45 in 1 function hierarchy "top.resolve"
    simulation.code_unit.decl 46 in 1 function hierarchy "top.other"
    simulation.vpi_object.anchor @resolver id 0 type 20 in 1 parent @top ordinal 0 hierarchy "top.resolve" debug "resolve" {}
    simulation.vpi_nettype.decl @nt id 0 in 1 owner @top hierarchy "top.nt" debug "nt" {
      resolution_function = @resolver,
      target_type = #simulation.vpi_type<kind = logic, isSigned = false, isFourState = true, range = [1, 0], children = [], childNames = []>
    }
    simulation.net.decl 0 in 1 : !simulation.logic<2> design {vpi_type = #simulation.vpi_type<kind = logic, isSigned = false, isFourState = true, range = [1, 0], children = [], childNames = []>, nettype = @nt, vpi_properties = #simulation.vpi_properties<[#simulation.vpi_property<selector = 22 : i32, value = 14 : i32>]>}
    simulation.func @body(%ctx: !simulation.context {simulation.capture_kind = 0 : i32}) attributes {entry_kind = 7 : i32, code_unit_id = 44 : i64} {
      // expected-error @+1 {{net requires an exact resolution-function binding}}
      %ticket = simulation.vpi.resolution.prepare %ctx net 0
      simulation.return
    }
    simulation.func private @other(%ctx: !simulation.context {simulation.capture_kind = 0 : i32}) attributes {entry_kind = 8 : i32, code_unit_id = 46 : i64} {
      simulation.return
    }
  }
}

// -----

module {
  simulation.design @invalid {
    simulation.scope.decl 0 hierarchy "$root"
    simulation.scope.decl 1 parent 0 hierarchy "top" vpi_kind 32
    simulation.vpi_object.anchor @top id 2 type 32 in 1 ordinal 0 hierarchy "top" debug "top" {backing = #simulation.vpi_backing<kind = scope, id = 1 : i64>}
    simulation.code_unit.decl 44 in 1 continuous hierarchy "top.$resolution" {internal}
    simulation.code_unit.decl 45 in 1 function hierarchy "top.resolve"
    simulation.code_unit.decl 46 in 1 function hierarchy "top.other"
    simulation.vpi_object.anchor @resolver id 0 type 20 in 1 parent @top ordinal 0 hierarchy "top.resolve" debug "resolve" {backing = #simulation.vpi_backing<kind = code_unit, id = 45 : i64>}
    simulation.vpi_nettype.decl @nt id 0 in 1 owner @top hierarchy "top.nt" debug "nt" {
      resolution_function = @resolver,
      target_type = #simulation.vpi_type<kind = logic, isSigned = false, isFourState = true, range = [1, 0], children = [], childNames = []>
    }
    simulation.net.decl 0 in 1 : !simulation.logic<2> design {vpi_type = #simulation.vpi_type<kind = logic, isSigned = false, isFourState = true, range = [1, 0], children = [], childNames = []>, nettype = @nt, vpi_properties = #simulation.vpi_properties<[#simulation.vpi_property<selector = 22 : i32, value = 14 : i32>]>}
    simulation.func @body(%ctx: !simulation.context {simulation.capture_kind = 0 : i32}) attributes {entry_kind = 7 : i32, code_unit_id = 44 : i64} {
      // expected-error @+1 {{net resolution call target disagrees with its nettype}}
      simulation.call @other(%ctx) {simulation.vpi_resolution_net = 0 : i64} : (!simulation.context) -> ()
      simulation.return
    }
    simulation.func private @other(%ctx: !simulation.context {simulation.capture_kind = 0 : i32}) attributes {entry_kind = 8 : i32, code_unit_id = 46 : i64} {
      simulation.return
    }
  }
}

// -----

module {
  simulation.design @invalid {
    simulation.scope.decl 0 hierarchy "$root"
    simulation.scope.decl 1 parent 0 hierarchy "top" vpi_kind 32
    simulation.vpi_object.anchor @top id 2 type 32 in 1 ordinal 0 hierarchy "top" debug "top" {backing = #simulation.vpi_backing<kind = scope, id = 1 : i64>}
    simulation.code_unit.decl 44 in 1 continuous hierarchy "top.$resolution" {internal}
    simulation.code_unit.decl 45 in 1 function hierarchy "top.resolve"
    simulation.code_unit.decl 46 in 1 function hierarchy "top.other"
    simulation.vpi_object.anchor @resolver id 0 type 20 in 1 parent @top ordinal 0 hierarchy "top.resolve" debug "resolve" {backing = #simulation.vpi_backing<kind = code_unit, id = 45 : i64>}
    simulation.vpi_nettype.decl @nt id 0 in 1 owner @top hierarchy "top.nt" debug "nt" {
      resolution_function = @resolver,
      target_type = #simulation.vpi_type<kind = logic, isSigned = false, isFourState = true, range = [1, 0], children = [], childNames = []>
    }
    simulation.net.decl 0 in 1 : !simulation.logic<2> design {vpi_type = #simulation.vpi_type<kind = logic, isSigned = false, isFourState = true, range = [1, 0], children = [], childNames = []>, nettype = @nt, vpi_properties = #simulation.vpi_properties<[#simulation.vpi_property<selector = 22 : i32, value = 14 : i32>]>}
    simulation.func @body(%ctx: !simulation.context {simulation.capture_kind = 0 : i32}) attributes {entry_kind = 7 : i32, code_unit_id = 44 : i64} {
      // expected-error @+1 {{requires a nonnegative i64 net ID on simulation.call}}
      simulation.call @other(%ctx) {simulation.vpi_resolution_net = 0 : i32} : (!simulation.context) -> ()
      simulation.return
    }
    simulation.func private @other(%ctx: !simulation.context {simulation.capture_kind = 0 : i32}) attributes {entry_kind = 8 : i32, code_unit_id = 46 : i64} {
      simulation.return
    }
  }
}
