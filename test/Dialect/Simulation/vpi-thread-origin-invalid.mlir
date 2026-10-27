// RUN: talos-opt %s --split-input-file --verify-diagnostics

module {
  simulation.design @negative {
    simulation.scope.decl 0 hierarchy "top" vpi_kind 32
    simulation.scope.decl 1 parent 0 hierarchy "top.other" vpi_kind 32
    simulation.code_unit.decl 10 in 0 initial hierarchy "top.initial"
    simulation.statement.decl 1 in 10 scope 0 type 38
    // expected-error @+1 {{code-unit ID must be nonnegative}}
    simulation.vpi_thread_origin_binding -1 origin 1
  }
}

// -----

module {
  simulation.design @zero {
    simulation.scope.decl 0 hierarchy "top" vpi_kind 32
    simulation.scope.decl 1 parent 0 hierarchy "top.other" vpi_kind 32
    simulation.code_unit.decl 10 in 0 initial hierarchy "top.initial"
    simulation.statement.decl 1 in 10 scope 0 type 38
    // expected-error @+1 {{code-unit and statement IDs must be nonzero}}
    simulation.vpi_thread_origin_binding 0 origin 1
  }
}

// -----

module {
  simulation.design @missing_unit {
    simulation.scope.decl 0 hierarchy "top" vpi_kind 32
    simulation.scope.decl 1 parent 0 hierarchy "top.other" vpi_kind 32
    simulation.code_unit.decl 10 in 0 initial hierarchy "top.initial"
    simulation.statement.decl 1 in 10 scope 0 type 38
    // expected-error @+1 {{references an unknown code-unit ID}}
    simulation.vpi_thread_origin_binding 99 origin 1
  }
}

// -----

module {
  simulation.design @missing_statement {
    simulation.scope.decl 0 hierarchy "top" vpi_kind 32
    simulation.scope.decl 1 parent 0 hierarchy "top.other" vpi_kind 32
    simulation.code_unit.decl 10 in 0 initial hierarchy "top.initial"
    simulation.statement.decl 1 in 10 scope 0 type 38
    // expected-error @+1 {{origin must name an existing behavioral statement}}
    simulation.vpi_thread_origin_binding 10 origin 99
  }
}

// -----

module {
  simulation.design @scope_statement {
    simulation.scope.decl 0 hierarchy "top" vpi_kind 32
    simulation.scope.decl 1 parent 0 hierarchy "top.other" vpi_kind 32
    simulation.code_unit.decl 10 in 0 initial hierarchy "top.initial"
    simulation.statement.decl 1 in 10 scope 0 type 38
    simulation.statement.decl 2 scope 0 type 8
    // expected-error @+1 {{origin must name an existing behavioral statement}}
    simulation.vpi_thread_origin_binding 10 origin 2
  }
}

// -----

module {
  simulation.design @duplicate {
    simulation.scope.decl 0 hierarchy "top" vpi_kind 32
    simulation.scope.decl 1 parent 0 hierarchy "top.other" vpi_kind 32
    simulation.code_unit.decl 10 in 0 initial hierarchy "top.initial"
    simulation.statement.decl 1 in 10 scope 0 type 38
    simulation.vpi_thread_origin_binding 10 origin 1
    // expected-error @+1 {{duplicates a thread-origin code-unit binding}}
    simulation.vpi_thread_origin_binding 10 origin 1
  }
}

// -----

module {
  simulation.design @cross_scope {
    simulation.scope.decl 0 hierarchy "top" vpi_kind 32
    simulation.scope.decl 1 parent 0 hierarchy "top.other" vpi_kind 32
    simulation.code_unit.decl 10 in 0 initial hierarchy "top.initial"
    simulation.statement.decl 1 in 10 scope 0 type 38
    simulation.code_unit.decl 20 in 1 fork hierarchy "other.$fork"
    // expected-error @+1 {{origin and code unit must have the same scope}}
    simulation.vpi_thread_origin_binding 20 origin 1
  }
}

// -----

module {
  simulation.design @public_cross_owner {
    simulation.scope.decl 0 hierarchy "top" vpi_kind 32
    simulation.scope.decl 1 parent 0 hierarchy "top.other" vpi_kind 32
    simulation.code_unit.decl 10 in 0 initial hierarchy "top.initial"
    simulation.statement.decl 1 in 10 scope 0 type 38
    simulation.code_unit.decl 20 in 0 initial hierarchy "top.second"
    // expected-error @+1 {{only an internal code unit may reference another owner's statement}}
    simulation.vpi_thread_origin_binding 20 origin 1
  }
}
