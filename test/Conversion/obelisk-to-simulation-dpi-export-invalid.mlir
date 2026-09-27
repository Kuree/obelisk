// RUN: obelisk-opt %s --split-input-file '--lower-obelisk-to-sim=opt-level=0' \
// RUN:   | FileCheck %s

module {
  obelisk.sv.symbol.definition attributes {definition_kind = 0 : i32,
      hierarchical_name = "task_export", name = "task_export",
      node_id = 0 : i64, sym_name = "task_def"} {
  }
  obelisk.sv.symbol.root attributes {hierarchical_name = "\\$root ",
      name = "$root", node_id = 1 : i64, sym_name = "root"} {
    obelisk.sv.symbol.instance attributes {hierarchical_name = "task_export",
        is_uninstantiated = false, name = "task_export", node_id = 2 : i64,
        referenced_path = "task_export", referenced_symbol = @task_def,
        sym_name = "task"} {
      obelisk.sv.symbol.instance_body attributes {
          hierarchical_name = "task_export", name = "task_export",
          node_id = 3 : i64, sym_name = "task_body"} {
        obelisk.sv.symbol.subroutine attributes {default_lifetime = 1 : i32,
            dpi_export_c_identifier = "unsupported_task",
            hierarchical_name = "task_export.unsupported_task",
            name = "unsupported_task", node_id = 4 : i64,
            semantic_type = !obelisk.subroutine<() -> (), true>,
            subroutine_kind = 1 : i32, sym_name = "unsupported_task"} {
          obelisk.sv.statement.list attributes {node_id = 5 : i64} {
          }
        }
      }
    }
  }
}

// CHECK: entry_kind = 12

// -----

module {
  obelisk.sv.symbol.definition attributes {definition_kind = 0 : i32,
      hierarchical_name = "aggregate_export", name = "aggregate_export",
      node_id = 0 : i64, sym_name = "aggregate_def"} {
  }
  obelisk.sv.symbol.root attributes {hierarchical_name = "\\$root ",
      name = "$root", node_id = 1 : i64, sym_name = "root"} {
    obelisk.sv.symbol.instance attributes {
        hierarchical_name = "aggregate_export", is_uninstantiated = false,
        name = "aggregate_export", node_id = 2 : i64,
        referenced_path = "aggregate_export",
        referenced_symbol = @aggregate_def, sym_name = "aggregate"} {
      obelisk.sv.symbol.instance_body attributes {
          hierarchical_name = "aggregate_export", name = "aggregate_export",
          node_id = 3 : i64, sym_name = "aggregate_body"} {
        obelisk.sv.symbol.subroutine attributes {default_lifetime = 1 : i32,
            dpi_export_c_identifier = "unsupported_aggregate",
            hierarchical_name = "aggregate_export.unsupported_aggregate",
            name = "unsupported_aggregate", node_id = 4 : i64,
            semantic_type = !obelisk.subroutine<(!obelisk.unpacked_array<2 x !obelisk.integral<32, true, false, 31 : 0, int>>) -> !obelisk.integral<32, true, false, 31 : 0, int>, false>,
            subroutine_kind = 0 : i32, sym_name = "unsupported_aggregate"} {
          obelisk.sv.statement.return attributes {node_id = 5 : i64} {
            obelisk.sv.expression.integer_literal attributes {
                constant_value = "0", node_id = 6 : i64,
                semantic_type = !obelisk.integral<32, true, false, 31 : 0, int>} {
            }
          }
          obelisk.sv.symbol.formal_argument attributes {direction = 0 : i32,
              hierarchical_name = "aggregate_export.unsupported_aggregate.value",
              name = "value", node_id = 7 : i64,
              semantic_type = !obelisk.unpacked_array<2 x !obelisk.integral<32, true, false, 31 : 0, int>>,
              sym_name = "value"} {
          }
        }
      }
    }
  }
}

// CHECK: #simulation.dpi_abi<kind = unpacked_aggregate
// CHECK: dpi_aggregate_abi
