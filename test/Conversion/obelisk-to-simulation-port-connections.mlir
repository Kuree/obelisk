// RUN: obelisk-opt %s '--lower-obelisk-to-sim=opt-level=0' | FileCheck %s

module {
  obelisk.sv.symbol.definition attributes {definition_kind = 0 : i32, hierarchical_name = "sim_port_child", name = "sim_port_child", node_id = 0 : i64, sym_name = "s0.sim_port_child"} {
  }
  obelisk.sv.symbol.definition attributes {definition_kind = 0 : i32, hierarchical_name = "supported_port_connections", name = "supported_port_connections", node_id = 1 : i64, sym_name = "s1.supported_port_connections"} {
  }
  obelisk.sv.symbol.root attributes {hierarchical_name = "\\$root ", name = "$root", node_id = 2 : i64, sym_name = "s2.$root"} {
    obelisk.sv.symbol.compilation_unit attributes {hierarchical_name = "$unit", node_id = 3 : i64, sym_name = "s3"} {
    }
    obelisk.sv.symbol.instance attributes {hierarchical_name = "supported_port_connections", is_uninstantiated = false, name = "supported_port_connections", node_id = 4 : i64, referenced_path = "supported_port_connections", referenced_symbol = @s1.supported_port_connections, sym_name = "s4.supported_port_connections"} {
      obelisk.sv.symbol.instance_body attributes {hierarchical_name = "supported_port_connections", name = "supported_port_connections", node_id = 5 : i64, sym_name = "s5.supported_port_connections"} {
        obelisk.sv.symbol.variable attributes {hierarchical_name = "supported_port_connections.source_value", lifetime = 1 : i32, name = "source_value", node_id = 6 : i64, semantic_type = !obelisk.ranged_packed_array<7 : 0 x !obelisk.integral<1, false, true, 0 : 0, logic>>, sym_name = "s6.source_value"} {
        }
        obelisk.sv.symbol.variable attributes {hierarchical_name = "supported_port_connections.destination", lifetime = 1 : i32, name = "destination", node_id = 7 : i64, semantic_type = !obelisk.ranged_packed_array<7 : 0 x !obelisk.integral<1, false, true, 0 : 0, logic>>, sym_name = "s7.destination"} {
        }
        obelisk.sv.symbol.instance attributes {hierarchical_name = "supported_port_connections.child", is_uninstantiated = false, name = "child", node_id = 8 : i64, referenced_path = "sim_port_child", referenced_symbol = @s0.sim_port_child, sym_name = "s8.child"} {
          obelisk.sv.port.connection attributes {actual_is_constant = false, direction = 0 : i32, formal_name = "value", formal_ordinal = 0 : i64, formal_path = "supported_port_connections.child.value", formal_symbol = @s2.$root::@s4.supported_port_connections::@s5.supported_port_connections::@s8.child::@s9.sim_port_child::@s10.value, formal_type = !obelisk.ranged_packed_array<3 : 0 x !obelisk.integral<1, false, true, 0 : 0, logic>>, internal_path = "supported_port_connections.child.value", internal_symbol = @s2.$root::@s4.supported_port_connections::@s5.supported_port_connections::@s8.child::@s9.sim_port_child::@s11.value, is_ansi = true, is_net = false, node_id = 9 : i64, provenance = 0 : i32} {
          } {
            obelisk.sv.expression.binary_op attributes {node_id = 10 : i64, operator_kind = 0 : i32, semantic_type = !obelisk.ranged_packed_array<3 : 0 x !obelisk.integral<1, false, true, 0 : 0, logic>>} {
              obelisk.sv.expression.range_select attributes {node_id = 11 : i64, selection_kind = 0 : i32, semantic_type = !obelisk.ranged_packed_array<3 : 0 x !obelisk.integral<1, false, true, 0 : 0, logic>>} {
                obelisk.sv.expression.named_value attributes {node_id = 12 : i64, referenced_path = "supported_port_connections.source_value", referenced_symbol = @s2.$root::@s4.supported_port_connections::@s5.supported_port_connections::@s6.source_value, semantic_type = !obelisk.ranged_packed_array<7 : 0 x !obelisk.integral<1, false, true, 0 : 0, logic>>} {
                }
                obelisk.sv.expression.integer_literal attributes {constant_value = "3", node_id = 13 : i64, semantic_type = !obelisk.integral<32, true, false, 31 : 0, int>} {
                }
                obelisk.sv.expression.integer_literal attributes {constant_value = "0", node_id = 14 : i64, semantic_type = !obelisk.integral<32, true, false, 31 : 0, int>} {
                }
              }
              obelisk.sv.expression.conversion attributes {node_id = 15 : i64, semantic_type = !obelisk.ranged_packed_array<3 : 0 x !obelisk.integral<1, false, true, 0 : 0, logic>>} {
                obelisk.sv.expression.integer_literal attributes {constant_value = "1'b1", node_id = 16 : i64, semantic_type = !obelisk.ranged_packed_array<0 : 0 x !obelisk.integral<1, false, false, 0 : 0, bit>>} {
                }
              }
            }
          }
          obelisk.sv.port.connection attributes {actual_is_constant = false, direction = 1 : i32, formal_name = "copied", formal_ordinal = 1 : i64, formal_path = "supported_port_connections.child.copied", formal_symbol = @s2.$root::@s4.supported_port_connections::@s5.supported_port_connections::@s8.child::@s9.sim_port_child::@s12.copied, formal_type = !obelisk.ranged_packed_array<3 : 0 x !obelisk.integral<1, false, true, 0 : 0, logic>>, internal_path = "supported_port_connections.child.copied", internal_symbol = @s2.$root::@s4.supported_port_connections::@s5.supported_port_connections::@s8.child::@s9.sim_port_child::@s13.copied, is_ansi = true, is_net = false, node_id = 17 : i64, provenance = 0 : i32} {
          } {
            obelisk.sv.expression.assignment attributes {assignment_kind = 0 : i32, node_id = 18 : i64, semantic_type = !obelisk.ranged_packed_array<3 : 0 x !obelisk.integral<1, false, true, 0 : 0, logic>>} {
              obelisk.sv.expression.concatenation attributes {node_id = 19 : i64, semantic_type = !obelisk.ranged_packed_array<3 : 0 x !obelisk.integral<1, false, true, 0 : 0, logic>>} {
                obelisk.sv.expression.range_select attributes {node_id = 20 : i64, selection_kind = 0 : i32, semantic_type = !obelisk.ranged_packed_array<7 : 6 x !obelisk.integral<1, false, true, 0 : 0, logic>>} {
                  obelisk.sv.expression.named_value attributes {node_id = 21 : i64, referenced_path = "supported_port_connections.destination", referenced_symbol = @s2.$root::@s4.supported_port_connections::@s5.supported_port_connections::@s7.destination, semantic_type = !obelisk.ranged_packed_array<7 : 0 x !obelisk.integral<1, false, true, 0 : 0, logic>>} {
                  }
                  obelisk.sv.expression.integer_literal attributes {constant_value = "7", node_id = 22 : i64, semantic_type = !obelisk.integral<32, true, false, 31 : 0, int>} {
                  }
                  obelisk.sv.expression.integer_literal attributes {constant_value = "6", node_id = 23 : i64, semantic_type = !obelisk.integral<32, true, false, 31 : 0, int>} {
                  }
                }
                obelisk.sv.expression.range_select attributes {node_id = 24 : i64, selection_kind = 0 : i32, semantic_type = !obelisk.ranged_packed_array<1 : 0 x !obelisk.integral<1, false, true, 0 : 0, logic>>} {
                  obelisk.sv.expression.named_value attributes {node_id = 25 : i64, referenced_path = "supported_port_connections.destination", referenced_symbol = @s2.$root::@s4.supported_port_connections::@s5.supported_port_connections::@s7.destination, semantic_type = !obelisk.ranged_packed_array<7 : 0 x !obelisk.integral<1, false, true, 0 : 0, logic>>} {
                  }
                  obelisk.sv.expression.integer_literal attributes {constant_value = "1", node_id = 26 : i64, semantic_type = !obelisk.integral<32, true, false, 31 : 0, int>} {
                  }
                  obelisk.sv.expression.integer_literal attributes {constant_value = "0", node_id = 27 : i64, semantic_type = !obelisk.integral<32, true, false, 31 : 0, int>} {
                  }
                }
              }
              obelisk.sv.expression.empty_argument attributes {node_id = 28 : i64, semantic_type = !obelisk.ranged_packed_array<3 : 0 x !obelisk.integral<1, false, true, 0 : 0, logic>>} {
              }
            }
          }
          obelisk.sv.symbol.instance_body attributes {hierarchical_name = "supported_port_connections.child", name = "sim_port_child", node_id = 29 : i64, sym_name = "s9.sim_port_child"} {
            obelisk.sv.symbol.port attributes {direction = 0 : i32, hierarchical_name = "supported_port_connections.child.value", name = "value", node_id = 30 : i64, semantic_type = !obelisk.ranged_packed_array<3 : 0 x !obelisk.integral<1, false, true, 0 : 0, logic>>, sym_name = "s10.value"} {
            }
            obelisk.sv.symbol.variable attributes {hierarchical_name = "supported_port_connections.child.value", lifetime = 1 : i32, name = "value", node_id = 31 : i64, semantic_type = !obelisk.ranged_packed_array<3 : 0 x !obelisk.integral<1, false, true, 0 : 0, logic>>, sym_name = "s11.value"} {
            }
            obelisk.sv.symbol.port attributes {direction = 1 : i32, hierarchical_name = "supported_port_connections.child.copied", name = "copied", node_id = 32 : i64, semantic_type = !obelisk.ranged_packed_array<3 : 0 x !obelisk.integral<1, false, true, 0 : 0, logic>>, sym_name = "s12.copied"} {
            }
            obelisk.sv.symbol.variable attributes {hierarchical_name = "supported_port_connections.child.copied", lifetime = 1 : i32, name = "copied", node_id = 33 : i64, semantic_type = !obelisk.ranged_packed_array<3 : 0 x !obelisk.integral<1, false, true, 0 : 0, logic>>, sym_name = "s13.copied"} {
            }
            obelisk.sv.symbol.procedural_block attributes {hierarchical_name = "supported_port_connections.child", node_id = 34 : i64, procedure_kind = 3 : i32, sym_name = "s14", time_precision_fs = 1000000 : i64, time_unit_fs = 1000000 : i64} {
              obelisk.sv.statement.expression_statement attributes {node_id = 35 : i64} {
                obelisk.sv.expression.assignment attributes {assignment_kind = 0 : i32, node_id = 36 : i64, semantic_type = !obelisk.ranged_packed_array<3 : 0 x !obelisk.integral<1, false, true, 0 : 0, logic>>} {
                  obelisk.sv.expression.named_value attributes {node_id = 37 : i64, referenced_path = "supported_port_connections.child.copied", referenced_symbol = @s2.$root::@s4.supported_port_connections::@s5.supported_port_connections::@s8.child::@s9.sim_port_child::@s13.copied, semantic_type = !obelisk.ranged_packed_array<3 : 0 x !obelisk.integral<1, false, true, 0 : 0, logic>>} {
                  }
                  obelisk.sv.expression.named_value attributes {node_id = 38 : i64, referenced_path = "supported_port_connections.child.value", referenced_symbol = @s2.$root::@s4.supported_port_connections::@s5.supported_port_connections::@s8.child::@s9.sim_port_child::@s11.value, semantic_type = !obelisk.ranged_packed_array<3 : 0 x !obelisk.integral<1, false, true, 0 : 0, logic>>} {
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

// CHECK-DAG: obelisk_sim.code_unit.decl {{[0-9]+}} in {{[0-9]+}} port_input hierarchy "supported_port_connections.child.$port_connection_0"{{.*}}{internal}
// CHECK-DAG: obelisk_sim.code_unit.decl {{[0-9]+}} in {{[0-9]+}} port_output hierarchy "supported_port_connections.child.$port_connection_1"{{.*}}{internal}
// CHECK-DAG: entry_kind = 9 : i32{{.*}}internal
// CHECK-DAG: entry_kind = 10 : i32{{.*}}internal
// CHECK-DAG: obelisk_sim.continuous_store
// CHECK-DAG: #obelisk_sim.effect<effect = write, resource = storage{{.*}}low = 0, width = 2
// CHECK-DAG: #obelisk_sim.effect<effect = write, resource = storage{{.*}}low = 6, width = 2
// CHECK-NOT: obelisk.sv.
