// RUN: obelisk-opt %s '--lower-obelisk-to-sim=opt-level=0' | FileCheck %s

module {
  obelisk.sv.symbol.definition attributes {definition_kind = 0 : i32, hierarchical_name = "supported_static_local", name = "supported_static_local", node_id = 0 : i64, sym_name = "s0.supported_static_local"} {
  }
  obelisk.sv.symbol.root attributes {hierarchical_name = "\\$root ", name = "$root", node_id = 1 : i64, sym_name = "s1.$root"} {
    obelisk.sv.symbol.compilation_unit attributes {hierarchical_name = "$unit", node_id = 2 : i64, sym_name = "s2"} {
    }
    obelisk.sv.symbol.instance attributes {hierarchical_name = "supported_static_local", is_uninstantiated = false, name = "supported_static_local", node_id = 3 : i64, referenced_path = "supported_static_local", referenced_symbol = @s0.supported_static_local, sym_name = "s3.supported_static_local"} {
      obelisk.sv.symbol.instance_body attributes {hierarchical_name = "supported_static_local", name = "supported_static_local", node_id = 4 : i64, sym_name = "s4.supported_static_local"} {
        obelisk.sv.symbol.statement_block attributes {block_kind = 0 : i32, hierarchical_name = "supported_static_local", node_id = 5 : i64, sym_name = "s5"} {
          obelisk.sv.symbol.variable attributes {hierarchical_name = "supported_static_local.value", lifetime = 1 : i32, name = "value", node_id = 6 : i64, semantic_type = !obelisk.integral<1, false, true, 0 : 0, logic>, sym_name = "s6.value"} {
            obelisk.sv.expression.conversion attributes {node_id = 7 : i64, semantic_type = !obelisk.integral<1, false, true, 0 : 0, logic>} {
              obelisk.sv.expression.integer_literal attributes {constant_value = "1'b1", node_id = 8 : i64, semantic_type = !obelisk.ranged_packed_array<0 : 0 x !obelisk.integral<1, false, false, 0 : 0, bit>>} {
              }
            }
          }
        }
        obelisk.sv.symbol.procedural_block attributes {hierarchical_name = "supported_static_local", node_id = 9 : i64, procedure_kind = 0 : i32, sym_name = "s7", time_precision_fs = 1000000 : i64, time_unit_fs = 1000000 : i64} {
          obelisk.sv.statement.block attributes {node_id = 10 : i64} {
            obelisk.sv.statement.variable_declaration attributes {node_id = 11 : i64, referenced_path = "supported_static_local.value", referenced_symbol = @s1.$root::@s3.supported_static_local::@s4.supported_static_local::@s5::@s6.value} {
            }
          }
        }
      }
    }
  }
}

// CHECK: obelisk_sim.static.once
// CHECK: cf.cond_br
// CHECK: obelisk_sim.ref.store
// CHECK-NOT: obelisk.sv.
