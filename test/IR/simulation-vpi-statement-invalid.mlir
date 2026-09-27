// RUN: obelisk-opt --split-input-file --verify-diagnostics %s

module {
  simulation.design @non_statement_kind {
    simulation.scope.decl 0
    simulation.code_unit.decl 1 in 0 initial hierarchy "top.initial"
    // expected-error @below {{VPI kind is not a concrete statement object}}
    simulation.statement.decl 1 in 1 scope 0 type 39
  }
}

// -----

module {
  simulation.design @missing_name {
    simulation.scope.decl 0
    simulation.code_unit.decl 1 in 0 initial hierarchy "top.initial"
    // expected-error @below {{named block requires a nonempty name}}
    simulation.statement.decl 1 in 1 scope 0 type 33
  }
}

// -----

module {
  simulation.design @named_block_not_marked_scope {
    simulation.scope.decl 0
    simulation.code_unit.decl 1 in 0 initial hierarchy "top.initial"
    // expected-error @below {{named begin/fork and foreach statements must be marked is_scope}}
    simulation.statement.decl 1 in 1 scope 0 type 33 name "body"
  }
}

// -----

module {
  simulation.design @foreach_not_marked_scope {
    simulation.scope.decl 0
    simulation.code_unit.decl 1 in 0 initial hierarchy "top.initial"
    // expected-error @below {{named begin/fork and foreach statements must be marked is_scope}}
    simulation.statement.decl 1 in 1 scope 0 type 675
  }
}

// -----

module {
  simulation.design @non_scope_marked_scope {
    simulation.scope.decl 0
    simulation.code_unit.decl 1 in 0 initial hierarchy "top.initial"
    // expected-error @below {{only a scope-capable statement may be marked is_scope}}
    simulation.statement.decl 1 in 1 scope 0 type 38 {is_scope}
  }
}

// -----

module {
  simulation.design @unknown_owner {
    simulation.scope.decl 0
    // expected-error @below {{references an unknown code-unit ID}}
    simulation.statement.decl 1 in 99 scope 0 type 4
    simulation.statement_site.decl 2 on 1 phase 0
  }
}

// -----

module {
  simulation.design @behavioral_statement_without_owner {
    simulation.scope.decl 0
    // expected-error @below {{behavioral statement requires a code-unit ID}}
    simulation.statement.decl 1 scope 0 type 38
  }
}

// -----

module {
  simulation.design @scope_owned_statement_with_owner {
    simulation.scope.decl 0
    simulation.code_unit.decl 1 in 0 initial hierarchy "top.initial"
    // expected-error @below {{scope-owned statement must omit a code-unit ID}}
    simulation.statement.decl 1 in 1 scope 0 type 8
  }
}

// -----

module {
  simulation.design @scope_owner_mismatch {
    simulation.scope.decl 0
    simulation.scope.decl 1 parent 0
    simulation.code_unit.decl 1 in 1 initial hierarchy "top.child.initial"
    // expected-error @below {{scope ID must match the owning code unit's scope}}
    simulation.statement.decl 1 in 1 scope 0 type 4
    simulation.statement_site.decl 2 on 1 phase 0
  }
}

// -----

module {
  simulation.design @illegal_for_phase {
    simulation.scope.decl 0
    simulation.code_unit.decl 1 in 0 initial hierarchy "top.initial"
    simulation.statement.decl 1 in 1 scope 0 type 15
    // expected-error @below {{phase is not legal for the statement's Table 38-6 policy}}
    simulation.statement_site.decl 2 on 1 phase 0
  }
}

// -----

module {
  simulation.design @missing_for_site {
    simulation.scope.decl 0
    simulation.code_unit.decl 1 in 0 initial hierarchy "top.initial"
    // expected-error @below {{does not declare exactly the callback sites required by its Table 38-6 policy}}
    simulation.statement.decl 1 in 1 scope 0 type 15
    simulation.statement_site.decl 2 on 1 phase 1
  }
}

// -----

module {
  simulation.design @parent_cycle {
    simulation.scope.decl 0
    simulation.code_unit.decl 1 in 0 initial hierarchy "top.initial"
    // expected-error @below {{parent statements contain a cycle}}
    simulation.statement.decl 10 in 1 scope 0 type 4 parent 20
    simulation.statement.decl 20 in 1 scope 0 type 4 parent 10
    simulation.statement_site.decl 11 on 10 phase 0
    simulation.statement_site.decl 21 on 20 phase 0
  }
}

// -----

module {
  simulation.design @duplicate_statement_id {
    simulation.scope.decl 0
    simulation.code_unit.decl 1 in 0 initial hierarchy "top.initial"
    simulation.statement.decl 1 in 1 scope 0 type 38
    // expected-error @below {{duplicate statement ID 1}}
    simulation.statement.decl 1 in 1 scope 0 type 38
  }
}

// -----

module {
  simulation.design @duplicate_site_id {
    simulation.scope.decl 0
    simulation.code_unit.decl 1 in 0 initial hierarchy "top.initial"
    simulation.statement.decl 1 in 1 scope 0 type 3
    simulation.statement.decl 3 in 1 scope 0 type 3
    simulation.statement_site.decl 2 on 1 phase 0
    // expected-error @below {{duplicate statement-site ID 2}}
    simulation.statement_site.decl 2 on 3 phase 0
  }
}

// -----

module {
  simulation.design @zero_statement_id {
    simulation.scope.decl 0
    simulation.code_unit.decl 1 in 0 initial hierarchy "top.initial"
    // expected-error @below {{statement ID must be nonzero}}
    simulation.statement.decl 0 in 1 scope 0 type 38
  }
}

// -----

module {
  simulation.design @unknown_site_target {
    simulation.scope.decl 0
    simulation.code_unit.decl 1 in 0 initial hierarchy "top.initial"
    simulation.statement.decl 1 in 1 scope 0 type 38
    // expected-error @below {{references an unknown statement ID}}
    simulation.statement_site.decl 2 on 99 phase 0
  }
}

// -----

module {
  simulation.design @duplicate_phase {
    simulation.scope.decl 0
    simulation.code_unit.decl 1 in 0 initial hierarchy "top.initial"
    simulation.statement.decl 1 in 1 scope 0 type 3
    simulation.statement_site.decl 2 on 1 phase 0
    // expected-error @below {{duplicates a semantic callback phase for the statement}}
    simulation.statement_site.decl 3 on 1 phase 0
  }
}

// -----

module {
  simulation.design @cross_code_unit_parent {
    simulation.scope.decl 0
    simulation.code_unit.decl 1 in 0 initial hierarchy "top.first"
    simulation.code_unit.decl 2 in 0 initial hierarchy "top.second"
    simulation.statement.decl 10 in 1 scope 0 type 38
    // expected-error @below {{references an unknown or cross-owner/scope parent statement}}
    simulation.statement.decl 20 in 2 scope 0 type 38 parent 10
  }
}

// -----

module {
  simulation.design @cross_scope_scope_owned_parent {
    simulation.scope.decl 0
    simulation.scope.decl 1 parent 0
    simulation.statement.decl 10 scope 0 type 8
    // expected-error @below {{references an unknown or cross-owner/scope parent statement}}
    simulation.statement.decl 20 scope 1 type 128 parent 10
  }
}

// -----

module {
  simulation.design @name_on_ordinary_statement {
    simulation.scope.decl 0
    simulation.code_unit.decl 1 in 0 initial hierarchy "top.initial"
    // expected-error @below {{only named begin/fork may carry a name}}
    simulation.statement.decl 1 in 1 scope 0 type 38 name "illegal"
  }
}
