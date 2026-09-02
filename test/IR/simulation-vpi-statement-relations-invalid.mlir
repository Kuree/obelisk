// RUN: obelisk-opt --split-input-file --verify-diagnostics %s

module {
  obelisk_sim.design @empty_mode_mask {
    // expected-error @below {{mode mask must contain only vpi_handle and/or vpi_iterate}}
    obelisk_sim.vpi_statement_relation.decl scope 0 type 32 selector 8 ordinal 0 modes 0 to 1
  }
}

// -----

module {
  obelisk_sim.design @reserved_mode_bit {
    // expected-error @below {{mode mask must contain only vpi_handle and/or vpi_iterate}}
    obelisk_sim.vpi_statement_relation.decl scope 0 type 32 selector 8 ordinal 0 modes 4 to 1
  }
}

// -----

module {
  obelisk_sim.design @wide_ordinal {
    // expected-error @below {{ordinal exceeds the reflection encoding}}
    obelisk_sim.vpi_statement_relation.decl scope 0 type 32 selector 8 ordinal 4294967296 modes 2 to 1
  }
}

// -----

module {
  obelisk_sim.design @non_object_source_kind {
    // expected-error @below {{source VPI kind is not a concrete object}}
    obelisk_sim.vpi_statement_relation.decl scope 0 type 104 selector 8 ordinal 0 modes 2 to 1
  }
}

// -----

module {
  obelisk_sim.design @unknown_scope_source {
    obelisk_sim.scope.decl 0
    obelisk_sim.statement.decl 1 scope 0 type 8 loc("test.sv":1:1)
    // expected-error @below {{references an unknown source scope ID}}
    obelisk_sim.vpi_statement_relation.decl scope 99 type 32 selector 8 ordinal 0 modes 2 to 1
  }
}

// -----

module {
  obelisk_sim.design @unknown_target {
    obelisk_sim.scope.decl 0
    // expected-error @below {{references an unknown target statement ID}}
    obelisk_sim.vpi_statement_relation.decl scope 0 type 32 selector 8 ordinal 0 modes 2 to 99
  }
}

// -----

module {
  obelisk_sim.design @scope_kind_is_not_scope {
    obelisk_sim.scope.decl 0
    obelisk_sim.statement.decl 10 scope 0 type 8 loc("test.sv":1:1)
    // expected-error @below {{scope source VPI kind is not a scope object}}
    obelisk_sim.vpi_statement_relation.decl scope 0 type 24 selector 104 ordinal 0 modes 1 to 10
  }
}

// -----

module {
  obelisk_sim.design @inconsistent_scope_kinds {
    obelisk_sim.scope.decl 0
    obelisk_sim.statement.decl 10 scope 0 type 8 loc("test.sv":1:1)
    obelisk_sim.statement.decl 20 scope 0 type 646 loc("test.sv":2:1)
    obelisk_sim.vpi_statement_relation.decl scope 0 type 32 selector 8 ordinal 0 modes 2 to 10
    // expected-error @below {{source scope has inconsistent exact VPI kinds}}
    obelisk_sim.vpi_statement_relation.decl scope 0 type 134 selector 646 ordinal 0 modes 2 to 20
  }
}

// -----

module {
  obelisk_sim.design @interface_scope_claims_module_kind {
    obelisk_sim.scope.decl 0
    obelisk_sim.scope.decl 1 parent 0 interface "I"
    obelisk_sim.statement.decl 10 scope 1 type 8 loc("test.sv":1:1)
    // expected-error @below {{interface scope metadata and source VPI kind disagree}}
    obelisk_sim.vpi_statement_relation.decl scope 1 type 32 selector 8 ordinal 0 modes 2 to 10
  }
}

// -----

module {
  obelisk_sim.design @plain_scope_claims_interface_kind {
    obelisk_sim.scope.decl 0
    obelisk_sim.statement.decl 10 scope 0 type 8 loc("test.sv":1:1)
    // expected-error @below {{interface scope metadata and source VPI kind disagree}}
    obelisk_sim.vpi_statement_relation.decl scope 0 type 601 selector 8 ordinal 0 modes 2 to 10
  }
}

