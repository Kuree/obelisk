// RUN: obelisk-opt %s '--lower-obelisk-to-sim=opt-level=0' | FileCheck %s

// IEEE 1800-2017 11.11 selects exactly one min:typ:max branch for the
// compilation. Lowering must not evaluate or materialize the other branches.

module {
  obelisk.sv.symbol.definition @definition attributes {definition_kind = 0 : i32, hierarchical_name = "top", name = "top", node_id = 0 : i64} {
  }
  obelisk.sv.symbol.root @root attributes {hierarchical_name = "\\$root ", name = "$root", node_id = 1 : i64} {
    obelisk.sv.symbol.compilation_unit @unit attributes {hierarchical_name = "$unit", node_id = 2 : i64} {
    }
    obelisk.sv.symbol.instance @instance attributes {hierarchical_name = "top", is_uninstantiated = false, name = "top", node_id = 3 : i64, referenced_path = "top", referenced_symbol = @definition} {
      obelisk.sv.symbol.instance_body @body attributes {hierarchical_name = "top", name = "top", node_id = 4 : i64} {
        obelisk.sv.symbol.procedural_block @process attributes {hierarchical_name = "top", node_id = 5 : i64, procedure_kind = 0 : i32, time_precision_fs = 1000000 : i64, time_unit_fs = 1000000 : i64} {
          obelisk.sv.statement.timed attributes {node_id = 6 : i64} {
            obelisk.sv.timing.delay attributes {node_id = 7 : i64} {
              obelisk.sv.expression.min_typ_max attributes {node_id = 8 : i64, selected_index = 1 : i64, semantic_type = !obelisk.integral<32, true, false, 31 : 0, int>} {
                obelisk.sv.expression.integer_literal attributes {constant_value = "4", node_id = 9 : i64, semantic_type = !obelisk.integral<32, true, false, 31 : 0, int>} {
                }
                obelisk.sv.expression.integer_literal attributes {constant_value = "5", node_id = 10 : i64, semantic_type = !obelisk.integral<32, true, false, 31 : 0, int>} {
                }
                obelisk.sv.expression.integer_literal attributes {constant_value = "6", node_id = 11 : i64, semantic_type = !obelisk.integral<32, true, false, 31 : 0, int>} {
                }
              }
            }
            obelisk.sv.statement.empty attributes {node_id = 12 : i64} {
            }
          }
        }
      }
    }
  }
}

// CHECK-NOT: arith.constant 4 : i64
// CHECK: %[[SELECTED:.*]] = arith.constant 5 : i64
// CHECK-NOT: arith.constant 6 : i64
// CHECK: %[[DELAY:.*]] = simulation.time.scale %[[SELECTED]] by 1 signed = false : i64
// CHECK: simulation.suspend.delay %[[DELAY]]
// CHECK-NOT: obelisk.sv.
