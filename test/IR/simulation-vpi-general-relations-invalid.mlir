// RUN: obelisk-opt --split-input-file --verify-diagnostics %s

module {
  obelisk_sim.design @handle_ordinal {
    // expected-error @below {{vpi_handle relation must use ordinal zero}}
    obelisk_sim.vpi_relation.decl <kind = statement, id = 1 : i64>
        selector 77 handle ordinal 1 to <kind = net, id = 0 : i64>
  }
}

// -----

module {
  obelisk_sim.design @unknown_source {
    obelisk_sim.scope.decl 0 hierarchy "top" vpi_kind 32
    // expected-error @below {{references an unknown source net ID 99}}
    obelisk_sim.vpi_relation.decl <kind = net, id = 99 : i64>
        selector 91 iterate ordinal 0 to <kind = statement, id = 1 : i64>
  }
}

// -----

module {
  obelisk_sim.design @unknown_target {
    obelisk_sim.scope.decl 0 hierarchy "top" vpi_kind 32
    obelisk_sim.statement.decl 1 scope 0 type 8
    obelisk_sim.vpi_statement_relation.decl scope 0 type 32 selector 8
        ordinal 0 modes 2 to 1
    // expected-error @below {{references an unknown target net ID 99}}
    obelisk_sim.vpi_relation.decl <kind = statement, id = 1 : i64>
        selector 77 handle ordinal 0 to <kind = net, id = 99 : i64>
  }
}

// -----

module {
  obelisk_sim.design @illegal_edge {
    obelisk_sim.scope.decl 0 hierarchy "top" vpi_kind 32
    obelisk_sim.statement.decl 1 scope 0 type 8
    obelisk_sim.vpi_statement_relation.decl scope 0 type 32 selector 8
        ordinal 0 modes 2 to 1
    obelisk_sim.net.decl 0 in 0 : !obelisk_sim.logic<1> design
        hierarchy "top.n" debug "n"
    // expected-error @below {{is not a legal non-containment traversal in the generated VPI model}}
    obelisk_sim.vpi_relation.decl <kind = statement, id = 1 : i64>
        selector 91 iterate ordinal 0 to <kind = net, id = 0 : i64>
  }
}

// -----

module {
  obelisk_sim.design @handle_multiplicity {
    obelisk_sim.scope.decl 0 hierarchy "top" vpi_kind 32
    obelisk_sim.statement.decl 1 scope 0 type 8
    obelisk_sim.vpi_statement_relation.decl scope 0 type 32 selector 8
        ordinal 0 modes 2 to 1
    obelisk_sim.net.decl 0 in 0 : !obelisk_sim.logic<1> design
        hierarchy "top.a" debug "a"
    obelisk_sim.net.decl 1 in 0 : !obelisk_sim.logic<1> design
        hierarchy "top.b" debug "b"
    obelisk_sim.vpi_relation.decl <kind = statement, id = 1 : i64>
        selector 77 handle ordinal 0 to <kind = net, id = 0 : i64>
    // expected-error @below {{vpi_handle relation may expose at most one target}}
    obelisk_sim.vpi_relation.decl <kind = statement, id = 1 : i64>
        selector 77 handle ordinal 0 to <kind = net, id = 1 : i64>
  }
}

// -----

module {
  obelisk_sim.design @automatic_edge {
    obelisk_sim.scope.decl 0 hierarchy "top" vpi_kind 32
    obelisk_sim.statement.decl 1 scope 0 type 8
    obelisk_sim.vpi_statement_relation.decl scope 0 type 32 selector 8
        ordinal 0 modes 2 to 1
    obelisk_sim.net.decl 0 in 0 : !obelisk_sim.logic<1> design
        hierarchy "top.n" debug "n"
    // expected-error @below {{is not a legal non-containment traversal in the generated VPI model}}
    obelisk_sim.vpi_relation.decl <kind = net, id = 0 : i64>
        selector 84 handle ordinal 0 to <kind = statement, id = 1 : i64>
  }
}

// -----

module {
  obelisk_sim.design @ordinal_gap {
    obelisk_sim.scope.decl 0 hierarchy "top" vpi_kind 32
    obelisk_sim.statement.decl 1 scope 0 type 8
    obelisk_sim.statement.decl 2 scope 0 type 8
    obelisk_sim.vpi_statement_relation.decl scope 0 type 32 selector 8
        ordinal 0 modes 2 to 1
    obelisk_sim.vpi_statement_relation.decl scope 0 type 32 selector 8
        ordinal 1 modes 2 to 2
    obelisk_sim.net.decl 0 in 0 : !obelisk_sim.logic<1> design
        hierarchy "top.n" debug "n"
    obelisk_sim.vpi_relation.decl <kind = net, id = 0 : i64>
        selector 91 iterate ordinal 0 to <kind = statement, id = 1 : i64>
    // expected-error @below {{ordinals must be dense from zero for each source, selector, and access mode}}
    obelisk_sim.vpi_relation.decl <kind = net, id = 0 : i64>
        selector 91 iterate ordinal 2 to <kind = statement, id = 2 : i64>
  }
}

// -----

module {
  obelisk_sim.design @duplicate_ordinal {
    obelisk_sim.scope.decl 0 hierarchy "top" vpi_kind 32
    obelisk_sim.statement.decl 1 scope 0 type 8
    obelisk_sim.statement.decl 2 scope 0 type 8
    obelisk_sim.vpi_statement_relation.decl scope 0 type 32 selector 8
        ordinal 0 modes 2 to 1
    obelisk_sim.vpi_statement_relation.decl scope 0 type 32 selector 8
        ordinal 1 modes 2 to 2
    obelisk_sim.net.decl 0 in 0 : !obelisk_sim.logic<1> design
        hierarchy "top.n" debug "n"
    obelisk_sim.vpi_relation.decl <kind = net, id = 0 : i64>
        selector 91 iterate ordinal 0 to <kind = statement, id = 1 : i64>
    // expected-error @below {{overlaps another target at the same source, selector, mode, and ordinal}}
    obelisk_sim.vpi_relation.decl <kind = net, id = 0 : i64>
        selector 91 iterate ordinal 0 to <kind = statement, id = 2 : i64>
  }
}

// -----

module {
  obelisk_sim.design @duplicate_target {
    obelisk_sim.scope.decl 0 hierarchy "top" vpi_kind 32
    obelisk_sim.statement.decl 1 scope 0 type 8
    obelisk_sim.vpi_statement_relation.decl scope 0 type 32 selector 8
        ordinal 0 modes 2 to 1
    obelisk_sim.net.decl 0 in 0 : !obelisk_sim.logic<1> design
        hierarchy "top.n" debug "n"
    obelisk_sim.vpi_relation.decl <kind = net, id = 0 : i64>
        selector 91 iterate ordinal 0 to <kind = statement, id = 1 : i64>
    // expected-error @below {{duplicates a target in the same source, selector, and access mode}}
    obelisk_sim.vpi_relation.decl <kind = net, id = 0 : i64>
        selector 91 iterate ordinal 1 to <kind = statement, id = 1 : i64>
  }
}
