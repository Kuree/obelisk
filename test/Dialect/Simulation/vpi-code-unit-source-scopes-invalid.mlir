// RUN: talos-opt %s --split-input-file --verify-diagnostics

module {
  simulation.design @zero {
    simulation.scope.decl 0 hierarchy "$root"
    simulation.scope.decl 1 parent 0 hierarchy "top" vpi_kind 32
    simulation.vpi_object.anchor @top id 0 type 32 in 1 ordinal 0 hierarchy "top" debug "top" {backing = #simulation.vpi_backing<kind = scope, id = 1 : i64>}
    simulation.vpi_object.anchor @f id 1 type 20 in 1 parent @top ordinal 0 hierarchy "top.f" debug "f"
    simulation.code_unit.decl 45 in 1 function hierarchy "top.f" {internal}
    // expected-error @+1 {{code-unit ID must be positive}}
    simulation.vpi_code_unit_scope.bind 0 to @f
  }
}

// -----

module {
  simulation.design @missing {
    simulation.scope.decl 0 hierarchy "$root"
    simulation.scope.decl 1 parent 0 hierarchy "top" vpi_kind 32
    simulation.vpi_object.anchor @top id 0 type 32 in 1 ordinal 0 hierarchy "top" debug "top" {backing = #simulation.vpi_backing<kind = scope, id = 1 : i64>}
    simulation.vpi_object.anchor @f id 1 type 20 in 1 parent @top ordinal 0 hierarchy "top.f" debug "f"
    simulation.code_unit.decl 45 in 1 function hierarchy "top.f" {internal}
    // expected-error @+1 {{source must name an unbacked task or function anchor}}
    simulation.vpi_code_unit_scope.bind 45 to @absent
  }
}

// -----

module {
  simulation.design @public {
    simulation.scope.decl 0 hierarchy "$root"
    simulation.scope.decl 1 parent 0 hierarchy "top" vpi_kind 32
    simulation.vpi_object.anchor @top id 0 type 32 in 1 ordinal 0 hierarchy "top" debug "top" {backing = #simulation.vpi_backing<kind = scope, id = 1 : i64>}
    simulation.vpi_object.anchor @f id 1 type 20 in 1 parent @top ordinal 0 hierarchy "top.f" debug "f"
    simulation.code_unit.decl 45 in 1 function hierarchy "top.f"
    // expected-error @+1 {{requires an internal task/function code unit}}
    simulation.vpi_code_unit_scope.bind 45 to @f
  }
}

// -----

module {
  simulation.design @wrong_kind {
    simulation.scope.decl 0 hierarchy "$root"
    simulation.scope.decl 1 parent 0 hierarchy "top" vpi_kind 32
    simulation.vpi_object.anchor @top id 0 type 32 in 1 ordinal 0 hierarchy "top" debug "top" {backing = #simulation.vpi_backing<kind = scope, id = 1 : i64>}
    simulation.vpi_object.anchor @f id 1 type 20 in 1 parent @top ordinal 0 hierarchy "top.f" debug "f"
    simulation.code_unit.decl 45 in 1 task hierarchy "top.f" {internal}
    // expected-error @+1 {{source callable kind, physical scope, and hierarchy must match}}
    simulation.vpi_code_unit_scope.bind 45 to @f
  }
}

// -----

module {
  simulation.design @wrong_hierarchy {
    simulation.scope.decl 0 hierarchy "$root"
    simulation.scope.decl 1 parent 0 hierarchy "top" vpi_kind 32
    simulation.vpi_object.anchor @top id 0 type 32 in 1 ordinal 0 hierarchy "top" debug "top" {backing = #simulation.vpi_backing<kind = scope, id = 1 : i64>}
    simulation.vpi_object.anchor @f id 1 type 20 in 1 parent @top ordinal 0 hierarchy "top.f" debug "f"
    simulation.code_unit.decl 45 in 1 function hierarchy "top.g" {internal}
    // expected-error @+1 {{source callable kind, physical scope, and hierarchy must match}}
    simulation.vpi_code_unit_scope.bind 45 to @f
  }
}

// -----

module {
  simulation.design @wrong_scope {
    simulation.scope.decl 0 hierarchy "$root"
    simulation.scope.decl 1 parent 0 hierarchy "top" vpi_kind 32
    simulation.vpi_object.anchor @top id 0 type 32 in 1 ordinal 0 hierarchy "top" debug "top" {backing = #simulation.vpi_backing<kind = scope, id = 1 : i64>}
    simulation.vpi_object.anchor @f id 1 type 20 in 1 parent @top ordinal 0 hierarchy "top.f" debug "f"
    simulation.code_unit.decl 45 in 0 function hierarchy "top.f" {internal}
    // expected-error @+1 {{source callable kind, physical scope, and hierarchy must match}}
    simulation.vpi_code_unit_scope.bind 45 to @f
  }
}

// -----

module {
  simulation.design @unknown_unit {
    simulation.scope.decl 0 hierarchy "$root"
    simulation.scope.decl 1 parent 0 hierarchy "top" vpi_kind 32
    simulation.vpi_object.anchor @top id 0 type 32 in 1 ordinal 0 hierarchy "top" debug "top" {backing = #simulation.vpi_backing<kind = scope, id = 1 : i64>}
    simulation.vpi_object.anchor @f id 1 type 20 in 1 parent @top ordinal 0 hierarchy "top.f" debug "f"
    simulation.code_unit.decl 45 in 1 function hierarchy "top.f" {internal}
    // expected-error @+1 {{requires an internal task/function code unit}}
    simulation.vpi_code_unit_scope.bind 99 to @f
  }
}

// -----

module {
  simulation.design @backed {
    simulation.scope.decl 0 hierarchy "$root"
    simulation.scope.decl 1 parent 0 hierarchy "top" vpi_kind 32
    simulation.vpi_object.anchor @top id 0 type 32 in 1 ordinal 0 hierarchy "top" debug "top" {backing = #simulation.vpi_backing<kind = scope, id = 1 : i64>}
    simulation.vpi_object.anchor @f id 1 type 20 in 1 parent @top ordinal 0 hierarchy "top.f" debug "f" {backing = #simulation.vpi_backing<kind = code_unit, id = 45 : i64>}
    simulation.code_unit.decl 45 in 1 function hierarchy "top.f" {internal}
    // expected-error @+1 {{source must name an unbacked task or function anchor}}
    simulation.vpi_code_unit_scope.bind 45 to @f
  }
}

// -----

module {
  simulation.design @duplicate {
    simulation.scope.decl 0 hierarchy "$root"
    simulation.scope.decl 1 parent 0 hierarchy "top" vpi_kind 32
    simulation.vpi_object.anchor @top id 0 type 32 in 1 ordinal 0 hierarchy "top" debug "top" {backing = #simulation.vpi_backing<kind = scope, id = 1 : i64>}
    simulation.vpi_object.anchor @f id 1 type 20 in 1 parent @top ordinal 0 hierarchy "top.f" debug "f"
    simulation.code_unit.decl 45 in 1 function hierarchy "top.f" {internal}
    simulation.vpi_code_unit_scope.bind 45 to @f
    // expected-error @+1 {{duplicates a code-unit or source scope binding}}
    simulation.vpi_code_unit_scope.bind 45 to @f
  }
}

// -----

module {
  simulation.design @task_marker_requires_import {
    simulation.scope.decl 0 hierarchy "$root"
    simulation.scope.decl 1 parent 0 hierarchy "top" vpi_kind 32
    simulation.vpi_object.anchor @top id 0 type 32 in 1 ordinal 0 hierarchy "top" debug "top" {backing = #simulation.vpi_backing<kind = scope, id = 1 : i64>}
    simulation.vpi_object.anchor @task id 1 type 59 in 1 parent @top ordinal 0 hierarchy "top.task" debug "task"
    simulation.code_unit.decl 45 in 1 function hierarchy "top.task" {internal, dpi.task}
    // expected-error @+1 {{source callable kind, physical scope, and hierarchy must match}}
    simulation.vpi_code_unit_scope.bind 45 to @task
  }
}

// -----

module {
  simulation.design @empty_name {
    simulation.scope.decl 0 hierarchy "$root"
    simulation.scope.decl 1 parent 0 hierarchy "top" vpi_kind 32
    simulation.vpi_object.anchor @top id 0 type 32 in 1 ordinal 0 hierarchy "top" debug "top" {backing = #simulation.vpi_backing<kind = scope, id = 1 : i64>}
    simulation.vpi_object.anchor @f id 1 type 20 in 1 parent @top ordinal 0 hierarchy "top.f" debug ""
    simulation.code_unit.decl 45 in 1 function hierarchy "top.f" {internal}
    // expected-error @+1 {{mapped source callable requires a nonempty exact name without NUL bytes}}
    simulation.vpi_code_unit_scope.bind 45 to @f
  }
}
