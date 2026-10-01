// RUN: obelisk-opt %s --obelisk-sim-prepare | FileCheck %s
// RUN: obelisk-opt %s --obelisk-sim-prepare --mlir-disable-threading | FileCheck %s
// Repeated code units share an instance classification, but different
// instances must not share the cached answer. Frontend definition identities
// need the name fallback when nearest-symbol lookup stops at isolated scopes.
// CHECK: simulation.func private @unit_0
// CHECK-SAME: domain = 1 : i32
// CHECK-SAME: home_region = 10 : i32
// CHECK: simulation.func private @unit_1
// CHECK-SAME: domain = 1 : i32
// CHECK-SAME: home_region = 10 : i32
// CHECK: simulation.func private @unit_2
// CHECK-SAME: domain = 0 : i32
// CHECK-SAME: home_region = 2 : i32
// CHECK: simulation.func private @unit_3
// CHECK-SAME: domain = 0 : i32
// CHECK-SAME: home_region = 2 : i32
// CHECK: simulation.func private @unit_4
// CHECK-SAME: domain = 1 : i32
// CHECK-SAME: home_region = 10 : i32

module {
  obelisk.sv.symbol.definition @program_def attributes {definition_kind = 2 : i32, hierarchical_name = "program_def", name = "program_def", node_id = 0 : i64} {}
  obelisk.sv.symbol.definition @module_def attributes {definition_kind = 0 : i32, hierarchical_name = "module_def", name = "module_def", node_id = 1 : i64} {}
  obelisk.sv.symbol.root @root attributes {node_id = 2 : i64} {
    obelisk.sv.symbol.instance @p attributes {hierarchical_name = "p", is_uninstantiated = false, name = "p", node_id = 3 : i64, referenced_path = "program_def", referenced_symbol = @program_def} {
      obelisk.sv.symbol.instance_body @p_body attributes {hierarchical_name = "p", name = "p", node_id = 4 : i64} {
        obelisk.sv.symbol.procedural_block @p_first attributes {hierarchical_name = "p.first", node_id = 5 : i64, procedure_kind = 0 : i32} {
          obelisk.sv.statement.empty attributes {node_id = 6 : i64} {}
        }
        obelisk.sv.symbol.procedural_block @p_second attributes {hierarchical_name = "p.second", node_id = 7 : i64, procedure_kind = 0 : i32} {
          obelisk.sv.statement.empty attributes {node_id = 8 : i64} {}
        }
      }
    }
    obelisk.sv.symbol.instance @m attributes {hierarchical_name = "m", is_uninstantiated = false, name = "m", node_id = 9 : i64, referenced_path = "module_def", referenced_symbol = @module_def} {
      obelisk.sv.symbol.instance_body @m_body attributes {hierarchical_name = "m", name = "m", node_id = 10 : i64} {
        obelisk.sv.symbol.procedural_block @m_first attributes {hierarchical_name = "m.first", node_id = 11 : i64, procedure_kind = 0 : i32} {
          obelisk.sv.statement.empty attributes {node_id = 12 : i64} {}
        }
        obelisk.sv.symbol.procedural_block @m_second attributes {hierarchical_name = "m.second", node_id = 13 : i64, procedure_kind = 0 : i32} {
          obelisk.sv.statement.empty attributes {node_id = 14 : i64} {}
        }
      }
    }
    obelisk.sv.symbol.instance @q attributes {hierarchical_name = "q", is_uninstantiated = false, name = "q", node_id = 15 : i64, referenced_path = "program_def", referenced_symbol = @program_def} {
      obelisk.sv.symbol.instance_body @q_body attributes {hierarchical_name = "q", name = "q", node_id = 16 : i64} {
        obelisk.sv.symbol.procedural_block @q_first attributes {hierarchical_name = "q.first", node_id = 17 : i64, procedure_kind = 0 : i32} {
          obelisk.sv.statement.empty attributes {node_id = 18 : i64} {}
        }
      }
    }
  }
}
