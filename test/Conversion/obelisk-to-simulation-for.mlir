// RUN: obelisk-opt %s '--lower-obelisk-to-sim=opt-level=0' \
// RUN:   | FileCheck %s --implicit-check-not=obelisk_sim.static.once

module {
  obelisk.sv.symbol.definition attributes {definition_kind = 0 : i32, hierarchical_name = "simulation_for", name = "simulation_for", node_id = 0 : i64, sym_name = "s0.simulation_for"} {
  }
  obelisk.sv.symbol.root attributes {hierarchical_name = "\\$root ", name = "$root", node_id = 1 : i64, sym_name = "s1.$root"} {
    obelisk.sv.symbol.compilation_unit attributes {hierarchical_name = "$unit", node_id = 2 : i64, sym_name = "s2"} {
    }
    obelisk.sv.symbol.instance attributes {hierarchical_name = "simulation_for", is_uninstantiated = false, name = "simulation_for", node_id = 3 : i64, referenced_path = "simulation_for", referenced_symbol = @s0.simulation_for, sym_name = "s3.simulation_for"} {
      obelisk.sv.symbol.instance_body attributes {hierarchical_name = "simulation_for", name = "simulation_for", node_id = 4 : i64, sym_name = "s4.simulation_for"} {
        obelisk.sv.symbol.variable attributes {hierarchical_name = "simulation_for.value", lifetime = 1 : i32, name = "value", node_id = 5 : i64, semantic_type = !obelisk.ranged_packed_array<7 : 0 x !obelisk.integral<1, false, true, 0 : 0, logic>>, sym_name = "s5.value"} {
        }
        // Unnamed for-init scopes do not contribute to the hierarchy string.
        // Keep outer storage with the same path as the automatic loop variable
        // below to ensure capture preparation follows the symbol reference.
        obelisk.sv.symbol.variable attributes {hierarchical_name = "simulation_for.i", lifetime = 1 : i32, name = "i", node_id = 1000 : i64, semantic_type = !obelisk.integral<32, true, false, 31 : 0, int>, sym_name = "s1000.i"} {
        }
        obelisk.sv.symbol.statement_block attributes {block_kind = 0 : i32, hierarchical_name = "simulation_for", node_id = 6 : i64, sym_name = "s6"} {
          obelisk.sv.symbol.variable attributes {hierarchical_name = "simulation_for.i", name = "i", node_id = 7 : i64, semantic_type = !obelisk.integral<32, true, false, 31 : 0, int>, sym_name = "s7.i"} {
            obelisk.sv.expression.integer_literal attributes {constant_value = "2", node_id = 8 : i64, semantic_type = !obelisk.integral<32, true, false, 31 : 0, int>} {
            }
          }
        }
        obelisk.sv.symbol.procedural_block attributes {hierarchical_name = "simulation_for", node_id = 9 : i64, procedure_kind = 0 : i32, sym_name = "s8", time_precision_fs = 1000000 : i64, time_unit_fs = 1000000 : i64} {
          obelisk.sv.statement.block attributes {node_id = 10 : i64} {
            obelisk.sv.statement.block attributes {node_id = 11 : i64} {
              obelisk.sv.statement.list attributes {node_id = 12 : i64} {
                obelisk.sv.statement.variable_declaration attributes {node_id = 13 : i64, referenced_path = "simulation_for.i", referenced_symbol = @s1.$root::@s3.simulation_for::@s4.simulation_for::@s6::@s7.i} {
                }
                obelisk.sv.statement.for_loop attributes {has_condition = true, initializer_count = 0 : i64, node_id = 14 : i64, step_count = 1 : i64} {
                  obelisk.sv.expression.binary_op attributes {node_id = 15 : i64, operator_kind = 16 : i32, semantic_type = !obelisk.integral<1, false, false, 0 : 0, bit>} {
                    obelisk.sv.expression.named_value attributes {node_id = 16 : i64, referenced_path = "simulation_for.i", referenced_symbol = @s1.$root::@s3.simulation_for::@s4.simulation_for::@s6::@s7.i, semantic_type = !obelisk.integral<32, true, false, 31 : 0, int>} {
                    }
                    obelisk.sv.expression.integer_literal attributes {constant_value = "6", node_id = 17 : i64, semantic_type = !obelisk.integral<32, true, false, 31 : 0, int>} {
                    }
                  }
                  obelisk.sv.expression.unary_op attributes {node_id = 18 : i64, operator_kind = 12 : i32, semantic_type = !obelisk.integral<32, true, false, 31 : 0, int>} {
                    obelisk.sv.expression.named_value attributes {node_id = 19 : i64, referenced_path = "simulation_for.i", referenced_symbol = @s1.$root::@s3.simulation_for::@s4.simulation_for::@s6::@s7.i, semantic_type = !obelisk.integral<32, true, false, 31 : 0, int>} {
                    }
                  }
                  obelisk.sv.statement.block attributes {node_id = 20 : i64} {
                    obelisk.sv.statement.list attributes {node_id = 21 : i64} {
                      obelisk.sv.statement.conditional attributes {check_kind = 0 : i32, condition_count = 1 : i64, condition_pattern_flags = array<i64: 0>, has_else = false, node_id = 22 : i64} {
                        obelisk.sv.expression.binary_op attributes {node_id = 23 : i64, operator_kind = 9 : i32, semantic_type = !obelisk.integral<1, false, false, 0 : 0, bit>} {
                          obelisk.sv.expression.named_value attributes {node_id = 24 : i64, referenced_path = "simulation_for.i", referenced_symbol = @s1.$root::@s3.simulation_for::@s4.simulation_for::@s6::@s7.i, semantic_type = !obelisk.integral<32, true, false, 31 : 0, int>} {
                          }
                          obelisk.sv.expression.integer_literal attributes {constant_value = "3", node_id = 25 : i64, semantic_type = !obelisk.integral<32, true, false, 31 : 0, int>} {
                          }
                        }
                        obelisk.sv.statement.continue attributes {node_id = 26 : i64} {
                        }
                      }
                      obelisk.sv.statement.conditional attributes {check_kind = 0 : i32, condition_count = 1 : i64, condition_pattern_flags = array<i64: 0>, has_else = false, node_id = 27 : i64} {
                        obelisk.sv.expression.binary_op attributes {node_id = 28 : i64, operator_kind = 9 : i32, semantic_type = !obelisk.integral<1, false, false, 0 : 0, bit>} {
                          obelisk.sv.expression.named_value attributes {node_id = 29 : i64, referenced_path = "simulation_for.i", referenced_symbol = @s1.$root::@s3.simulation_for::@s4.simulation_for::@s6::@s7.i, semantic_type = !obelisk.integral<32, true, false, 31 : 0, int>} {
                          }
                          obelisk.sv.expression.integer_literal attributes {constant_value = "5", node_id = 30 : i64, semantic_type = !obelisk.integral<32, true, false, 31 : 0, int>} {
                          }
                        }
                        obelisk.sv.statement.break attributes {node_id = 31 : i64} {
                        }
                      }
                      obelisk.sv.statement.expression_statement attributes {node_id = 32 : i64} {
                        obelisk.sv.expression.assignment attributes {assignment_kind = 0 : i32, node_id = 33 : i64, semantic_type = !obelisk.ranged_packed_array<7 : 0 x !obelisk.integral<1, false, true, 0 : 0, logic>>} {
                          obelisk.sv.expression.named_value attributes {node_id = 34 : i64, referenced_path = "simulation_for.value", referenced_symbol = @s1.$root::@s3.simulation_for::@s4.simulation_for::@s5.value, semantic_type = !obelisk.ranged_packed_array<7 : 0 x !obelisk.integral<1, false, true, 0 : 0, logic>>} {
                          }
                          obelisk.sv.expression.conversion attributes {node_id = 35 : i64, semantic_type = !obelisk.ranged_packed_array<7 : 0 x !obelisk.integral<1, false, true, 0 : 0, logic>>} {
                            obelisk.sv.expression.binary_op attributes {node_id = 36 : i64, operator_kind = 0 : i32, semantic_type = !obelisk.ranged_packed_array<31 : 0 x !obelisk.integral<1, false, true, 0 : 0, logic>>} {
                              obelisk.sv.expression.conversion attributes {node_id = 37 : i64, semantic_type = !obelisk.ranged_packed_array<31 : 0 x !obelisk.integral<1, false, true, 0 : 0, logic>>} {
                                obelisk.sv.expression.named_value attributes {node_id = 38 : i64, referenced_path = "simulation_for.value", referenced_symbol = @s1.$root::@s3.simulation_for::@s4.simulation_for::@s5.value, semantic_type = !obelisk.ranged_packed_array<7 : 0 x !obelisk.integral<1, false, true, 0 : 0, logic>>} {
                                }
                              }
                              obelisk.sv.expression.conversion attributes {node_id = 39 : i64, semantic_type = !obelisk.ranged_packed_array<31 : 0 x !obelisk.integral<1, false, true, 0 : 0, logic>>} {
                                obelisk.sv.expression.integer_literal attributes {constant_value = "1", node_id = 40 : i64, semantic_type = !obelisk.integral<32, true, false, 31 : 0, int>} {
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
          }
        }
      }
    }
  }
}

// CHECK: obelisk_sim.func
// CHECK: %[[INITIAL:.*]] = arith.constant 2 : i32
// CHECK: cf.br ^[[LOOP:bb[0-9]+]](%[[INITIAL]] : i32)
// CHECK: ^[[LOOP]](%[[LOCAL:.*]]: i32):
// CHECK: arith.cmpi slt, %[[LOCAL]],
// CHECK: cf.cond_br
// CHECK: arith.addi
// CHECK: cf.br
// CHECK-NOT: obelisk.sv.
