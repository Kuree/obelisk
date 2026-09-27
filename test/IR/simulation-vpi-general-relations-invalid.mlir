// RUN: obelisk-opt --split-input-file --verify-diagnostics %s

module {
  simulation.design @handle_ordinal {
    // expected-error @below {{vpi_handle relation must use ordinal zero}}
    simulation.vpi_relation.decl <kind = statement, id = 1 : i64>
        selector 77 handle ordinal 1 to <kind = net, id = 0 : i64>
  }
}

// -----

module {
  simulation.design @unknown_source {
    simulation.scope.decl 0 hierarchy "top" vpi_kind 32
    // expected-error @below {{references an unknown source net ID 99}}
    simulation.vpi_relation.decl <kind = net, id = 99 : i64>
        selector 91 iterate ordinal 0 to <kind = statement, id = 1 : i64>
  }
}

// -----

module {
  simulation.design @unknown_target {
    simulation.scope.decl 0 hierarchy "top" vpi_kind 32
    simulation.statement.decl 1 scope 0 type 8
    simulation.vpi_statement_relation.decl scope 0 type 32 selector 8
        ordinal 0 modes 2 to 1
    // expected-error @below {{references an unknown target net ID 99}}
    simulation.vpi_relation.decl <kind = statement, id = 1 : i64>
        selector 77 handle ordinal 0 to <kind = net, id = 99 : i64>
  }
}

// -----

module {
  simulation.design @unknown_net_identity_reference {
    simulation.scope.decl 0 hierarchy "top" vpi_kind 32
    simulation.net.decl 0 in 0 : !simulation.logic<1> design
        hierarchy "top.n" debug "n"
    // expected-error @below {{references an unknown source net_identity ID 0}}
    simulation.vpi_relation.decl <kind = net_identity, id = 0 : i64>
        selector 126 handle ordinal 0 to <kind = net, id = 0 : i64>
  }
}

// -----

module {
  simulation.design @unknown_net_identity_backing {
    simulation.scope.decl 0 hierarchy "top" vpi_kind 32
    // expected-error @below {{references an unknown backing net ID}}
    simulation.vpi_net_identity.decl 0 backed_by 99 in 0
        : !simulation.logic<1> hierarchy "top.alias" debug "alias" {
      vpi_type = #simulation.vpi_type<kind = logic, isSigned = false,
          isFourState = true, range = [0, 0], children = [], childNames = []>
    }
  }
}

// -----

module {
  simulation.design @net_identity_type_mismatch {
    simulation.scope.decl 0 hierarchy "top" vpi_kind 32
    simulation.net.decl 0 in 0 : !simulation.logic<1> design
        hierarchy "top.n" debug "n"
    // expected-error @below {{net identity width does not match its backing net}}
    simulation.vpi_net_identity.decl 0 backed_by 0 in 0
        : !simulation.logic<2> hierarchy "top.alias" debug "alias" {
      vpi_type = #simulation.vpi_type<kind = logic, isSigned = false,
          isFourState = true, range = [1, 0], children = [], childNames = []>
    }
  }
}

// -----

module {
  simulation.design @duplicate_net_identity_hierarchy {
    simulation.scope.decl 0 hierarchy "top" vpi_kind 32
    simulation.net.decl 0 in 0 : !simulation.logic<1> design
        hierarchy "top.n" debug "n"
    // expected-error @below {{duplicates declared net hierarchy}}
    simulation.vpi_net_identity.decl 0 backed_by 0 in 0
        : !simulation.logic<1> hierarchy "top.n" debug "n" {
      vpi_type = #simulation.vpi_type<kind = logic, isSigned = false,
          isFourState = true, range = [0, 0], children = [], childNames = []>
    }
  }
}

// -----

module {
  simulation.design @missing_net_identity_sim_net {
    simulation.scope.decl 0 hierarchy "top" vpi_kind 32
    simulation.net.decl 0 in 0 : !simulation.logic<1> design
        hierarchy "top.n" debug "n"
    // expected-error @below {{requires one vpiSimNet relation to its backing net}}
    simulation.vpi_net_identity.decl 0 backed_by 0 in 0
        : !simulation.logic<1> hierarchy "top.alias" debug "alias" {
      vpi_type = #simulation.vpi_type<kind = logic, isSigned = false,
          isFourState = true, range = [0, 0], children = [], childNames = []>
    }
  }
}

// -----

module {
  simulation.design @wrong_net_identity_sim_net {
    simulation.scope.decl 0 hierarchy "top" vpi_kind 32
    simulation.net.decl 0 in 0 : !simulation.logic<1> design
        hierarchy "top.a" debug "a"
    simulation.net.decl 1 in 0 : !simulation.logic<1> design
        hierarchy "top.b" debug "b"
    simulation.vpi_net_identity.decl 0 backed_by 0 in 0
        : !simulation.logic<1> hierarchy "top.alias" debug "alias" {
      vpi_type = #simulation.vpi_type<kind = logic, isSigned = false,
          isFourState = true, range = [0, 0], children = [], childNames = []>
    }
    // expected-error @below {{vpiSimNet target does not match the net identity's backing net}}
    simulation.vpi_relation.decl <kind = net_identity, id = 0 : i64>
        selector 126 handle ordinal 0 to <kind = net, id = 1 : i64>
  }
}

// -----

