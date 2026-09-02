// RUN: obelisk-opt --split-input-file --verify-diagnostics %s

module {
  obelisk_sim.design @non_statement_kind {
    obelisk_sim.scope.decl 0
    obelisk_sim.code_unit.decl 1 in 0 initial hierarchy "top.initial"
    // expected-error @below {{VPI kind is not a concrete statement object}}
    obelisk_sim.statement.decl 1 in 1 scope 0 type 39
  }
}

// -----

module {
  obelisk_sim.design @missing_name {
    obelisk_sim.scope.decl 0
    obelisk_sim.code_unit.decl 1 in 0 initial hierarchy "top.initial"
    // expected-error @below {{named block requires a nonempty name}}
    obelisk_sim.statement.decl 1 in 1 scope 0 type 33
  }
}

// -----

module {
  obelisk_sim.design @unknown_owner {
    obelisk_sim.scope.decl 0
    // expected-error @below {{references an unknown code-unit ID}}
    obelisk_sim.statement.decl 1 in 99 scope 0 type 4
    obelisk_sim.statement_site.decl 2 on 1 phase 0
  }
}

// -----

module {
  obelisk_sim.design @behavioral_statement_without_owner {
    obelisk_sim.scope.decl 0
    // expected-error @below {{behavioral statement requires a code-unit ID}}
    obelisk_sim.statement.decl 1 scope 0 type 38
  }
}

// -----

module {
  obelisk_sim.design @scope_owned_statement_with_owner {
    obelisk_sim.scope.decl 0
    obelisk_sim.code_unit.decl 1 in 0 initial hierarchy "top.initial"
    // expected-error @below {{scope-owned statement must omit a code-unit ID}}
    obelisk_sim.statement.decl 1 in 1 scope 0 type 8
  }
}

// -----

module {
  obelisk_sim.design @scope_owner_mismatch {
    obelisk_sim.scope.decl 0
    obelisk_sim.scope.decl 1 parent 0
    obelisk_sim.code_unit.decl 1 in 1 initial hierarchy "top.child.initial"
    // expected-error @below {{scope ID must match the owning code unit's scope}}
    obelisk_sim.statement.decl 1 in 1 scope 0 type 4
    obelisk_sim.statement_site.decl 2 on 1 phase 0
  }
}

// -----

module {
  obelisk_sim.design @illegal_for_phase {
    obelisk_sim.scope.decl 0
    obelisk_sim.code_unit.decl 1 in 0 initial hierarchy "top.initial"
    obelisk_sim.statement.decl 1 in 1 scope 0 type 15
    // expected-error @below {{phase is not legal for the statement's Table 38-6 policy}}
    obelisk_sim.statement_site.decl 2 on 1 phase 0
  }
}

// -----

module {
  obelisk_sim.design @missing_for_site {
    obelisk_sim.scope.decl 0
    obelisk_sim.code_unit.decl 1 in 0 initial hierarchy "top.initial"
    // expected-error @below {{does not declare exactly the callback sites required by its Table 38-6 policy}}
    obelisk_sim.statement.decl 1 in 1 scope 0 type 15
    obelisk_sim.statement_site.decl 2 on 1 phase 1
  }
}

// -----

module {
  obelisk_sim.design @parent_cycle {
    obelisk_sim.scope.decl 0
    obelisk_sim.code_unit.decl 1 in 0 initial hierarchy "top.initial"
    // expected-error @below {{parent statements contain a cycle}}
    obelisk_sim.statement.decl 10 in 1 scope 0 type 4 parent 20
    obelisk_sim.statement.decl 20 in 1 scope 0 type 4 parent 10
    obelisk_sim.statement_site.decl 11 on 10 phase 0
    obelisk_sim.statement_site.decl 21 on 20 phase 0
  }
}

// -----

module {
  obelisk_sim.design @duplicate_statement_id {
    obelisk_sim.scope.decl 0
    obelisk_sim.code_unit.decl 1 in 0 initial hierarchy "top.initial"
    obelisk_sim.statement.decl 1 in 1 scope 0 type 38
    // expected-error @below {{duplicate statement ID 1}}
    obelisk_sim.statement.decl 1 in 1 scope 0 type 38
  }
}

// -----

module {
  obelisk_sim.design @duplicate_site_id {
    obelisk_sim.scope.decl 0
    obelisk_sim.code_unit.decl 1 in 0 initial hierarchy "top.initial"
    obelisk_sim.statement.decl 1 in 1 scope 0 type 3
    obelisk_sim.statement.decl 3 in 1 scope 0 type 3
    obelisk_sim.statement_site.decl 2 on 1 phase 0
    // expected-error @below {{duplicate statement-site ID 2}}
    obelisk_sim.statement_site.decl 2 on 3 phase 0
  }
}

// -----

module {
  obelisk_sim.design @zero_statement_id {
    obelisk_sim.scope.decl 0
    obelisk_sim.code_unit.decl 1 in 0 initial hierarchy "top.initial"
    // expected-error @below {{statement ID must be nonzero}}
    obelisk_sim.statement.decl 0 in 1 scope 0 type 38
  }
}

// -----

module {
  obelisk_sim.design @unknown_site_target {
    obelisk_sim.scope.decl 0
    obelisk_sim.code_unit.decl 1 in 0 initial hierarchy "top.initial"
    obelisk_sim.statement.decl 1 in 1 scope 0 type 38
    // expected-error @below {{references an unknown statement ID}}
    obelisk_sim.statement_site.decl 2 on 99 phase 0
  }
}

// -----

module {
  obelisk_sim.design @duplicate_phase {
    obelisk_sim.scope.decl 0
    obelisk_sim.code_unit.decl 1 in 0 initial hierarchy "top.initial"
    obelisk_sim.statement.decl 1 in 1 scope 0 type 3
    obelisk_sim.statement_site.decl 2 on 1 phase 0
    // expected-error @below {{duplicates a semantic callback phase for the statement}}
    obelisk_sim.statement_site.decl 3 on 1 phase 0
  }
}

// -----

module {
  obelisk_sim.design @cross_code_unit_parent {
    obelisk_sim.scope.decl 0
    obelisk_sim.code_unit.decl 1 in 0 initial hierarchy "top.first"
    obelisk_sim.code_unit.decl 2 in 0 initial hierarchy "top.second"
    obelisk_sim.statement.decl 10 in 1 scope 0 type 38
    // expected-error @below {{references an unknown or cross-owner/scope parent statement}}
    obelisk_sim.statement.decl 20 in 2 scope 0 type 38 parent 10
  }
}

// -----

module {
  obelisk_sim.design @cross_scope_scope_owned_parent {
    obelisk_sim.scope.decl 0
    obelisk_sim.scope.decl 1 parent 0
    obelisk_sim.statement.decl 10 scope 0 type 8
    // expected-error @below {{references an unknown or cross-owner/scope parent statement}}
    obelisk_sim.statement.decl 20 scope 1 type 128 parent 10
  }
}

// -----

module {
  obelisk_sim.design @name_on_ordinary_statement {
    obelisk_sim.scope.decl 0
    obelisk_sim.code_unit.decl 1 in 0 initial hierarchy "top.initial"
    // expected-error @below {{only named begin/fork may carry a name}}
    obelisk_sim.statement.decl 1 in 1 scope 0 type 38 name "illegal"
  }
}