// -----

module {
  obelisk_sim.design @unknown_code_unit_source {
    obelisk_sim.scope.decl 0
    obelisk_sim.statement.decl 10 in 99 scope 0 type 4 loc("test.sv":1:1)
    obelisk_sim.statement_site.decl 11 on 10 phase 0
    // expected-error @below {{references an unknown source code-unit ID}}
    obelisk_sim.vpi_statement_relation.decl code_unit 98 type 24 selector 104 ordinal 0 modes 1 to 10
  }
}

// -----

module {
  obelisk_sim.design @unknown_statement_source {
    obelisk_sim.scope.decl 0
    obelisk_sim.code_unit.decl 1 in 0 initial hierarchy "top.initial"
    obelisk_sim.statement.decl 10 in 1 scope 0 type 4 loc("test.sv":1:1)
    obelisk_sim.statement_site.decl 11 on 10 phase 0
    obelisk_sim.statement.decl 20 in 1 scope 0 type 3 parent 10 loc("test.sv":2:1)
    obelisk_sim.statement_site.decl 21 on 20 phase 0
    obelisk_sim.vpi_statement_relation.decl code_unit 1 type 24 selector 104 ordinal 0 modes 1 to 10
    // expected-error @below {{references an unknown source statement ID}}
    obelisk_sim.vpi_statement_relation.decl statement 99 type 4 selector 104 ordinal 0 modes 2 to 20
  }
}

// -----

module {
  obelisk_sim.design @wrong_code_unit_vpi_kind {
    obelisk_sim.scope.decl 0
    obelisk_sim.code_unit.decl 1 in 0 initial hierarchy "top.initial"
    obelisk_sim.statement.decl 10 in 1 scope 0 type 4 loc("test.sv":1:1)
    obelisk_sim.statement_site.decl 11 on 10 phase 0
    // expected-error @below {{source VPI kind does not match the code-unit kind}}
    obelisk_sim.vpi_statement_relation.decl code_unit 1 type 1 selector 104 ordinal 0 modes 1 to 10
  }
}

// -----

module {
  obelisk_sim.design @wrong_statement_vpi_kind {
    obelisk_sim.scope.decl 0
    obelisk_sim.code_unit.decl 1 in 0 initial hierarchy "top.initial"
    obelisk_sim.statement.decl 10 in 1 scope 0 type 4 loc("test.sv":1:1)
    obelisk_sim.statement_site.decl 11 on 10 phase 0
    obelisk_sim.statement.decl 20 in 1 scope 0 type 3 parent 10 loc("test.sv":2:1)
    obelisk_sim.statement_site.decl 21 on 20 phase 0
    obelisk_sim.vpi_statement_relation.decl code_unit 1 type 24 selector 104 ordinal 0 modes 1 to 10
    // expected-error @below {{source VPI kind does not match the statement declaration}}
    obelisk_sim.vpi_statement_relation.decl statement 10 type 15 selector 104 ordinal 0 modes 1 to 20
  }
}

// -----

module {
  obelisk_sim.design @cross_parent_relation {
    obelisk_sim.scope.decl 0
    obelisk_sim.code_unit.decl 1 in 0 initial hierarchy "top.initial"
    obelisk_sim.statement.decl 10 in 1 scope 0 type 4 loc("test.sv":1:1)
    obelisk_sim.statement_site.decl 11 on 10 phase 0
    obelisk_sim.statement.decl 20 in 1 scope 0 type 3 parent 10 loc("test.sv":2:1)
    obelisk_sim.statement_site.decl 21 on 20 phase 0
    obelisk_sim.vpi_statement_relation.decl code_unit 1 type 24 selector 104 ordinal 0 modes 1 to 10
    // expected-error @below {{statement source does not match the target's structural parent}}
    obelisk_sim.vpi_statement_relation.decl statement 20 type 3 selector 104 ordinal 0 modes 1 to 20
  }
}

