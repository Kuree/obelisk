// RUN: obelisk-opt %s --split-input-file --verify-diagnostics

module {
  obelisk_sim.design @unknown_scope {
    obelisk_sim.scope.decl 0 hierarchy "top"
    obelisk_sim.code_unit.decl 1 in 0 initial hierarchy "top.exercise"
    obelisk_sim.func @exercise(%ctx: !obelisk_sim.context
        {obelisk_sim.capture_kind = 0 : i32})
        attributes {entry_kind = 1 : i32, code_unit_id = 1 : i64} {
      // expected-error @+1 {{'obelisk_sim.virtual_interface.bind' op references an unknown interface scope ID 7}}
      %bad = obelisk_sim.virtual_interface.bind 7
        : !obelisk_sim.virtual_interface<"@bus", "">
      obelisk_sim.return
    }
  }
}

// -----

module {
  obelisk_sim.design @bad_scope_kind {
    obelisk_sim.scope.decl 0 hierarchy "top"
    obelisk_sim.scope.decl 1 parent 0 hierarchy "top.module"
    obelisk_sim.code_unit.decl 1 in 0 initial hierarchy "top.bad"
    obelisk_sim.func @bad(%context: !obelisk_sim.context
        {obelisk_sim.capture_kind = 0 : i32})
        attributes {entry_kind = 1 : i32, code_unit_id = 1 : i64} {
      // expected-error @+1 {{'obelisk_sim.virtual_interface.bind' op scope ID does not identify an interface instance}}
      %bad = obelisk_sim.virtual_interface.bind 1
          : !obelisk_sim.virtual_interface<"@bus", "">
      obelisk_sim.return
    }
  }
}

// -----

module {
  obelisk_sim.design @bad_scope_type {
    obelisk_sim.scope.decl 0 hierarchy "top"
    obelisk_sim.scope.decl 1 parent 0 hierarchy "top.other" interface "@other"
    obelisk_sim.code_unit.decl 1 in 0 initial hierarchy "top.bad"
    obelisk_sim.func @bad(%context: !obelisk_sim.context
        {obelisk_sim.capture_kind = 0 : i32})
        attributes {entry_kind = 1 : i32, code_unit_id = 1 : i64} {
      // expected-error @+1 {{'obelisk_sim.virtual_interface.bind' op scope interface specialization does not match result type}}
      %bad = obelisk_sim.virtual_interface.bind 1
          : !obelisk_sim.virtual_interface<"@bus", "">
      obelisk_sim.return
    }
  }
}

// -----

module {
  obelisk_sim.design @invalid_cast {
    obelisk_sim.scope.decl 0 hierarchy "top"
    obelisk_sim.scope.decl 1 parent 0 hierarchy "top.bus" interface "@bus"
    obelisk_sim.code_unit.decl 1 in 0 initial hierarchy "top.exercise"
    obelisk_sim.func @exercise(%ctx: !obelisk_sim.context
        {obelisk_sim.capture_kind = 0 : i32})
        attributes {entry_kind = 1 : i32, code_unit_id = 1 : i64} {
      %bus = obelisk_sim.virtual_interface.bind 1
        : !obelisk_sim.virtual_interface<"@bus", "driver">
      // expected-error @+1 {{'obelisk_sim.virtual_interface.cast' op cannot remove or change a selected modport}}
      %bad = obelisk_sim.virtual_interface.cast %bus
        : !obelisk_sim.virtual_interface<"@bus", "driver"> to
          !obelisk_sim.virtual_interface<"@bus", "">
      obelisk_sim.return
    }
  }
}

// -----

module {
  obelisk_sim.design @invalid_modport_change {
    obelisk_sim.scope.decl 0 hierarchy "top"
    obelisk_sim.scope.decl 1 parent 0 hierarchy "top.bus" interface "@bus"
    obelisk_sim.code_unit.decl 1 in 0 initial hierarchy "top.exercise"
    obelisk_sim.func @exercise(%ctx: !obelisk_sim.context
        {obelisk_sim.capture_kind = 0 : i32})
        attributes {entry_kind = 1 : i32, code_unit_id = 1 : i64} {
      %bus = obelisk_sim.virtual_interface.bind 1
        : !obelisk_sim.virtual_interface<"@bus", "driver">
      // expected-error @+1 {{'obelisk_sim.virtual_interface.cast' op cannot remove or change a selected modport}}
      %bad = obelisk_sim.virtual_interface.cast %bus
        : !obelisk_sim.virtual_interface<"@bus", "driver"> to
          !obelisk_sim.virtual_interface<"@bus", "monitor">
      obelisk_sim.return
    }
  }
}

// -----

module {
  obelisk_sim.design @invalid_equality {
    obelisk_sim.scope.decl 0 hierarchy "top"
    obelisk_sim.scope.decl 1 parent 0 hierarchy "top.bus" interface "@bus"
    obelisk_sim.scope.decl 2 parent 0 hierarchy "top.other" interface "@other"
    obelisk_sim.code_unit.decl 1 in 0 initial hierarchy "top.exercise"
    obelisk_sim.func @exercise(%ctx: !obelisk_sim.context
        {obelisk_sim.capture_kind = 0 : i32})
        attributes {entry_kind = 1 : i32, code_unit_id = 1 : i64} {
      %bus = obelisk_sim.virtual_interface.bind 1
        : !obelisk_sim.virtual_interface<"@bus", "">
      %other = obelisk_sim.virtual_interface.bind 2
        : !obelisk_sim.virtual_interface<"@other", "">
      // expected-error @+1 {{'obelisk_sim.virtual_interface.equal' op cannot compare different interface specializations}}
      %bad = obelisk_sim.virtual_interface.equal %bus, %other
        : !obelisk_sim.virtual_interface<"@bus", "">,
          !obelisk_sim.virtual_interface<"@other", "">
      obelisk_sim.return
    }
  }
}
