// RUN: obelisk-opt %s -split-input-file -verify-diagnostics -o /dev/null

module {
  simulation.design @design {
    simulation.scope.decl 0 hierarchy "$root"
    // expected-error @+1 {{modport requires an interface parent and a nonempty declaration name}}
    simulation.vpi_object.anchor @modport id 0 type 606 in 0 ordinal 0 hierarchy "m" debug "m"
  }
}

// -----

module {
  simulation.design @design {
    simulation.scope.decl 0 hierarchy "$root"
    simulation.scope.decl 1 parent 0 hierarchy "top" vpi_kind 32
    simulation.vpi_object.anchor @top id 0 type 32 in 1 ordinal 0 hierarchy "top" debug "top" {backing = #simulation.vpi_backing<kind = scope, id = 1 : i64>}
    // expected-error @+1 {{has an illegal lexical parent kind}}
    simulation.vpi_object.anchor @modport id 1 type 606 in 1 parent @top ordinal 0 hierarchy "top.m" debug "m"
  }
}

// -----

module {
  simulation.design @design {
    simulation.scope.decl 0 hierarchy "$root"
    simulation.scope.decl 1 parent 0 hierarchy "first" vpi_kind 601
    simulation.scope.decl 2 parent 0 hierarchy "second" vpi_kind 601
    simulation.vpi_object.anchor @first id 0 type 601 in 1 ordinal 0 hierarchy "first" debug "first" {backing = #simulation.vpi_backing<kind = scope, id = 1 : i64>}
    // expected-error @+1 {{has an illegal lexical parent kind}}
    simulation.vpi_object.anchor @modport id 1 type 606 in 2 parent @first ordinal 0 hierarchy "first.m" debug "m"
  }
}

// -----

module {
  simulation.design @design {
    simulation.scope.decl 0 hierarchy "$root"
    simulation.scope.decl 1 parent 0 hierarchy "iface" vpi_kind 601
    simulation.vpi_object.anchor @iface id 0 type 601 in 1 ordinal 0 hierarchy "iface" debug "iface" {backing = #simulation.vpi_backing<kind = scope, id = 1 : i64>}
    // expected-error @+1 {{modport requires an interface parent and a nonempty declaration name}}
    simulation.vpi_object.anchor @modport id 1 type 606 in 1 parent @iface ordinal 0 hierarchy "iface.m" debug ""
  }
}

// -----

module {
  simulation.design @design {
    simulation.scope.decl 0 hierarchy "$root"
    simulation.scope.decl 1 parent 0 hierarchy "iface" vpi_kind 601
    simulation.vpi_object.anchor @iface id 0 type 601 in 1 ordinal 0 hierarchy "iface" debug "iface" {backing = #simulation.vpi_backing<kind = scope, id = 1 : i64>}
    simulation.vpi_object.anchor @first id 1 type 606 in 1 parent @iface ordinal 0 hierarchy "iface.first" debug "m.dot"
    // expected-error @+1 {{duplicates a modport declaration name in the same interface}}
    simulation.vpi_object.anchor @second id 2 type 606 in 1 parent @iface ordinal 1 hierarchy "iface.second" debug "m.dot"
  }
}