// -----

module {
  obelisk_sim.design @not_containment {
    obelisk_sim.scope.decl 0
    obelisk_sim.code_unit.decl 1 in 0 initial hierarchy "top.initial"
    obelisk_sim.statement.decl 10 in 1 scope 0 type 3 loc("test.sv":1:1)
    obelisk_sim.statement_site.decl 11 on 10 phase 0
    obelisk_sim.statement.decl 20 in 1 scope 0 type 3 parent 10 loc("test.sv":2:1)
    obelisk_sim.statement_site.decl 21 on 20 phase 0
    obelisk_sim.vpi_statement_relation.decl code_unit 1 type 24 selector 104 ordinal 0 modes 1 to 10
    // expected-error @below {{is not a legal statement-containment traversal in the generated VPI model}}
    obelisk_sim.vpi_statement_relation.decl statement 10 type 3 selector 104 ordinal 0 modes 1 to 20
  }
}

// -----

module {
  obelisk_sim.design @target_outside_generated_set {
    obelisk_sim.scope.decl 0
    obelisk_sim.code_unit.decl 1 in 0 initial hierarchy "top.initial"
    obelisk_sim.statement.decl 10 in 1 scope 0 type 5 loc("test.sv":1:1)
    obelisk_sim.statement_site.decl 11 on 10 phase 0
    obelisk_sim.statement.decl 20 in 1 scope 0 type 3 parent 10 loc("test.sv":2:1)
    obelisk_sim.statement_site.decl 21 on 20 phase 0
    obelisk_sim.vpi_statement_relation.decl code_unit 1 type 24 selector 104 ordinal 0 modes 1 to 10
    // expected-error @below {{is not a legal statement-containment traversal in the generated VPI model}}
    obelisk_sim.vpi_statement_relation.decl statement 10 type 5 selector 6 ordinal 0 modes 2 to 20
  }
}

// -----

module {
  obelisk_sim.design @mode_not_supported_by_relation {
    obelisk_sim.scope.decl 0
    obelisk_sim.code_unit.decl 1 in 0 initial hierarchy "top.initial"
    obelisk_sim.statement.decl 10 in 1 scope 0 type 4 loc("test.sv":1:1)
    obelisk_sim.statement_site.decl 11 on 10 phase 0
    obelisk_sim.statement.decl 20 in 1 scope 0 type 3 parent 10 loc("test.sv":2:1)
    obelisk_sim.statement_site.decl 21 on 20 phase 0
    obelisk_sim.vpi_statement_relation.decl code_unit 1 type 24 selector 104 ordinal 0 modes 1 to 10
    // expected-error @below {{is not a legal statement-containment traversal in the generated VPI model}}
    obelisk_sim.vpi_statement_relation.decl statement 10 type 4 selector 104 ordinal 0 modes 3 to 20
  }
}

// -----

module {
  obelisk_sim.design @dual_mode_handle_only {
    obelisk_sim.scope.decl 0
    obelisk_sim.code_unit.decl 1 in 0 initial hierarchy "top.initial"
    obelisk_sim.statement.decl 10 in 1 scope 0 type 15 loc("test.sv":1:1)
    obelisk_sim.statement_site.decl 11 on 10 phase 1
    obelisk_sim.statement_site.decl 12 on 10 phase 2
    obelisk_sim.statement.decl 20 in 1 scope 0 type 3 parent 10 loc("test.sv":2:1)
    obelisk_sim.statement_site.decl 21 on 20 phase 0
    obelisk_sim.vpi_statement_relation.decl code_unit 1 type 24 selector 104 ordinal 0 modes 1 to 10
    // expected-error @below {{ordinal zero must merge vpi_handle and vpi_iterate for a dual-mode statement relation}}
    obelisk_sim.vpi_statement_relation.decl statement 10 type 15 selector 75 ordinal 0 modes 1 to 20
  }
}

// -----

