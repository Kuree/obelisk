// RUN: obelisk-opt %s '--lower-obelisk-to-sim=opt-level=0' | FileCheck %s

module {
  obelisk.sv.symbol.definition attributes {definition_kind = 0 : i32, hierarchical_name = "simulation_loop_friends", name = "simulation_loop_friends", node_id = 0 : i64, sym_name = "s0.simulation_loop_friends"} {
  }
  obelisk.sv.symbol.root attributes {hierarchical_name = "\\$root ", name = "$root", node_id = 1 : i64, sym_name = "s1.$root"} {
    obelisk.sv.symbol.compilation_unit attributes {hierarchical_name = "$unit", node_id = 2 : i64, sym_name = "s2"} {
    }
    obelisk.sv.symbol.instance attributes {hierarchical_name = "simulation_loop_friends", is_uninstantiated = false, name = "simulation_loop_friends", node_id = 3 : i64, referenced_path = "simulation_loop_friends", referenced_symbol = @s0.simulation_loop_friends, sym_name = "s3.simulation_loop_friends"} {
      obelisk.sv.symbol.instance_body attributes {hierarchical_name = "simulation_loop_friends", name = "simulation_loop_friends", node_id = 4 : i64, sym_name = "s4.simulation_loop_friends"} {
        obelisk.sv.symbol.variable attributes {hierarchical_name = "simulation_loop_friends.indices_only", lifetime = 1 : i32, name = "indices_only", node_id = 5 : i64, semantic_type = !obelisk.ranged_packed_array<2 : 0 x !obelisk.integral<1, false, true, 0 : 0, logic>>, sym_name = "s5.indices_only"} {
        }
        obelisk.sv.symbol.variable attributes {hierarchical_name = "simulation_loop_friends.once", lifetime = 1 : i32, name = "once", node_id = 6 : i64, semantic_type = !obelisk.integral<32, true, true, 31 : 0, integer>, sym_name = "s6.once"} {
        }
        obelisk.sv.symbol.variable attributes {hierarchical_name = "simulation_loop_friends.count", lifetime = 1 : i32, name = "count", node_id = 7 : i64, semantic_type = !obelisk.integral<32, true, true, 31 : 0, integer>, sym_name = "s7.count"} {
        }
        obelisk.sv.symbol.statement_block attributes {block_kind = 0 : i32, hierarchical_name = "simulation_loop_friends", node_id = 8 : i64, sym_name = "s8"} {
          obelisk.sv.symbol.iterator attributes {array_type = !obelisk.ranged_packed_array<2 : 0 x !obelisk.integral<1, false, true, 0 : 0, logic>>, hierarchical_name = "simulation_loop_friends.index", index_method_name = "", is_const, name = "index", node_id = 9 : i64, semantic_type = !obelisk.integral<32, true, false, 31 : 0, int>, sym_name = "s9.index"} {
          }
        }
        obelisk.sv.symbol.procedural_block attributes {hierarchical_name = "simulation_loop_friends", node_id = 10 : i64, procedure_kind = 0 : i32, sym_name = "s10", time_precision_fs = 1000000 : i64, time_unit_fs = 1000000 : i64} {
          obelisk.sv.statement.block attributes {node_id = 11 : i64} {
            obelisk.sv.statement.list attributes {node_id = 12 : i64} {
              obelisk.sv.statement.expression_statement attributes {node_id = 13 : i64} {
                obelisk.sv.expression.assignment attributes {assignment_kind = 0 : i32, node_id = 14 : i64, semantic_type = !obelisk.integral<32, true, true, 31 : 0, integer>} {
                  obelisk.sv.expression.named_value attributes {node_id = 15 : i64, referenced_path = "simulation_loop_friends.once", referenced_symbol = @s1.$root::@s3.simulation_loop_friends::@s4.simulation_loop_friends::@s6.once, semantic_type = !obelisk.integral<32, true, true, 31 : 0, integer>} {
                  }
                  obelisk.sv.expression.conversion attributes {node_id = 16 : i64, semantic_type = !obelisk.integral<32, true, true, 31 : 0, integer>} {
                    obelisk.sv.expression.integer_literal attributes {constant_value = "0", node_id = 17 : i64, semantic_type = !obelisk.integral<32, true, false, 31 : 0, int>} {
                    }
                  }
                }
              }
              obelisk.sv.statement.do_while_loop attributes {node_id = 18 : i64} {
                obelisk.sv.expression.integer_literal attributes {constant_value = "0", node_id = 19 : i64, semantic_type = !obelisk.integral<32, true, false, 31 : 0, int>} {
                }
                obelisk.sv.statement.expression_statement attributes {node_id = 20 : i64} {
                  obelisk.sv.expression.assignment attributes {assignment_kind = 0 : i32, node_id = 21 : i64, semantic_type = !obelisk.integral<32, true, true, 31 : 0, integer>} {
                    obelisk.sv.expression.named_value attributes {node_id = 22 : i64, referenced_path = "simulation_loop_friends.once", referenced_symbol = @s1.$root::@s3.simulation_loop_friends::@s4.simulation_loop_friends::@s6.once, semantic_type = !obelisk.integral<32, true, true, 31 : 0, integer>} {
                    }
                    obelisk.sv.expression.binary_op attributes {node_id = 23 : i64, operator_kind = 0 : i32, semantic_type = !obelisk.integral<32, true, true, 31 : 0, integer>} {
                      obelisk.sv.expression.named_value attributes {node_id = 24 : i64, referenced_path = "simulation_loop_friends.once", referenced_symbol = @s1.$root::@s3.simulation_loop_friends::@s4.simulation_loop_friends::@s6.once, semantic_type = !obelisk.integral<32, true, true, 31 : 0, integer>} {
                      }
                      obelisk.sv.expression.conversion attributes {node_id = 25 : i64, semantic_type = !obelisk.integral<32, true, true, 31 : 0, integer>} {
                        obelisk.sv.expression.integer_literal attributes {constant_value = "1", node_id = 26 : i64, semantic_type = !obelisk.integral<32, true, false, 31 : 0, int>} {
                        }
                      }
                    }
                  }
                }
              }
              obelisk.sv.statement.forever_loop attributes {node_id = 27 : i64} {
                obelisk.sv.statement.block attributes {node_id = 28 : i64} {
                  obelisk.sv.statement.list attributes {node_id = 29 : i64} {
                    obelisk.sv.statement.expression_statement attributes {node_id = 30 : i64} {
                      obelisk.sv.expression.assignment attributes {assignment_kind = 0 : i32, node_id = 31 : i64, semantic_type = !obelisk.integral<32, true, true, 31 : 0, integer>} {
                        obelisk.sv.expression.named_value attributes {node_id = 32 : i64, referenced_path = "simulation_loop_friends.count", referenced_symbol = @s1.$root::@s3.simulation_loop_friends::@s4.simulation_loop_friends::@s7.count, semantic_type = !obelisk.integral<32, true, true, 31 : 0, integer>} {
                        }
                        obelisk.sv.expression.binary_op attributes {node_id = 33 : i64, operator_kind = 0 : i32, semantic_type = !obelisk.integral<32, true, true, 31 : 0, integer>} {
                          obelisk.sv.expression.named_value attributes {node_id = 34 : i64, referenced_path = "simulation_loop_friends.count", referenced_symbol = @s1.$root::@s3.simulation_loop_friends::@s4.simulation_loop_friends::@s7.count, semantic_type = !obelisk.integral<32, true, true, 31 : 0, integer>} {
                          }
                          obelisk.sv.expression.conversion attributes {node_id = 35 : i64, semantic_type = !obelisk.integral<32, true, true, 31 : 0, integer>} {
                            obelisk.sv.expression.integer_literal attributes {constant_value = "1", node_id = 36 : i64, semantic_type = !obelisk.integral<32, true, false, 31 : 0, int>} {
                            }
                          }
                        }
                      }
                    }
                    obelisk.sv.statement.break attributes {node_id = 37 : i64} {
                    }
                  }
                }
              }
              obelisk.sv.statement.block attributes {node_id = 38 : i64} {
                obelisk.sv.statement.foreach_loop attributes {loop_dimensions = [{has_iterator = true, has_static_range = true, iterator_path = "simulation_loop_friends.index", iterator_symbol = @s1.$root::@s4.simulation_loop_friends::@s8::@s9.index, iterator_type = !obelisk.integral<32, true, false, 31 : 0, int>, left = 2 : i64, right = 0 : i64}], node_id = 39 : i64} {
                  obelisk.sv.expression.named_value attributes {node_id = 40 : i64, referenced_path = "simulation_loop_friends.indices_only", referenced_symbol = @s1.$root::@s3.simulation_loop_friends::@s4.simulation_loop_friends::@s5.indices_only, semantic_type = !obelisk.ranged_packed_array<2 : 0 x !obelisk.integral<1, false, true, 0 : 0, logic>>} {
                  }
                  obelisk.sv.statement.expression_statement attributes {node_id = 41 : i64} {
                    obelisk.sv.expression.assignment attributes {assignment_kind = 0 : i32, node_id = 42 : i64, semantic_type = !obelisk.integral<32, true, true, 31 : 0, integer>} {
                      obelisk.sv.expression.named_value attributes {node_id = 43 : i64, referenced_path = "simulation_loop_friends.count", referenced_symbol = @s1.$root::@s3.simulation_loop_friends::@s4.simulation_loop_friends::@s7.count, semantic_type = !obelisk.integral<32, true, true, 31 : 0, integer>} {
                      }
                      obelisk.sv.expression.binary_op attributes {node_id = 44 : i64, operator_kind = 0 : i32, semantic_type = !obelisk.integral<32, true, true, 31 : 0, integer>} {
                        obelisk.sv.expression.named_value attributes {node_id = 45 : i64, referenced_path = "simulation_loop_friends.count", referenced_symbol = @s1.$root::@s3.simulation_loop_friends::@s4.simulation_loop_friends::@s7.count, semantic_type = !obelisk.integral<32, true, true, 31 : 0, integer>} {
                        }
                        obelisk.sv.expression.conversion attributes {node_id = 46 : i64, semantic_type = !obelisk.integral<32, true, true, 31 : 0, integer>} {
                          obelisk.sv.expression.named_value attributes {node_id = 47 : i64, referenced_path = "simulation_loop_friends.index", referenced_symbol = @s1.$root::@s3.simulation_loop_friends::@s4.simulation_loop_friends::@s8::@s9.index, semantic_type = !obelisk.integral<32, true, false, 31 : 0, int>} {
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

// The fixed foreach collection is not a process capture or aggregate read.
// CHECK-LABEL: obelisk_sim.func{{.*}}@unit_0(
// CHECK-NOT: !obelisk_sim.ref<!obelisk_sim.packed_array
// CHECK: arith.cmpi ult
// CHECK: arith.remui
// CHECK-NOT: obelisk.sv.
