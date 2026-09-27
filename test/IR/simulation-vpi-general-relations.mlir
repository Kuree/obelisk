// RUN: obelisk-opt %s | FileCheck %s

module {
  simulation.design @general_relations {
    simulation.scope.decl 0 hierarchy "top" vpi_kind 32
    simulation.statement.decl 1 scope 0 type 8 loc("relation.sv":1:1)
    simulation.statement.decl 2 scope 0 type 8 loc("relation.sv":2:1)
    simulation.vpi_statement_relation.decl scope 0 type 32 selector 8
        ordinal 0 modes 2 to 1
    simulation.vpi_statement_relation.decl scope 0 type 32 selector 8
        ordinal 1 modes 2 to 2
    simulation.net.decl 0 in 0 : !simulation.logic<1> design
        hierarchy "top.lhs" debug "lhs" {
      vpi_type = #simulation.vpi_type<kind = logic, isSigned = false,
          isFourState = true, range = [0, 0], children = [], childNames = []>
    }
    simulation.net.decl 1 in 0 : !simulation.logic<1> design
        hierarchy "top.rhs" debug "rhs" {
      vpi_type = #simulation.vpi_type<kind = logic, isSigned = false,
          isFourState = true, range = [0, 0], children = [], childNames = []>
    }
    simulation.vpi_net_identity.decl 0 backed_by 0 in 0
        : !simulation.logic<1> hierarchy "top.alias" debug "alias" {
      vpi_type = #simulation.vpi_type<kind = logic, isSigned = false,
          isFourState = true, range = [0, 0], children = [], childNames = []>
    }
    simulation.storage.decl 0 in 0 : !simulation.logic<1> design
        hierarchy "top.variable" debug "variable"
    simulation.vpi_relation.decl <kind = statement, id = 1 : i64>
        selector 77 handle ordinal 0 to <kind = net, id = 0 : i64>
    simulation.vpi_relation.decl <kind = statement, id = 1 : i64>
        selector 82 handle ordinal 0 to <kind = net, id = 1 : i64>
    simulation.vpi_relation.decl <kind = net, id = 0 : i64>
        selector 91 iterate ordinal 0 to <kind = statement, id = 1 : i64>
    simulation.vpi_relation.decl <kind = net, id = 0 : i64>
        selector 101 iterate ordinal 0 to <kind = statement, id = 1 : i64>
    simulation.vpi_relation.decl <kind = net, id = 1 : i64>
        selector 93 iterate ordinal 0 to <kind = statement, id = 1 : i64>
    simulation.vpi_relation.decl <kind = statement, id = 2 : i64>
        selector 82 handle ordinal 0 to <kind = storage, id = 0 : i64>
    simulation.vpi_relation.decl <kind = storage, id = 0 : i64>
        selector 93 iterate ordinal 0 to <kind = statement, id = 2 : i64>
    simulation.vpi_relation.decl <kind = storage, id = 0 : i64>
        selector 101 iterate ordinal 0 to <kind = statement, id = 2 : i64>
    simulation.vpi_relation.decl <kind = net_identity, id = 0 : i64>
        selector 126 handle ordinal 0 to <kind = net, id = 0 : i64>
  }
}

// CHECK-LABEL: simulation.design @general_relations
// CHECK: simulation.vpi_relation.decl <kind = statement, id = 1 : i64> selector 77 handle ordinal 0 to <kind = net, id = 0 : i64>
// CHECK: simulation.vpi_relation.decl <kind = statement, id = 1 : i64> selector 82 handle ordinal 0 to <kind = net, id = 1 : i64>
// CHECK: simulation.vpi_relation.decl <kind = net, id = 0 : i64> selector 91 iterate ordinal 0 to <kind = statement, id = 1 : i64>
// CHECK: simulation.vpi_relation.decl <kind = net, id = 0 : i64> selector 101 iterate ordinal 0 to <kind = statement, id = 1 : i64>
// CHECK: simulation.vpi_relation.decl <kind = net, id = 1 : i64> selector 93 iterate ordinal 0 to <kind = statement, id = 1 : i64>
// CHECK: simulation.vpi_relation.decl <kind = statement, id = 2 : i64> selector 82 handle ordinal 0 to <kind = storage, id = 0 : i64>
// CHECK: simulation.vpi_relation.decl <kind = storage, id = 0 : i64> selector 93 iterate ordinal 0 to <kind = statement, id = 2 : i64>
// CHECK: simulation.vpi_relation.decl <kind = storage, id = 0 : i64> selector 101 iterate ordinal 0 to <kind = statement, id = 2 : i64>
// CHECK: simulation.vpi_relation.decl <kind = net_identity, id = 0 : i64> selector 126 handle ordinal 0 to <kind = net, id = 0 : i64>