module {
  obelisk_sim.design @dual_mode_iterate_only {
    obelisk_sim.scope.decl 0
    obelisk_sim.code_unit.decl 1 in 0 initial hierarchy "top.initial"
    obelisk_sim.statement.decl 10 in 1 scope 0 type 15 loc("test.sv":1:1)
    obelisk_sim.statement_site.decl 11 on 10 phase 1
    obelisk_sim.statement_site.decl 12 on 10 phase 2
    obelisk_sim.statement.decl 20 in 1 scope 0 type 3 parent 10 loc("test.sv":2:1)
    obelisk_sim.statement_site.decl 21 on 20 phase 0
    obelisk_sim.vpi_statement_relation.decl code_unit 1 type 24 selector 104 ordinal 0 modes 1 to 10
    // expected-error @below {{ordinal zero must merge vpi_handle and vpi_iterate for a dual-mode statement relation}}
    obelisk_sim.vpi_statement_relation.decl statement 10 type 15 selector 75 ordinal 0 modes 2 to 20
  }
}

// -----

module {
  obelisk_sim.design @dual_mode_split_targets {
    obelisk_sim.scope.decl 0
    obelisk_sim.code_unit.decl 1 in 0 initial hierarchy "top.initial"
    obelisk_sim.statement.decl 10 in 1 scope 0 type 15 loc("test.sv":1:1)
    obelisk_sim.statement_site.decl 11 on 10 phase 1
    obelisk_sim.statement_site.decl 12 on 10 phase 2
    obelisk_sim.statement.decl 20 in 1 scope 0 type 3 parent 10 loc("test.sv":2:1)
    obelisk_sim.statement_site.decl 21 on 20 phase 0
    obelisk_sim.statement.decl 30 in 1 scope 0 type 3 parent 10 loc("test.sv":3:1)
    obelisk_sim.statement_site.decl 31 on 30 phase 0
    obelisk_sim.vpi_statement_relation.decl code_unit 1 type 24 selector 104 ordinal 0 modes 1 to 10
    // expected-error @below {{ordinal zero must merge vpi_handle and vpi_iterate for a dual-mode statement relation}}
    obelisk_sim.vpi_statement_relation.decl statement 10 type 15 selector 75 ordinal 0 modes 1 to 20
    obelisk_sim.vpi_statement_relation.decl statement 10 type 15 selector 75 ordinal 0 modes 2 to 30
  }
}

// -----

module {
  obelisk_sim.design @duplicate_target_modes_not_merged {
    obelisk_sim.scope.decl 0
    obelisk_sim.code_unit.decl 1 in 0 initial hierarchy "top.initial"
    obelisk_sim.statement.decl 10 in 1 scope 0 type 15 loc("test.sv":1:1)
    obelisk_sim.statement_site.decl 11 on 10 phase 1
    obelisk_sim.statement_site.decl 12 on 10 phase 2
    obelisk_sim.statement.decl 20 in 1 scope 0 type 3 parent 10 loc("test.sv":2:1)
    obelisk_sim.statement_site.decl 21 on 20 phase 0
    obelisk_sim.vpi_statement_relation.decl code_unit 1 type 24 selector 104 ordinal 0 modes 1 to 10
    obelisk_sim.vpi_statement_relation.decl statement 10 type 15 selector 75 ordinal 0 modes 1 to 20
    // expected-error @below {{duplicates the target statement's semantic containment edge; merge access modes into one record}}
    obelisk_sim.vpi_statement_relation.decl statement 10 type 15 selector 75 ordinal 0 modes 2 to 20
  }
}

// -----

