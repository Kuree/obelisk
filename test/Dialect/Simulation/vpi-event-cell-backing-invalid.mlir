// RUN: talos-opt %s --split-input-file --verify-diagnostics

#event = #simulation.vpi_type<kind = event, isSigned = false, isFourState = false, range = [], children = [], childNames = []>
module {
  simulation.design @wrong_anchor {
    simulation.scope.decl 0 hierarchy "$root"
    simulation.scope.decl 1 parent 0 hierarchy "top" vpi_kind 32
    simulation.vpi_object.anchor @top id 0 type 32 in 1 ordinal 0 hierarchy "top" debug "top" {backing = #simulation.vpi_backing<kind = scope, id = 1 : i64>}
    simulation.storage.decl 0 in 1 : !simulation.event design hierarchy "cell" {vpi_type = #event}
    // expected-error @+1 {{storage backing requires a scalar named-event anchor}}
    simulation.vpi_object.anchor @cell id 1 type 41 in 1 parent @top ordinal 0 hierarchy "cell" debug "cell" {backing = #simulation.vpi_backing<kind = storage, id = 0 : i64>}
  }
}

// -----

#event = #simulation.vpi_type<kind = event, isSigned = false, isFourState = false, range = [], children = [], childNames = []>
module {
  simulation.design @missing_storage {
    simulation.scope.decl 0 hierarchy "$root"
    simulation.scope.decl 1 parent 0 hierarchy "top" vpi_kind 32
    simulation.vpi_object.anchor @top id 0 type 32 in 1 ordinal 0 hierarchy "top" debug "top" {backing = #simulation.vpi_backing<kind = scope, id = 1 : i64>}

    // expected-error @+1 {{references an unknown backing storage ID}}
    simulation.vpi_object.anchor @cell id 1 type 34 in 1 parent @top ordinal 0 hierarchy "cell" debug "cell" {backing = #simulation.vpi_backing<kind = storage, id = 0 : i64>}
  }
}

// -----

#event = #simulation.vpi_type<kind = event, isSigned = false, isFourState = false, range = [], children = [], childNames = []>
module {
  simulation.design @wrong_type {
    simulation.scope.decl 0 hierarchy "$root"
    simulation.scope.decl 1 parent 0 hierarchy "top" vpi_kind 32
    simulation.vpi_object.anchor @top id 0 type 32 in 1 ordinal 0 hierarchy "top" debug "top" {backing = #simulation.vpi_backing<kind = scope, id = 1 : i64>}
    simulation.storage.decl 0 in 1 : i64 design hierarchy "cell"
    // expected-error @+1 {{backing named event requires exact event storage and VPI semantics}}
    simulation.vpi_object.anchor @cell id 1 type 34 in 1 parent @top ordinal 0 hierarchy "cell" debug "cell" {backing = #simulation.vpi_backing<kind = storage, id = 0 : i64>}
  }
}

// -----

#event = #simulation.vpi_type<kind = event, isSigned = false, isFourState = false, range = [], children = [], childNames = []>
module {
  simulation.design @missing_semantics {
    simulation.scope.decl 0 hierarchy "$root"
    simulation.scope.decl 1 parent 0 hierarchy "top" vpi_kind 32
    simulation.vpi_object.anchor @top id 0 type 32 in 1 ordinal 0 hierarchy "top" debug "top" {backing = #simulation.vpi_backing<kind = scope, id = 1 : i64>}
    simulation.storage.decl 0 in 1 : !simulation.event design hierarchy "cell"
    // expected-error @+1 {{backing named event requires exact event storage and VPI semantics}}
    simulation.vpi_object.anchor @cell id 1 type 34 in 1 parent @top ordinal 0 hierarchy "cell" debug "cell" {backing = #simulation.vpi_backing<kind = storage, id = 0 : i64>}
  }
}

// -----

#event = #simulation.vpi_type<kind = event, isSigned = false, isFourState = false, range = [], children = [], childNames = []>
module {
  simulation.design @automatic {
    simulation.scope.decl 0 hierarchy "$root"
    simulation.scope.decl 1 parent 0 hierarchy "top" vpi_kind 32
    simulation.vpi_object.anchor @top id 0 type 32 in 1 ordinal 0 hierarchy "top" debug "top" {backing = #simulation.vpi_backing<kind = scope, id = 1 : i64>}
    simulation.storage.decl 0 in 1 : !simulation.event automatic hierarchy "cell" {vpi_type = #event}
    // expected-error @+1 {{backing named event requires static storage}}
    simulation.vpi_object.anchor @cell id 1 type 34 in 1 parent @top ordinal 0 hierarchy "cell" debug "cell" {backing = #simulation.vpi_backing<kind = storage, id = 0 : i64>}
  }
}

// -----

#event = #simulation.vpi_type<kind = event, isSigned = false, isFourState = false, range = [], children = [], childNames = []>
module {
  simulation.design @scope {
    simulation.scope.decl 0 hierarchy "$root"
    simulation.scope.decl 1 parent 0 hierarchy "top" vpi_kind 32
    simulation.vpi_object.anchor @top id 0 type 32 in 1 ordinal 0 hierarchy "top" debug "top" {backing = #simulation.vpi_backing<kind = scope, id = 1 : i64>}
    simulation.scope.decl 2 parent 0 hierarchy "other" vpi_kind 32
    simulation.storage.decl 0 in 2 : !simulation.event design hierarchy "cell" {vpi_type = #event}
    // expected-error @+1 {{backing storage scope must equal the anchor's enclosing scope}}
    simulation.vpi_object.anchor @cell id 1 type 34 in 1 parent @top ordinal 0 hierarchy "cell" debug "cell" {backing = #simulation.vpi_backing<kind = storage, id = 0 : i64>}
  }
}

// -----

#event = #simulation.vpi_type<kind = event, isSigned = false, isFourState = false, range = [], children = [], childNames = []>
module {
  simulation.design @hierarchy {
    simulation.scope.decl 0 hierarchy "$root"
    simulation.scope.decl 1 parent 0 hierarchy "top" vpi_kind 32
    simulation.vpi_object.anchor @top id 0 type 32 in 1 ordinal 0 hierarchy "top" debug "top" {backing = #simulation.vpi_backing<kind = scope, id = 1 : i64>}
    simulation.storage.decl 0 in 1 : !simulation.event design hierarchy "different" {vpi_type = #event}
    // expected-error @+1 {{backing storage hierarchy must equal the anchor hierarchy}}
    simulation.vpi_object.anchor @cell id 1 type 34 in 1 parent @top ordinal 0 hierarchy "cell" debug "cell" {backing = #simulation.vpi_backing<kind = storage, id = 0 : i64>}
  }
}

// -----

#event = #simulation.vpi_type<kind = event, isSigned = false, isFourState = false, range = [], children = [], childNames = []>
module {
  simulation.design @duplicates {
    simulation.scope.decl 0 hierarchy "$root"
    simulation.scope.decl 1 parent 0 hierarchy "top" vpi_kind 32
    simulation.vpi_object.anchor @top id 0 type 32 in 1 ordinal 0 hierarchy "top" debug "top" {backing = #simulation.vpi_backing<kind = scope, id = 1 : i64>}
    simulation.storage.decl 0 in 1 : !simulation.event design hierarchy "cell" {vpi_type = #event}
    simulation.vpi_object.anchor @cell id 1 type 34 in 1 parent @top ordinal 0 hierarchy "cell" debug "cell" {backing = #simulation.vpi_backing<kind = storage, id = 0 : i64>}
    // expected-error @+1 {{duplicates physical backing used by}}
    simulation.vpi_object.anchor @other id 2 type 34 in 1 parent @top ordinal 1 hierarchy "cell" debug "cell" {backing = #simulation.vpi_backing<kind = storage, id = 0 : i64>}
  }
}

// -----

#event = #simulation.vpi_type<kind = event, isSigned = false, isFourState = false, range = [], children = [], childNames = []>
module {
  simulation.design @fixed_and_stored {
    simulation.scope.decl 0 hierarchy "$root"
    simulation.scope.decl 1 parent 0 hierarchy "top" vpi_kind 32
    simulation.vpi_object.anchor @top id 0 type 32 in 1 ordinal 0 hierarchy "top" debug "top" {backing = #simulation.vpi_backing<kind = scope, id = 1 : i64>}
    simulation.storage.decl 0 in 1 : !simulation.event design hierarchy "cell" {vpi_type = #event}
    simulation.vpi_object.anchor @cell id 1 type 34 in 1 parent @top ordinal 0 hierarchy "cell" debug "cell" {backing = #simulation.vpi_backing<kind = storage, id = 0 : i64>}
    // expected-error @+1 {{event must name an unbacked scalar named-event anchor in the same design}}
    simulation.vpi_event_binding @cell id 0
  }
}
