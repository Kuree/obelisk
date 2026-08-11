// RUN: obelisk-opt %s '--lower-obelisk-to-sim=opt-level=0' | FileCheck %s

// `time` is a four-state 64-bit unsigned integer, not a two-state one
// (IEEE 1800-2017 Table 6-8). Normalizing it to a plain i64 would give an
// uninitialized `time` variable the value zero instead of x, and would send
// its arithmetic down the two-state path where unknown bits cannot exist.

// CHECK-LABEL: obelisk_sim.func private @unit_0(
// CHECK-SAME: !obelisk_sim.ref<!obelisk_sim.logic<64>>
// CHECK: %[[LHS:.*]] = obelisk_sim.ref.load
// CHECK: obelisk_sim.logic.binary add %[[LHS]], %[[LHS]] : !obelisk_sim.logic<64>

module {
  obelisk.sv.symbol.definition attributes {definition_kind = 0 : i32, hierarchical_name = "t", name = "t", node_id = 0 : i64, sym_name = "s0.t"} {
  }
  obelisk.sv.symbol.root attributes {hierarchical_name = "\\$root ", name = "$root", node_id = 1 : i64, sym_name = "s1.$root"} {
    obelisk.sv.symbol.compilation_unit attributes {hierarchical_name = "$unit", node_id = 2 : i64, sym_name = "s2"} {
    }
    obelisk.sv.symbol.instance attributes {hierarchical_name = "t", is_uninstantiated = false, name = "t", node_id = 3 : i64, referenced_path = "t", referenced_symbol = @s0.t, sym_name = "s3.t"} {
      obelisk.sv.symbol.instance_body attributes {hierarchical_name = "t", name = "t", node_id = 4 : i64, sym_name = "s4.t", time_precision_fs = 1000000 : i64, time_unit_fs = 1000000 : i64} {
        obelisk.sv.symbol.variable attributes {hierarchical_name = "t.slot", lifetime = 1 : i32, name = "slot", node_id = 5 : i64, semantic_type = !obelisk.time, sym_name = "s5.slot"} {
        }
        obelisk.sv.symbol.procedural_block attributes {hierarchical_name = "t", node_id = 6 : i64, procedure_kind = 0 : i32, sym_name = "s6", time_precision_fs = 1000000 : i64, time_unit_fs = 1000000 : i64} {
          obelisk.sv.statement.block attributes {node_id = 7 : i64} {
            obelisk.sv.statement.list attributes {node_id = 8 : i64} {
              obelisk.sv.statement.expression_statement attributes {node_id = 9 : i64} {
                obelisk.sv.expression.assignment attributes {assignment_kind = 0 : i32, is_signed = false, node_id = 10 : i64, semantic_type = !obelisk.time} {
                  obelisk.sv.expression.named_value attributes {is_signed = false, node_id = 11 : i64, referenced_path = "t.slot", referenced_symbol = @s1.$root::@s3.t::@s4.t::@s5.slot, semantic_type = !obelisk.time} {
                  }
                  obelisk.sv.expression.binary_op attributes {is_signed = false, node_id = 12 : i64, operator_kind = 0 : i32, semantic_type = !obelisk.time} {
                    obelisk.sv.expression.named_value attributes {is_signed = false, node_id = 13 : i64, referenced_path = "t.slot", referenced_symbol = @s1.$root::@s3.t::@s4.t::@s5.slot, semantic_type = !obelisk.time} {
                    }
                    obelisk.sv.expression.named_value attributes {is_signed = false, node_id = 14 : i64, referenced_path = "t.slot", referenced_symbol = @s1.$root::@s3.t::@s4.t::@s5.slot, semantic_type = !obelisk.time} {
                    }
                  }
                }
              }
            }
          }
        }
      }
    }
  }
}
