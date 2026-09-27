// RUN: obelisk-opt %s --split-input-file --verify-diagnostics

module {
  simulation.design @unknown_scope {
    simulation.scope.decl 0 hierarchy "top"
    simulation.code_unit.decl 1 in 0 initial hierarchy "top.exercise"
    simulation.func @exercise(%ctx: !simulation.context
        {simulation.capture_kind = 0 : i32})
        attributes {entry_kind = 1 : i32, code_unit_id = 1 : i64} {
      // expected-error @+1 {{'simulation.virtual_interface.bind' op references an unknown interface scope ID 7}}
      %bad = simulation.virtual_interface.bind 7
        : !simulation.virtual_interface<"@bus", "">
      simulation.return
    }
  }
}

// -----

module {
  simulation.design @bad_scope_kind {
    simulation.scope.decl 0 hierarchy "top"
    simulation.scope.decl 1 parent 0 hierarchy "top.module"
    simulation.code_unit.decl 1 in 0 initial hierarchy "top.bad"
    simulation.func @bad(%context: !simulation.context
        {simulation.capture_kind = 0 : i32})
        attributes {entry_kind = 1 : i32, code_unit_id = 1 : i64} {
      // expected-error @+1 {{'simulation.virtual_interface.bind' op scope ID does not identify an interface instance}}
      %bad = simulation.virtual_interface.bind 1
          : !simulation.virtual_interface<"@bus", "">
      simulation.return
    }
  }
}

// -----

module {
  simulation.design @bad_scope_type {
    simulation.scope.decl 0 hierarchy "top"
    simulation.scope.decl 1 parent 0 hierarchy "top.other" interface "@other"
    simulation.code_unit.decl 1 in 0 initial hierarchy "top.bad"
    simulation.func @bad(%context: !simulation.context
        {simulation.capture_kind = 0 : i32})
        attributes {entry_kind = 1 : i32, code_unit_id = 1 : i64} {
      // expected-error @+1 {{'simulation.virtual_interface.bind' op scope interface specialization does not match result type}}
      %bad = simulation.virtual_interface.bind 1
          : !simulation.virtual_interface<"@bus", "">
      simulation.return
    }
  }
}

// -----

module {
  simulation.design @invalid_cast {
    simulation.scope.decl 0 hierarchy "top"
    simulation.scope.decl 1 parent 0 hierarchy "top.bus" interface "@bus"
    simulation.code_unit.decl 1 in 0 initial hierarchy "top.exercise"
    simulation.func @exercise(%ctx: !simulation.context
        {simulation.capture_kind = 0 : i32})
        attributes {entry_kind = 1 : i32, code_unit_id = 1 : i64} {
      %bus = simulation.virtual_interface.bind 1
        : !simulation.virtual_interface<"@bus", "driver">
      // expected-error @+1 {{'simulation.virtual_interface.cast' op cannot remove or change a selected modport}}
      %bad = simulation.virtual_interface.cast %bus
        : !simulation.virtual_interface<"@bus", "driver"> to
          !simulation.virtual_interface<"@bus", "">
      simulation.return
    }
  }
}

// -----

module {
  simulation.design @invalid_modport_change {
    simulation.scope.decl 0 hierarchy "top"
    simulation.scope.decl 1 parent 0 hierarchy "top.bus" interface "@bus"
    simulation.code_unit.decl 1 in 0 initial hierarchy "top.exercise"
    simulation.func @exercise(%ctx: !simulation.context
        {simulation.capture_kind = 0 : i32})
        attributes {entry_kind = 1 : i32, code_unit_id = 1 : i64} {
      %bus = simulation.virtual_interface.bind 1
        : !simulation.virtual_interface<"@bus", "driver">
      // expected-error @+1 {{'simulation.virtual_interface.cast' op cannot remove or change a selected modport}}
      %bad = simulation.virtual_interface.cast %bus
        : !simulation.virtual_interface<"@bus", "driver"> to
          !simulation.virtual_interface<"@bus", "monitor">
      simulation.return
    }
  }
}

// -----

module {
  simulation.design @invalid_equality {
    simulation.scope.decl 0 hierarchy "top"
    simulation.scope.decl 1 parent 0 hierarchy "top.bus" interface "@bus"
    simulation.scope.decl 2 parent 0 hierarchy "top.other" interface "@other"
    simulation.code_unit.decl 1 in 0 initial hierarchy "top.exercise"
    simulation.func @exercise(%ctx: !simulation.context
        {simulation.capture_kind = 0 : i32})
        attributes {entry_kind = 1 : i32, code_unit_id = 1 : i64} {
      %bus = simulation.virtual_interface.bind 1
        : !simulation.virtual_interface<"@bus", "">
      %other = simulation.virtual_interface.bind 2
        : !simulation.virtual_interface<"@other", "">
      // expected-error @+1 {{'simulation.virtual_interface.equal' op cannot compare different interface specializations}}
      %bad = simulation.virtual_interface.equal %bus, %other
        : !simulation.virtual_interface<"@bus", "">,
          !simulation.virtual_interface<"@other", "">
      simulation.return
    }
  }
}
