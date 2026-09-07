// RUN: obelisk-opt %s | FileCheck %s

module {
  obelisk_sim.design @statement_relations {
    // Relation verification is independent of mutable block order.
    obelisk_sim.vpi_statement_relation.decl scope 0 type 32 selector 8 ordinal 0 modes 2 to 50
    obelisk_sim.scope.decl 0 hierarchy "top" vpi_kind 32
    obelisk_sim.code_unit.decl 1 in 0 initial hierarchy "top.initial"

    obelisk_sim.statement.decl 50 scope 0 type 8 loc("test.sv":1:1)
    obelisk_sim.statement.decl 51 scope 0 type 128 parent 50 loc("test.sv":1:1)
    obelisk_sim.statement.decl 100 in 1 scope 0 type 4 loc("test.sv":2:1)
    obelisk_sim.statement.decl 110 in 1 scope 0 type 3 parent 100 loc("test.sv":3:3)
    obelisk_sim.statement.decl 120 in 1 scope 0 type 15 parent 100 loc("test.sv":4:3)
    obelisk_sim.statement.decl 121 in 1 scope 0 type 3 parent 120 loc("test.sv":4:8)
    obelisk_sim.statement.decl 122 in 1 scope 0 type 3 parent 120 loc("test.sv":4:12)
    obelisk_sim.statement.decl 130 in 1 scope 0 type 675 parent 100 {is_scope} loc("test.sv":5:3)

    obelisk_sim.statement_site.decl 1000 on 100 phase 0
    obelisk_sim.statement_site.decl 1100 on 110 phase 0
    obelisk_sim.statement_site.decl 1200 on 120 phase 1
    obelisk_sim.statement_site.decl 1201 on 120 phase 2
    obelisk_sim.statement_site.decl 1210 on 121 phase 0
    obelisk_sim.statement_site.decl 1220 on 122 phase 0

    // vpiContAssign -> vpiContAssignBit via vpi_iterate.
    obelisk_sim.vpi_statement_relation.decl statement 50 type 8 selector 90 ordinal 0 modes 2 to 51
    // vpiInitial -> vpiStmt via vpi_handle.
    obelisk_sim.vpi_statement_relation.decl code_unit 1 type 24 selector 104 ordinal 0 modes 1 to 100
    // A block's statement iterator has a dense implementation ordinal even
    // though the LRM does not promise an API-visible order.
    obelisk_sim.vpi_statement_relation.decl statement 100 type 4 selector 104 ordinal 0 modes 2 to 110
    obelisk_sim.vpi_statement_relation.decl statement 100 type 4 selector 104 ordinal 1 modes 2 to 120
    obelisk_sim.vpi_statement_relation.decl statement 100 type 4 selector 104 ordinal 2 modes 2 to 130
    // Singular and iterative for-init access name the same semantic child.
    obelisk_sim.vpi_statement_relation.decl statement 120 type 15 selector 75 ordinal 0 modes 3 to 121
    // Later list entries are iterative only.
    obelisk_sim.vpi_statement_relation.decl statement 120 type 15 selector 75 ordinal 1 modes 2 to 122
  }

  // The relation inventory is transitional: legacy producers remain valid
  // only when they emit no relation records at all.
  obelisk_sim.design @legacy_without_relations {
    obelisk_sim.scope.decl 0 hierarchy "legacy"
    obelisk_sim.code_unit.decl 1 in 0 initial hierarchy "legacy.initial"
    obelisk_sim.statement.decl 1 in 1 scope 0 type 3 loc("legacy.sv":1:1)
    obelisk_sim.statement_site.decl 2 on 1 phase 0
  }

  // Generate scopes have immutable reflection identity without becoming
  // executable simulator scopes. Their statements therefore use the anchor
  // inventory ID as the exact forward-containment source.
  obelisk_sim.design @anchor_source {
    obelisk_sim.scope.decl 0 hierarchy "top" vpi_kind 32
    obelisk_sim.vpi_object.anchor @top id 0 type 32 in 0 ordinal 0
        hierarchy "top" debug "top" {backing = #obelisk_sim.vpi_backing<kind = scope, id = 0 : i64>}
    obelisk_sim.vpi_object.anchor @generated id 1 type 134 in 0 parent @top
        ordinal 0 hierarchy "top.g" debug "g"
    obelisk_sim.statement.decl 1 scope 0 type 8
    obelisk_sim.vpi_statement_relation.decl anchor 1 type 134 selector 8
        ordinal 0 modes 2 to 1
  }
}

// CHECK-LABEL: obelisk_sim.design @statement_relations
// CHECK: obelisk_sim.vpi_statement_relation.decl scope 0 type 32 selector 8 ordinal 0 modes 2 to 50
// CHECK: obelisk_sim.vpi_statement_relation.decl statement 50 type 8 selector 90 ordinal 0 modes 2 to 51
// CHECK: obelisk_sim.vpi_statement_relation.decl code_unit 1 type 24 selector 104 ordinal 0 modes 1 to 100
// CHECK: obelisk_sim.vpi_statement_relation.decl statement 100 type 4 selector 104 ordinal 0 modes 2 to 110
// CHECK: obelisk_sim.vpi_statement_relation.decl statement 100 type 4 selector 104 ordinal 1 modes 2 to 120
// CHECK: obelisk_sim.vpi_statement_relation.decl statement 100 type 4 selector 104 ordinal 2 modes 2 to 130
// CHECK: obelisk_sim.vpi_statement_relation.decl statement 120 type 15 selector 75 ordinal 0 modes 3 to 121
// CHECK: obelisk_sim.vpi_statement_relation.decl statement 120 type 15 selector 75 ordinal 1 modes 2 to 122
// CHECK-LABEL: obelisk_sim.design @legacy_without_relations
// CHECK-LABEL: obelisk_sim.design @anchor_source
// CHECK: obelisk_sim.vpi_statement_relation.decl anchor 1 type 134 selector 8 ordinal 0 modes 2 to 1
