// RUN: obelisk-opt %s | FileCheck %s

module {
  obelisk_sim.design @general_relations {
    obelisk_sim.scope.decl 0 hierarchy "top" vpi_kind 32
    obelisk_sim.statement.decl 1 scope 0 type 8 loc("relation.sv":1:1)
    obelisk_sim.statement.decl 2 scope 0 type 8 loc("relation.sv":2:1)
    obelisk_sim.vpi_statement_relation.decl scope 0 type 32 selector 8
        ordinal 0 modes 2 to 1
    obelisk_sim.vpi_statement_relation.decl scope 0 type 32 selector 8
        ordinal 1 modes 2 to 2
    obelisk_sim.net.decl 0 in 0 : !obelisk_sim.logic<1> design
        hierarchy "top.lhs" debug "lhs" {
      vpi_type = #obelisk_sim.vpi_type<kind = logic, isSigned = false,
          isFourState = true, range = [0, 0], children = [], childNames = []>
    }
    obelisk_sim.net.decl 1 in 0 : !obelisk_sim.logic<1> design
        hierarchy "top.rhs" debug "rhs" {
      vpi_type = #obelisk_sim.vpi_type<kind = logic, isSigned = false,
          isFourState = true, range = [0, 0], children = [], childNames = []>
    }
    obelisk_sim.storage.decl 0 in 0 : !obelisk_sim.logic<1> design
        hierarchy "top.variable" debug "variable"
    obelisk_sim.vpi_relation.decl <kind = statement, id = 1 : i64>
        selector 77 handle ordinal 0 to <kind = net, id = 0 : i64>
    obelisk_sim.vpi_relation.decl <kind = statement, id = 1 : i64>
        selector 82 handle ordinal 0 to <kind = net, id = 1 : i64>
    obelisk_sim.vpi_relation.decl <kind = net, id = 0 : i64>
        selector 91 iterate ordinal 0 to <kind = statement, id = 1 : i64>
    obelisk_sim.vpi_relation.decl <kind = net, id = 0 : i64>
        selector 101 iterate ordinal 0 to <kind = statement, id = 1 : i64>
    obelisk_sim.vpi_relation.decl <kind = net, id = 1 : i64>
        selector 93 iterate ordinal 0 to <kind = statement, id = 1 : i64>
    obelisk_sim.vpi_relation.decl <kind = statement, id = 2 : i64>
        selector 82 handle ordinal 0 to <kind = storage, id = 0 : i64>
    obelisk_sim.vpi_relation.decl <kind = storage, id = 0 : i64>
        selector 93 iterate ordinal 0 to <kind = statement, id = 2 : i64>
    obelisk_sim.vpi_relation.decl <kind = storage, id = 0 : i64>
        selector 101 iterate ordinal 0 to <kind = statement, id = 2 : i64>
  }
}

// CHECK-LABEL: obelisk_sim.design @general_relations
// CHECK: obelisk_sim.vpi_relation.decl <kind = statement, id = 1 : i64> selector 77 handle ordinal 0 to <kind = net, id = 0 : i64>
// CHECK: obelisk_sim.vpi_relation.decl <kind = statement, id = 1 : i64> selector 82 handle ordinal 0 to <kind = net, id = 1 : i64>
// CHECK: obelisk_sim.vpi_relation.decl <kind = net, id = 0 : i64> selector 91 iterate ordinal 0 to <kind = statement, id = 1 : i64>
// CHECK: obelisk_sim.vpi_relation.decl <kind = net, id = 0 : i64> selector 101 iterate ordinal 0 to <kind = statement, id = 1 : i64>
// CHECK: obelisk_sim.vpi_relation.decl <kind = net, id = 1 : i64> selector 93 iterate ordinal 0 to <kind = statement, id = 1 : i64>
// CHECK: obelisk_sim.vpi_relation.decl <kind = statement, id = 2 : i64> selector 82 handle ordinal 0 to <kind = storage, id = 0 : i64>
// CHECK: obelisk_sim.vpi_relation.decl <kind = storage, id = 0 : i64> selector 93 iterate ordinal 0 to <kind = statement, id = 2 : i64>
// CHECK: obelisk_sim.vpi_relation.decl <kind = storage, id = 0 : i64> selector 101 iterate ordinal 0 to <kind = statement, id = 2 : i64>