module {
  obelisk_sim.design @overlapping_ordinal {
    obelisk_sim.scope.decl 0
    obelisk_sim.code_unit.decl 1 in 0 initial hierarchy "top.initial"
    obelisk_sim.statement.decl 10 in 1 scope 0 type 4 loc("test.sv":1:1)
    obelisk_sim.statement_site.decl 11 on 10 phase 0
    obelisk_sim.statement.decl 20 in 1 scope 0 type 3 parent 10 loc("test.sv":2:1)
    obelisk_sim.statement_site.decl 21 on 20 phase 0
    obelisk_sim.statement.decl 30 in 1 scope 0 type 3 parent 10 loc("test.sv":3:1)
    obelisk_sim.statement_site.decl 31 on 30 phase 0
    obelisk_sim.vpi_statement_relation.decl code_unit 1 type 24 selector 104 ordinal 0 modes 1 to 10
    obelisk_sim.vpi_statement_relation.decl statement 10 type 4 selector 104 ordinal 0 modes 2 to 20
    // expected-error @below {{overlaps another target at the same source, selector, mode, and ordinal}}
    obelisk_sim.vpi_statement_relation.decl statement 10 type 4 selector 104 ordinal 0 modes 2 to 30
  }
}

// -----

module {
  obelisk_sim.design @nondense_ordinals {
    obelisk_sim.scope.decl 0
    obelisk_sim.code_unit.decl 1 in 0 initial hierarchy "top.initial"
    obelisk_sim.statement.decl 10 in 1 scope 0 type 4 loc("test.sv":1:1)
    obelisk_sim.statement_site.decl 11 on 10 phase 0
    obelisk_sim.statement.decl 20 in 1 scope 0 type 3 parent 10 loc("test.sv":2:1)
    obelisk_sim.statement_site.decl 21 on 20 phase 0
    obelisk_sim.statement.decl 30 in 1 scope 0 type 3 parent 10 loc("test.sv":3:1)
    obelisk_sim.statement_site.decl 31 on 30 phase 0
    obelisk_sim.vpi_statement_relation.decl code_unit 1 type 24 selector 104 ordinal 0 modes 1 to 10
    obelisk_sim.vpi_statement_relation.decl statement 10 type 4 selector 104 ordinal 0 modes 2 to 20
    // expected-error @below {{ordinals must be dense from zero for each source, selector, and access mode}}
    obelisk_sim.vpi_statement_relation.decl statement 10 type 4 selector 104 ordinal 2 modes 2 to 30
  }
}

// -----

module {
  obelisk_sim.design @too_many_handle_targets {
    obelisk_sim.scope.decl 0
    obelisk_sim.code_unit.decl 1 in 0 initial hierarchy "top.initial"
    obelisk_sim.statement.decl 10 in 1 scope 0 type 15 loc("test.sv":1:1)
    obelisk_sim.statement_site.decl 11 on 10 phase 1
    obelisk_sim.statement_site.decl 12 on 10 phase 2
    obelisk_sim.statement.decl 20 in 1 scope 0 type 3 parent 10 loc("test.sv":2:1)
    obelisk_sim.statement_site.decl 21 on 20 phase 0
    obelisk_sim.statement.decl 30 in 1 scope 0 type 3 parent 10 loc("test.sv":3:1)
    obelisk_sim.statement_site.decl 31 on 30 phase 0
    obelisk_sim.vpi_statement_relation.decl code_unit 1 type 24 selector 104 ordinal 0 modes 1 to 10
    obelisk_sim.vpi_statement_relation.decl statement 10 type 15 selector 75 ordinal 0 modes 1 to 20
    // expected-error @below {{vpi_handle relation may expose at most one target}}
    obelisk_sim.vpi_statement_relation.decl statement 10 type 15 selector 75 ordinal 1 modes 1 to 30
  }
}

// -----

module {
  obelisk_sim.design @missing_relation {
    obelisk_sim.scope.decl 0
    obelisk_sim.code_unit.decl 1 in 0 initial hierarchy "top.initial"
    obelisk_sim.statement.decl 10 in 1 scope 0 type 4 loc("test.sv":1:1)
    obelisk_sim.statement_site.decl 11 on 10 phase 0
    // expected-error @below {{is missing its semantic VPI statement-containment relation}}
    obelisk_sim.statement.decl 20 in 1 scope 0 type 3 parent 10
    obelisk_sim.statement_site.decl 21 on 20 phase 0
    obelisk_sim.vpi_statement_relation.decl code_unit 1 type 24 selector 104 ordinal 0 modes 1 to 10
  }
}