module {
  simulation.design @unknown_net_identity_scope {
    simulation.scope.decl 0 hierarchy "top" vpi_kind 32
    simulation.net.decl 0 in 0 : !simulation.logic<1> design
        hierarchy "top.n" debug "n"
    // expected-error @below {{references an unknown scope ID}}
    simulation.vpi_net_identity.decl 0 backed_by 0 in 99
        : !simulation.logic<1> hierarchy "top.alias" debug "alias" {
      vpi_type = #simulation.vpi_type<kind = logic, isSigned = false,
          isFourState = true, range = [0, 0], children = [], childNames = []>
    }
    simulation.vpi_relation.decl <kind = net_identity, id = 0 : i64>
        selector 126 handle ordinal 0 to <kind = net, id = 0 : i64>
  }
}

// -----

module {
  simulation.design @illegal_edge {
    simulation.scope.decl 0 hierarchy "top" vpi_kind 32
    simulation.statement.decl 1 scope 0 type 8
    simulation.vpi_statement_relation.decl scope 0 type 32 selector 8
        ordinal 0 modes 2 to 1
    simulation.net.decl 0 in 0 : !simulation.logic<1> design
        hierarchy "top.n" debug "n"
    // expected-error @below {{is not a legal non-containment traversal in the generated VPI model}}
    simulation.vpi_relation.decl <kind = statement, id = 1 : i64>
        selector 91 iterate ordinal 0 to <kind = net, id = 0 : i64>
  }
}

// -----

module {
  simulation.design @handle_multiplicity {
    simulation.scope.decl 0 hierarchy "top" vpi_kind 32
    simulation.statement.decl 1 scope 0 type 8
    simulation.vpi_statement_relation.decl scope 0 type 32 selector 8
        ordinal 0 modes 2 to 1
    simulation.net.decl 0 in 0 : !simulation.logic<1> design
        hierarchy "top.a" debug "a"
    simulation.net.decl 1 in 0 : !simulation.logic<1> design
        hierarchy "top.b" debug "b"
    simulation.vpi_relation.decl <kind = statement, id = 1 : i64>
        selector 77 handle ordinal 0 to <kind = net, id = 0 : i64>
    // expected-error @below {{vpi_handle relation may expose at most one target}}
    simulation.vpi_relation.decl <kind = statement, id = 1 : i64>
        selector 77 handle ordinal 0 to <kind = net, id = 1 : i64>
  }
}

// -----

module {
  simulation.design @automatic_edge {
    simulation.scope.decl 0 hierarchy "top" vpi_kind 32
    simulation.statement.decl 1 scope 0 type 8
    simulation.vpi_statement_relation.decl scope 0 type 32 selector 8
        ordinal 0 modes 2 to 1
    simulation.net.decl 0 in 0 : !simulation.logic<1> design
        hierarchy "top.n" debug "n"
    // expected-error @below {{is not a legal non-containment traversal in the generated VPI model}}
    simulation.vpi_relation.decl <kind = net, id = 0 : i64>
        selector 84 handle ordinal 0 to <kind = statement, id = 1 : i64>
  }
}

// -----

module {
  simulation.design @ordinal_gap {
    simulation.scope.decl 0 hierarchy "top" vpi_kind 32
    simulation.statement.decl 1 scope 0 type 8
    simulation.statement.decl 2 scope 0 type 8
    simulation.vpi_statement_relation.decl scope 0 type 32 selector 8
        ordinal 0 modes 2 to 1
    simulation.vpi_statement_relation.decl scope 0 type 32 selector 8
        ordinal 1 modes 2 to 2
    simulation.net.decl 0 in 0 : !simulation.logic<1> design
        hierarchy "top.n" debug "n"
    simulation.vpi_relation.decl <kind = net, id = 0 : i64>
        selector 91 iterate ordinal 0 to <kind = statement, id = 1 : i64>
    // expected-error @below {{ordinals must be dense from zero for each source, selector, and access mode}}
    simulation.vpi_relation.decl <kind = net, id = 0 : i64>
        selector 91 iterate ordinal 2 to <kind = statement, id = 2 : i64>
  }
}

// -----

module {
  simulation.design @duplicate_ordinal {
    simulation.scope.decl 0 hierarchy "top" vpi_kind 32
    simulation.statement.decl 1 scope 0 type 8
    simulation.statement.decl 2 scope 0 type 8
    simulation.vpi_statement_relation.decl scope 0 type 32 selector 8
        ordinal 0 modes 2 to 1
    simulation.vpi_statement_relation.decl scope 0 type 32 selector 8
        ordinal 1 modes 2 to 2
    simulation.net.decl 0 in 0 : !simulation.logic<1> design
        hierarchy "top.n" debug "n"
    simulation.vpi_relation.decl <kind = net, id = 0 : i64>
        selector 91 iterate ordinal 0 to <kind = statement, id = 1 : i64>
    // expected-error @below {{overlaps another target at the same source, selector, mode, and ordinal}}
    simulation.vpi_relation.decl <kind = net, id = 0 : i64>
        selector 91 iterate ordinal 0 to <kind = statement, id = 2 : i64>
  }
}

// -----

module {
  simulation.design @duplicate_target {
    simulation.scope.decl 0 hierarchy "top" vpi_kind 32
    simulation.statement.decl 1 scope 0 type 8
    simulation.vpi_statement_relation.decl scope 0 type 32 selector 8
        ordinal 0 modes 2 to 1
    simulation.net.decl 0 in 0 : !simulation.logic<1> design
        hierarchy "top.n" debug "n"
    simulation.vpi_relation.decl <kind = net, id = 0 : i64>
        selector 91 iterate ordinal 0 to <kind = statement, id = 1 : i64>
    // expected-error @below {{duplicates a target in the same source, selector, and access mode}}
    simulation.vpi_relation.decl <kind = net, id = 0 : i64>
        selector 91 iterate ordinal 1 to <kind = statement, id = 1 : i64>
  }
}
