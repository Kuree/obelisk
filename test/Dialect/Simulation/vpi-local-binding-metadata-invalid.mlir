// RUN: talos-opt %s --split-input-file --verify-diagnostics -o /dev/null
#word = #simulation.vpi_type<kind = int, isSigned = true, isFourState = false, range = [31, 0], children = [], childNames = []>
module {
  simulation.design @invalid {
    simulation.scope.decl 0 hierarchy "$root"
    simulation.scope.decl 1 parent 0 hierarchy "top" vpi_kind 32
    simulation.code_unit.decl 1 in 1 task hierarchy "top.f"
    simulation.code_unit.decl 2 in 1 task hierarchy "top.other"
    simulation.vpi_local.decl @local id 19 in 1 scope 1 ordinal 0 hierarchy "top.f.value" debug "value" : i32 {vpi_type = #word}
    // expected-error @+1 {{declaration must name a simulation.vpi_local.decl}}
    simulation.func @f(%ctx: !simulation.context {simulation.capture_kind = 0 : i32}) attributes {entry_kind = 12 : i32, code_unit_id = 1 : i64, simulation.bindings = [#simulation.local_binding<path = "top.f.value", type = i32, automatic = false, patternVariable = false, isReturn = false, vpiLocal = @missing>]} {
      simulation.return
    }
  }
}

// -----

#word = #simulation.vpi_type<kind = int, isSigned = true, isFourState = false, range = [31, 0], children = [], childNames = []>
module {
  simulation.design @invalid {
    simulation.scope.decl 0 hierarchy "$root"
    simulation.scope.decl 1 parent 0 hierarchy "top" vpi_kind 32
    simulation.code_unit.decl 1 in 1 task hierarchy "top.f"
    simulation.code_unit.decl 2 in 1 task hierarchy "top.other"
    simulation.vpi_local.decl @local id 19 in 1 scope 1 ordinal 0 hierarchy "top.f.value" debug "value" : i32 {vpi_type = #word}
    // expected-error @+1 {{automatic reference type must match its local declaration}}
    simulation.func @f(%ctx: !simulation.context {simulation.capture_kind = 0 : i32}) attributes {entry_kind = 12 : i32, code_unit_id = 1 : i64, simulation.bindings = [#simulation.local_binding<path = "top.f.value", type = i64, automatic = false, patternVariable = false, isReturn = false, vpiLocal = @local>]} {
      simulation.return
    }
  }
}

// -----

#word = #simulation.vpi_type<kind = int, isSigned = true, isFourState = false, range = [31, 0], children = [], childNames = []>
module {
  simulation.design @invalid {
    simulation.scope.decl 0 hierarchy "$root"
    simulation.scope.decl 1 parent 0 hierarchy "top" vpi_kind 32
    simulation.code_unit.decl 1 in 1 task hierarchy "top.f"
    simulation.code_unit.decl 2 in 1 task hierarchy "top.other"
    simulation.vpi_local.decl @local id 19 in 1 scope 1 ordinal 0 hierarchy "top.f.value" debug "value" : i32 {vpi_type = #word}
    // A private execution key need not match the declaration display name.
    simulation.func @f(%ctx: !simulation.context {simulation.capture_kind = 0 : i32}) attributes {entry_kind = 12 : i32, code_unit_id = 1 : i64, simulation.bindings = [#simulation.local_binding<path = "top.f.other", type = i32, automatic = false, patternVariable = false, isReturn = false, vpiLocal = @local>]} {
      simulation.return
    }
  }
}

// -----

#word = #simulation.vpi_type<kind = int, isSigned = true, isFourState = false, range = [31, 0], children = [], childNames = []>
module {
  simulation.design @invalid {
    simulation.scope.decl 0 hierarchy "$root"
    simulation.scope.decl 1 parent 0 hierarchy "top" vpi_kind 32
    simulation.code_unit.decl 1 in 1 task hierarchy "top.f"
    simulation.code_unit.decl 2 in 1 task hierarchy "top.other"
    simulation.vpi_local.decl @local id 19 in 1 scope 1 ordinal 0 hierarchy "top.f.value" debug "value" : i32 {vpi_type = #word}
    // expected-error @+1 {{local declaration belongs to a different lexical code unit}}
    simulation.func @f(%ctx: !simulation.context {simulation.capture_kind = 0 : i32}) attributes {entry_kind = 12 : i32, code_unit_id = 2 : i64, simulation.bindings = [#simulation.local_binding<path = "top.f.value", type = i32, automatic = false, patternVariable = false, isReturn = false, vpiLocal = @local>]} {
      simulation.return
    }
  }
}

// -----

#word = #simulation.vpi_type<kind = int, isSigned = true, isFourState = false, range = [31, 0], children = [], childNames = []>
module {
  simulation.design @invalid {
    simulation.scope.decl 0 hierarchy "$root"
    simulation.scope.decl 1 parent 0 hierarchy "top" vpi_kind 32
    simulation.code_unit.decl 1 in 1 task hierarchy "top.f"
    simulation.code_unit.decl 2 in 1 task hierarchy "top.other"
    simulation.vpi_local.decl @local id 19 in 1 scope 1 ordinal 0 hierarchy "top.f.value" debug "value" : i32 {vpi_type = #word}
    // expected-error @+1 {{duplicates a VPI local declaration binding}}
    simulation.func @f(%ctx: !simulation.context {simulation.capture_kind = 0 : i32}) attributes {entry_kind = 12 : i32, code_unit_id = 1 : i64, simulation.bindings = [#simulation.local_binding<path = "top.f.value", type = i32, automatic = false, patternVariable = false, isReturn = false, vpiLocal = @local>, #simulation.local_binding<path = "other.private.key", type = i32, automatic = false, patternVariable = false, isReturn = false, vpiLocal = @local>]} {
      simulation.return
    }
  }
}

// -----

#word = #simulation.vpi_type<kind = int, isSigned = true, isFourState = false, range = [31, 0], children = [], childNames = []>
module {
  simulation.design @invalid {
    simulation.scope.decl 0 hierarchy "$root"
    simulation.scope.decl 1 parent 0 hierarchy "top" vpi_kind 32
    simulation.code_unit.decl 1 in 1 task hierarchy "top.f"
    simulation.code_unit.decl 2 in 1 task hierarchy "top.other"
    simulation.vpi_local.decl @local id 19 in 1 scope 1 ordinal 0 hierarchy "top.f.value" debug "value" : i32 {vpi_type = #word}
    simulation.vpi_local.decl @other id 20 in 1 scope 1 ordinal 1 hierarchy "top.f.other" debug "other" : i32 {vpi_type = #word}
    // expected-error @+1 {{both provide the source value for path}}
    simulation.func @f(%ctx: !simulation.context {simulation.capture_kind = 0 : i32}) attributes {entry_kind = 12 : i32, code_unit_id = 1 : i64, simulation.bindings = [#simulation.local_binding<path = "private.key", type = i32, automatic = false, patternVariable = false, isReturn = false, vpiLocal = @local>, #simulation.local_binding<path = "private.key", type = i32, automatic = false, patternVariable = false, isReturn = false, vpiLocal = @other>]} {
      simulation.return
    }
  }
}
