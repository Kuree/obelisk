// RUN: obelisk-opt --split-input-file --verify-diagnostics %s

module {
  simulation.design @empty_mode_mask {
    // expected-error @below {{mode mask must contain only vpi_handle and/or vpi_iterate}}
    simulation.vpi_statement_relation.decl scope 0 type 32 selector 8 ordinal 0 modes 0 to 1
  }
}

// -----

module {
  simulation.design @unknown_anchor_source {
    simulation.scope.decl 0 hierarchy "top" vpi_kind 32
    simulation.statement.decl 1 scope 0 type 8
    // expected-error @below {{references an unknown source VPI anchor inventory ID}}
    simulation.vpi_statement_relation.decl anchor 99 type 134 selector 8 ordinal 0 modes 2 to 1
  }
}

// -----

module {
  simulation.design @anchor_source_kind_mismatch {
    simulation.scope.decl 0 hierarchy "top" vpi_kind 32
    simulation.vpi_object.anchor @top id 0 type 32 in 0 ordinal 0
        hierarchy "top" debug "top" {backing = #simulation.vpi_backing<kind = scope, id = 0 : i64>}
    simulation.vpi_object.anchor @generated id 1 type 134 in 0 parent @top
        ordinal 0 hierarchy "top.g" debug "g"
    simulation.statement.decl 1 scope 0 type 8
    // expected-error @below {{source VPI kind does not match the anchor declaration}}
    simulation.vpi_statement_relation.decl anchor 1 type 32 selector 8 ordinal 0 modes 2 to 1
  }
}

// -----

module {
  simulation.design @backed_anchor_source {
    simulation.scope.decl 0 hierarchy "top" vpi_kind 32
    simulation.vpi_object.anchor @top id 0 type 32 in 0 ordinal 0
        hierarchy "top" debug "top" {backing = #simulation.vpi_backing<kind = scope, id = 0 : i64>}
    simulation.statement.decl 1 scope 0 type 8
    // expected-error @below {{backed anchor source must use its canonical scope or code-unit ID}}
    simulation.vpi_statement_relation.decl anchor 0 type 32 selector 8 ordinal 0 modes 2 to 1
  }
}

// -----

module {
  simulation.design @anchor_source_scope_mismatch {
    simulation.scope.decl 0 hierarchy "top" vpi_kind 32
    simulation.scope.decl 1 parent 0 hierarchy "top.child" vpi_kind 32
    simulation.vpi_object.anchor @top id 0 type 32 in 0 ordinal 0
        hierarchy "top" debug "top" {backing = #simulation.vpi_backing<kind = scope, id = 0 : i64>}
    simulation.vpi_object.anchor @generated id 1 type 134 in 0 parent @top
        ordinal 0 hierarchy "top.g" debug "g"
    simulation.statement.decl 1 scope 1 type 8
    // expected-error @below {{anchor source does not own the root scope-owned statement}}
    simulation.vpi_statement_relation.decl anchor 1 type 134 selector 8 ordinal 0 modes 2 to 1
  }
}

// -----

module {
  simulation.design @reserved_mode_bit {
    // expected-error @below {{mode mask must contain only vpi_handle and/or vpi_iterate}}
    simulation.vpi_statement_relation.decl scope 0 type 32 selector 8 ordinal 0 modes 4 to 1
  }
}

// -----

module {
  simulation.design @wide_ordinal {
    // expected-error @below {{ordinal exceeds the reflection encoding}}
    simulation.vpi_statement_relation.decl scope 0 type 32 selector 8 ordinal 4294967296 modes 2 to 1
  }
}

// -----

module {
  simulation.design @non_object_source_kind {
    // expected-error @below {{source VPI kind is not a concrete object}}
    simulation.vpi_statement_relation.decl scope 0 type 104 selector 8 ordinal 0 modes 2 to 1
  }
}

// -----

module {
  simulation.design @unknown_scope_source {
    simulation.scope.decl 0
    simulation.statement.decl 1 scope 0 type 8 loc("test.sv":1:1)
    // expected-error @below {{references an unknown source scope ID}}
    simulation.vpi_statement_relation.decl scope 99 type 32 selector 8 ordinal 0 modes 2 to 1
  }
}

// -----

module {
  simulation.design @unknown_target {
    simulation.scope.decl 0
    // expected-error @below {{references an unknown target statement ID}}
    simulation.vpi_statement_relation.decl scope 0 type 32 selector 8 ordinal 0 modes 2 to 99
  }
}

// -----

module {
  simulation.design @scope_kind_is_not_scope {
    simulation.scope.decl 0
    simulation.statement.decl 10 scope 0 type 8 loc("test.sv":1:1)
    // expected-error @below {{scope source VPI kind is not a scope object}}
    simulation.vpi_statement_relation.decl scope 0 type 24 selector 104 ordinal 0 modes 1 to 10
  }
}

// -----

module {
  simulation.design @inconsistent_scope_kinds {
    simulation.scope.decl 0 vpi_kind 32
    simulation.statement.decl 10 scope 0 type 8 loc("test.sv":1:1)
    simulation.statement.decl 20 scope 0 type 646 loc("test.sv":2:1)
    simulation.vpi_statement_relation.decl scope 0 type 32 selector 8 ordinal 0 modes 2 to 10
    // expected-error @below {{source VPI kind does not match the scope declaration}}
    simulation.vpi_statement_relation.decl scope 0 type 134 selector 646 ordinal 0 modes 2 to 20
  }
}

// -----

module {
  simulation.design @interface_scope_claims_module_kind {
    simulation.scope.decl 0
    simulation.scope.decl 1 parent 0 interface "I"
    simulation.statement.decl 10 scope 1 type 8 loc("test.sv":1:1)
    // expected-error @below {{interface scope metadata and source VPI kind disagree}}
    simulation.vpi_statement_relation.decl scope 1 type 32 selector 8 ordinal 0 modes 2 to 10
  }
}

// -----

module {
  simulation.design @plain_scope_claims_interface_kind {
    simulation.scope.decl 0
    simulation.statement.decl 10 scope 0 type 8 loc("test.sv":1:1)
    // expected-error @below {{source VPI kind does not match the scope declaration}}
    simulation.vpi_statement_relation.decl scope 0 type 601 selector 8 ordinal 0 modes 2 to 10
  }
}

// -----

module {
  simulation.design @unknown_code_unit_source {
    simulation.scope.decl 0
    simulation.statement.decl 10 in 99 scope 0 type 4 loc("test.sv":1:1)
    simulation.statement_site.decl 11 on 10 phase 0
    // expected-error @below {{references an unknown source code-unit ID}}
    simulation.vpi_statement_relation.decl code_unit 98 type 24 selector 104 ordinal 0 modes 1 to 10
  }
}

// -----

module {
  simulation.design @unknown_statement_source {
    simulation.scope.decl 0
    simulation.code_unit.decl 1 in 0 initial hierarchy "top.initial"
    simulation.statement.decl 10 in 1 scope 0 type 4 loc("test.sv":1:1)
    simulation.statement_site.decl 11 on 10 phase 0
    simulation.statement.decl 20 in 1 scope 0 type 3 parent 10 loc("test.sv":2:1)
    simulation.statement_site.decl 21 on 20 phase 0
    simulation.vpi_statement_relation.decl code_unit 1 type 24 selector 104 ordinal 0 modes 1 to 10
    // expected-error @below {{references an unknown source statement ID}}
    simulation.vpi_statement_relation.decl statement 99 type 4 selector 104 ordinal 0 modes 2 to 20
  }
}

// -----

module {
  simulation.design @wrong_code_unit_vpi_kind {
    simulation.scope.decl 0
    simulation.code_unit.decl 1 in 0 initial hierarchy "top.initial"
    simulation.statement.decl 10 in 1 scope 0 type 4 loc("test.sv":1:1)
    simulation.statement_site.decl 11 on 10 phase 0
    // expected-error @below {{source VPI kind does not match the code-unit kind}}
    simulation.vpi_statement_relation.decl code_unit 1 type 1 selector 104 ordinal 0 modes 1 to 10
  }
}

// -----

module {
  simulation.design @wrong_statement_vpi_kind {
    simulation.scope.decl 0
    simulation.code_unit.decl 1 in 0 initial hierarchy "top.initial"
    simulation.statement.decl 10 in 1 scope 0 type 4 loc("test.sv":1:1)
    simulation.statement_site.decl 11 on 10 phase 0
    simulation.statement.decl 20 in 1 scope 0 type 3 parent 10 loc("test.sv":2:1)
    simulation.statement_site.decl 21 on 20 phase 0
    simulation.vpi_statement_relation.decl code_unit 1 type 24 selector 104 ordinal 0 modes 1 to 10
    // expected-error @below {{source VPI kind does not match the statement declaration}}
    simulation.vpi_statement_relation.decl statement 10 type 15 selector 104 ordinal 0 modes 1 to 20
  }
}

// -----

module {
  simulation.design @cross_parent_relation {
    simulation.scope.decl 0
    simulation.code_unit.decl 1 in 0 initial hierarchy "top.initial"
    simulation.statement.decl 10 in 1 scope 0 type 4 loc("test.sv":1:1)
    simulation.statement_site.decl 11 on 10 phase 0
    simulation.statement.decl 20 in 1 scope 0 type 3 parent 10 loc("test.sv":2:1)
    simulation.statement_site.decl 21 on 20 phase 0
    simulation.vpi_statement_relation.decl code_unit 1 type 24 selector 104 ordinal 0 modes 1 to 10
    // expected-error @below {{statement source does not match the target's structural parent}}
    simulation.vpi_statement_relation.decl statement 20 type 3 selector 104 ordinal 0 modes 1 to 20
  }
}

// -----

module {
  simulation.design @not_containment {
    simulation.scope.decl 0
    simulation.code_unit.decl 1 in 0 initial hierarchy "top.initial"
    simulation.statement.decl 10 in 1 scope 0 type 3 loc("test.sv":1:1)
    simulation.statement_site.decl 11 on 10 phase 0
    simulation.statement.decl 20 in 1 scope 0 type 3 parent 10 loc("test.sv":2:1)
    simulation.statement_site.decl 21 on 20 phase 0
    simulation.vpi_statement_relation.decl code_unit 1 type 24 selector 104 ordinal 0 modes 1 to 10
    // expected-error @below {{is not a legal statement-containment traversal in the generated VPI model}}
    simulation.vpi_statement_relation.decl statement 10 type 3 selector 104 ordinal 0 modes 1 to 20
  }
}

// -----

module {
  simulation.design @target_outside_generated_set {
    simulation.scope.decl 0
    simulation.code_unit.decl 1 in 0 initial hierarchy "top.initial"
    simulation.statement.decl 10 in 1 scope 0 type 5 loc("test.sv":1:1)
    simulation.statement_site.decl 11 on 10 phase 0
    simulation.statement.decl 20 in 1 scope 0 type 3 parent 10 loc("test.sv":2:1)
    simulation.statement_site.decl 21 on 20 phase 0
    simulation.vpi_statement_relation.decl code_unit 1 type 24 selector 104 ordinal 0 modes 1 to 10
    // expected-error @below {{is not a legal statement-containment traversal in the generated VPI model}}
    simulation.vpi_statement_relation.decl statement 10 type 5 selector 6 ordinal 0 modes 2 to 20
  }
}

// -----

module {
  simulation.design @mode_not_supported_by_relation {
    simulation.scope.decl 0
    simulation.code_unit.decl 1 in 0 initial hierarchy "top.initial"
    simulation.statement.decl 10 in 1 scope 0 type 4 loc("test.sv":1:1)
    simulation.statement_site.decl 11 on 10 phase 0
    simulation.statement.decl 20 in 1 scope 0 type 3 parent 10 loc("test.sv":2:1)
    simulation.statement_site.decl 21 on 20 phase 0
    simulation.vpi_statement_relation.decl code_unit 1 type 24 selector 104 ordinal 0 modes 1 to 10
    // expected-error @below {{is not a legal statement-containment traversal in the generated VPI model}}
    simulation.vpi_statement_relation.decl statement 10 type 4 selector 104 ordinal 0 modes 3 to 20
  }
}

// -----

module {
  simulation.design @dual_mode_handle_only {
    simulation.scope.decl 0
    simulation.code_unit.decl 1 in 0 initial hierarchy "top.initial"
    simulation.statement.decl 10 in 1 scope 0 type 15 loc("test.sv":1:1)
    simulation.statement_site.decl 11 on 10 phase 1
    simulation.statement_site.decl 12 on 10 phase 2
    simulation.statement.decl 20 in 1 scope 0 type 3 parent 10 loc("test.sv":2:1)
    simulation.statement_site.decl 21 on 20 phase 0
    simulation.vpi_statement_relation.decl code_unit 1 type 24 selector 104 ordinal 0 modes 1 to 10
    // expected-error @below {{ordinal zero must merge vpi_handle and vpi_iterate for a dual-mode statement relation}}
    simulation.vpi_statement_relation.decl statement 10 type 15 selector 75 ordinal 0 modes 1 to 20
  }
}

// -----

module {
  simulation.design @dual_mode_iterate_only {
    simulation.scope.decl 0
    simulation.code_unit.decl 1 in 0 initial hierarchy "top.initial"
    simulation.statement.decl 10 in 1 scope 0 type 15 loc("test.sv":1:1)
    simulation.statement_site.decl 11 on 10 phase 1
    simulation.statement_site.decl 12 on 10 phase 2
    simulation.statement.decl 20 in 1 scope 0 type 3 parent 10 loc("test.sv":2:1)
    simulation.statement_site.decl 21 on 20 phase 0
    simulation.vpi_statement_relation.decl code_unit 1 type 24 selector 104 ordinal 0 modes 1 to 10
    // expected-error @below {{ordinal zero must merge vpi_handle and vpi_iterate for a dual-mode statement relation}}
    simulation.vpi_statement_relation.decl statement 10 type 15 selector 75 ordinal 0 modes 2 to 20
  }
}

// -----

module {
  simulation.design @dual_mode_split_targets {
    simulation.scope.decl 0
    simulation.code_unit.decl 1 in 0 initial hierarchy "top.initial"
    simulation.statement.decl 10 in 1 scope 0 type 15 loc("test.sv":1:1)
    simulation.statement_site.decl 11 on 10 phase 1
    simulation.statement_site.decl 12 on 10 phase 2
    simulation.statement.decl 20 in 1 scope 0 type 3 parent 10 loc("test.sv":2:1)
    simulation.statement_site.decl 21 on 20 phase 0
    simulation.statement.decl 30 in 1 scope 0 type 3 parent 10 loc("test.sv":3:1)
    simulation.statement_site.decl 31 on 30 phase 0
    simulation.vpi_statement_relation.decl code_unit 1 type 24 selector 104 ordinal 0 modes 1 to 10
    // expected-error @below {{ordinal zero must merge vpi_handle and vpi_iterate for a dual-mode statement relation}}
    simulation.vpi_statement_relation.decl statement 10 type 15 selector 75 ordinal 0 modes 1 to 20
    simulation.vpi_statement_relation.decl statement 10 type 15 selector 75 ordinal 0 modes 2 to 30
  }
}

// -----

module {
  simulation.design @duplicate_target_modes_not_merged {
    simulation.scope.decl 0
    simulation.code_unit.decl 1 in 0 initial hierarchy "top.initial"
    simulation.statement.decl 10 in 1 scope 0 type 15 loc("test.sv":1:1)
    simulation.statement_site.decl 11 on 10 phase 1
    simulation.statement_site.decl 12 on 10 phase 2
    simulation.statement.decl 20 in 1 scope 0 type 3 parent 10 loc("test.sv":2:1)
    simulation.statement_site.decl 21 on 20 phase 0
    simulation.vpi_statement_relation.decl code_unit 1 type 24 selector 104 ordinal 0 modes 1 to 10
    simulation.vpi_statement_relation.decl statement 10 type 15 selector 75 ordinal 0 modes 1 to 20
    // expected-error @below {{duplicates the target statement's semantic containment edge; merge access modes into one record}}
    simulation.vpi_statement_relation.decl statement 10 type 15 selector 75 ordinal 0 modes 2 to 20
  }
}

// -----

module {
  simulation.design @overlapping_ordinal {
    simulation.scope.decl 0
    simulation.code_unit.decl 1 in 0 initial hierarchy "top.initial"
    simulation.statement.decl 10 in 1 scope 0 type 4 loc("test.sv":1:1)
    simulation.statement_site.decl 11 on 10 phase 0
    simulation.statement.decl 20 in 1 scope 0 type 3 parent 10 loc("test.sv":2:1)
    simulation.statement_site.decl 21 on 20 phase 0
    simulation.statement.decl 30 in 1 scope 0 type 3 parent 10 loc("test.sv":3:1)
    simulation.statement_site.decl 31 on 30 phase 0
    simulation.vpi_statement_relation.decl code_unit 1 type 24 selector 104 ordinal 0 modes 1 to 10
    simulation.vpi_statement_relation.decl statement 10 type 4 selector 104 ordinal 0 modes 2 to 20
    // expected-error @below {{overlaps another target at the same source, selector, mode, and ordinal}}
    simulation.vpi_statement_relation.decl statement 10 type 4 selector 104 ordinal 0 modes 2 to 30
  }
}

// -----

module {
  simulation.design @nondense_ordinals {
    simulation.scope.decl 0
    simulation.code_unit.decl 1 in 0 initial hierarchy "top.initial"
    simulation.statement.decl 10 in 1 scope 0 type 4 loc("test.sv":1:1)
    simulation.statement_site.decl 11 on 10 phase 0
    simulation.statement.decl 20 in 1 scope 0 type 3 parent 10 loc("test.sv":2:1)
    simulation.statement_site.decl 21 on 20 phase 0
    simulation.statement.decl 30 in 1 scope 0 type 3 parent 10 loc("test.sv":3:1)
    simulation.statement_site.decl 31 on 30 phase 0
    simulation.vpi_statement_relation.decl code_unit 1 type 24 selector 104 ordinal 0 modes 1 to 10
    simulation.vpi_statement_relation.decl statement 10 type 4 selector 104 ordinal 0 modes 2 to 20
    // expected-error @below {{ordinals must be dense from zero for each source, selector, and access mode}}
    simulation.vpi_statement_relation.decl statement 10 type 4 selector 104 ordinal 2 modes 2 to 30
  }
}

// -----

module {
  simulation.design @too_many_handle_targets {
    simulation.scope.decl 0
    simulation.code_unit.decl 1 in 0 initial hierarchy "top.initial"
    simulation.statement.decl 10 in 1 scope 0 type 15 loc("test.sv":1:1)
    simulation.statement_site.decl 11 on 10 phase 1
    simulation.statement_site.decl 12 on 10 phase 2
    simulation.statement.decl 20 in 1 scope 0 type 3 parent 10 loc("test.sv":2:1)
    simulation.statement_site.decl 21 on 20 phase 0
    simulation.statement.decl 30 in 1 scope 0 type 3 parent 10 loc("test.sv":3:1)
    simulation.statement_site.decl 31 on 30 phase 0
    simulation.vpi_statement_relation.decl code_unit 1 type 24 selector 104 ordinal 0 modes 1 to 10
    simulation.vpi_statement_relation.decl statement 10 type 15 selector 75 ordinal 0 modes 1 to 20
    // expected-error @below {{vpi_handle relation may expose at most one target}}
    simulation.vpi_statement_relation.decl statement 10 type 15 selector 75 ordinal 1 modes 1 to 30
  }
}

// -----

module {
  simulation.design @missing_relation {
    simulation.scope.decl 0
    simulation.code_unit.decl 1 in 0 initial hierarchy "top.initial"
    simulation.statement.decl 10 in 1 scope 0 type 4 loc("test.sv":1:1)
    simulation.statement_site.decl 11 on 10 phase 0
    // expected-error @below {{is missing its semantic VPI statement-containment relation}}
    simulation.statement.decl 20 in 1 scope 0 type 3 parent 10
    simulation.statement_site.decl 21 on 20 phase 0
    simulation.vpi_statement_relation.decl code_unit 1 type 24 selector 104 ordinal 0 modes 1 to 10
  }
}

// -----

module {
  simulation.design @intrinsic_scope_kind_is_not_concrete {
    // expected-error @below {{VPI kind is not a concrete scope object}}
    simulation.scope.decl 0 vpi_kind 99
  }
}

// -----

module {
  simulation.design @intrinsic_scope_kind_is_not_scope {
    // expected-error @below {{VPI kind is not a concrete scope object}}
    simulation.scope.decl 0 vpi_kind 24
  }
}

// -----

module {
  simulation.design @intrinsic_interface_metadata_with_module_kind {
    simulation.scope.decl 0
    // expected-error @below {{interface scope metadata and intrinsic VPI kind disagree}}
    simulation.scope.decl 1 parent 0 interface "I" vpi_kind 32
  }
}
