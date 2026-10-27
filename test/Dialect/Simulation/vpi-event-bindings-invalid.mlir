// RUN: talos-opt %s --split-input-file --verify-diagnostics

module {
  simulation.design @wrong_kind {
    simulation.scope.decl 0 hierarchy "$root"
    simulation.scope.decl 1 parent 0 hierarchy "top" vpi_kind 32
    simulation.vpi_object.anchor @module id 0 type 32 in 1 ordinal 0
        hierarchy "top" debug "top" {
      backing = #simulation.vpi_backing<kind = scope, id = 1 : i64>
    }
    // expected-error @+1 {{event must name an unbacked scalar named-event anchor in the same design}}
    simulation.vpi_event_binding @module id 0
  }
}

// -----

module {
  simulation.design @null_identity {
    simulation.scope.decl 0 hierarchy "$root"
    simulation.vpi_object.anchor @event id 0 type 34 in 0 ordinal 0
        hierarchy "event" debug "event"
    // expected-error @+1 {{event ID must be nonnegative}}
    simulation.vpi_event_binding @event id -2
  }
}

// -----

module {
  simulation.design @duplicates {
    simulation.scope.decl 0 hierarchy "$root"
    simulation.vpi_object.anchor @event id 0 type 34 in 0 ordinal 0
        hierarchy "event" debug "event"
    // expected-error @+1 {{duplicates a named-event binding}}
    simulation.vpi_event_binding @event id 0
    simulation.vpi_event_binding @event id 1
  }
}

// -----

module {
  simulation.design @missing_event {
    simulation.scope.decl 0 hierarchy "$root"
    // expected-error @+1 {{event must name an unbacked scalar named-event anchor in the same design}}
    simulation.vpi_event_binding @missing id 0
  }
}
