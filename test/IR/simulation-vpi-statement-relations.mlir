// RUN: obelisk-opt %s | FileCheck %s

module {
  simulation.design @statement_relations {
    // Relation verification is independent of mutable block order.
    simulation.vpi_statement_relation.decl scope 0 type 32 selector 8 ordinal 0 modes 2 to 50
    simulation.scope.decl 0 hierarchy "top" vpi_kind 32
    simulation.code_unit.decl 1 in 0 initial hierarchy "top.initial"

    simulation.statement.decl 50 scope 0 type 8 loc("test.sv":1:1)
    simulation.statement.decl 51 scope 0 type 128 parent 50 loc("test.sv":1:1)
    simulation.statement.decl 100 in 1 scope 0 type 4 loc("test.sv":2:1)
    simulation.statement.decl 110 in 1 scope 0 type 3 parent 100 loc("test.sv":3:3)
    simulation.statement.decl 120 in 1 scope 0 type 15 parent 100 loc("test.sv":4:3)
    simulation.statement.decl 121 in 1 scope 0 type 3 parent 120 loc("test.sv":4:8)
    simulation.statement.decl 122 in 1 scope 0 type 3 parent 120 loc("test.sv":4:12)
    simulation.statement.decl 130 in 1 scope 0 type 675 parent 100 {is_scope} loc("test.sv":5:3)

    simulation.statement_site.decl 1000 on 100 phase 0
    simulation.statement_site.decl 1100 on 110 phase 0
    simulation.statement_site.decl 1200 on 120 phase 1
    simulation.statement_site.decl 1201 on 120 phase 2
    simulation.statement_site.decl 1210 on 121 phase 0
    simulation.statement_site.decl 1220 on 122 phase 0

    // vpiContAssign -> vpiContAssignBit via vpi_iterate.
    simulation.vpi_statement_relation.decl statement 50 type 8 selector 90 ordinal 0 modes 2 to 51
    // vpiInitial -> vpiStmt via vpi_handle.
    simulation.vpi_statement_relation.decl code_unit 1 type 24 selector 104 ordinal 0 modes 1 to 100
    // A block's statement iterator has a dense implementation ordinal even
    // though the LRM does not promise an API-visible order.
    simulation.vpi_statement_relation.decl statement 100 type 4 selector 104 ordinal 0 modes 2 to 110
    simulation.vpi_statement_relation.decl statement 100 type 4 selector 104 ordinal 1 modes 2 to 120
    simulation.vpi_statement_relation.decl statement 100 type 4 selector 104 ordinal 2 modes 2 to 130
    // Singular and iterative for-init access name the same semantic child.
    simulation.vpi_statement_relation.decl statement 120 type 15 selector 75 ordinal 0 modes 3 to 121
    // Later list entries are iterative only.
    simulation.vpi_statement_relation.decl statement 120 type 15 selector 75 ordinal 1 modes 2 to 122
  }

  // The relation inventory is transitional: legacy producers remain valid
  // only when they emit no relation records at all.
  simulation.design @legacy_without_relations {
    simulation.scope.decl 0 hierarchy "legacy"
    simulation.code_unit.decl 1 in 0 initial hierarchy "legacy.initial"
    simulation.statement.decl 1 in 1 scope 0 type 3 loc("legacy.sv":1:1)
    simulation.statement_site.decl 2 on 1 phase 0
  }

  // Generate scopes have immutable reflection identity without becoming
  // executable simulator scopes. Their statements therefore use the anchor
  // inventory ID as the exact forward-containment source.
  simulation.design @anchor_source {
    simulation.scope.decl 0 hierarchy "top" vpi_kind 32
    simulation.vpi_object.anchor @top id 0 type 32 in 0 ordinal 0
        hierarchy "top" debug "top" {backing = #simulation.vpi_backing<kind = scope, id = 0 : i64>}
    simulation.vpi_object.anchor @generated id 1 type 134 in 0 parent @top
        ordinal 0 hierarchy "top.g" debug "g"
    simulation.statement.decl 1 scope 0 type 8
    simulation.vpi_statement_relation.decl anchor 1 type 134 selector 8
        ordinal 0 modes 2 to 1
  }
}

// CHECK-LABEL: simulation.design @statement_relations
// CHECK: simulation.vpi_statement_relation.decl scope 0 type 32 selector 8 ordinal 0 modes 2 to 50
// CHECK: simulation.vpi_statement_relation.decl statement 50 type 8 selector 90 ordinal 0 modes 2 to 51
// CHECK: simulation.vpi_statement_relation.decl code_unit 1 type 24 selector 104 ordinal 0 modes 1 to 100
// CHECK: simulation.vpi_statement_relation.decl statement 100 type 4 selector 104 ordinal 0 modes 2 to 110
// CHECK: simulation.vpi_statement_relation.decl statement 100 type 4 selector 104 ordinal 1 modes 2 to 120
// CHECK: simulation.vpi_statement_relation.decl statement 100 type 4 selector 104 ordinal 2 modes 2 to 130
// CHECK: simulation.vpi_statement_relation.decl statement 120 type 15 selector 75 ordinal 0 modes 3 to 121
// CHECK: simulation.vpi_statement_relation.decl statement 120 type 15 selector 75 ordinal 1 modes 2 to 122
// CHECK-LABEL: simulation.design @legacy_without_relations
// CHECK-LABEL: simulation.design @anchor_source
// CHECK: simulation.vpi_statement_relation.decl anchor 1 type 134 selector 8 ordinal 0 modes 2 to 1
