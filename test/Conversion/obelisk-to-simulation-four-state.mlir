// RUN: obelisk-opt %s --lower-obelisk-to-sim > %t.threaded.mlir
// RUN: obelisk-opt %s --lower-obelisk-to-sim --mlir-disable-threading > %t.single.mlir
// RUN: diff -u %t.single.mlir %t.threaded.mlir
// RUN: FileCheck %s < %t.threaded.mlir

module {
  obelisk.sv.symbol.definition attributes {definition_kind = 0 : i32, hierarchical_name = "simulation_four_state", name = "simulation_four_state", node_id = 0 : i64, sym_name = "s0.simulation_four_state"} {
  }
  obelisk.sv.symbol.root attributes {hierarchical_name = "\\$root ", name = "$root", node_id = 1 : i64, sym_name = "s1.$root"} {
    obelisk.sv.symbol.compilation_unit attributes {hierarchical_name = "$unit", node_id = 2 : i64, sym_name = "s2"} {
    }
    obelisk.sv.symbol.instance attributes {hierarchical_name = "simulation_four_state", is_uninstantiated = false, name = "simulation_four_state", node_id = 3 : i64, referenced_path = "simulation_four_state", referenced_symbol = @s0.simulation_four_state, sym_name = "s3.simulation_four_state"} {
      obelisk.sv.symbol.instance_body attributes {hierarchical_name = "simulation_four_state", name = "simulation_four_state", node_id = 4 : i64, sym_name = "s4.simulation_four_state"} {
        obelisk.sv.symbol.variable attributes {hierarchical_name = "simulation_four_state.four_state_input", lifetime = 1 : i32, name = "four_state_input", node_id = 5 : i64, semantic_type = !obelisk.ranged_packed_array<7 : 0 x !obelisk.integral<1, false, true, 0 : 0, logic>>, sym_name = "s5.four_state_input"} {
        }
        obelisk.sv.symbol.variable attributes {hierarchical_name = "simulation_four_state.condition", lifetime = 1 : i32, name = "condition", node_id = 6 : i64, semantic_type = !obelisk.integral<1, false, true, 0 : 0, logic>, sym_name = "s6.condition"} {
        }
        obelisk.sv.symbol.variable attributes {hierarchical_name = "simulation_four_state.value", lifetime = 1 : i32, name = "value", node_id = 7 : i64, semantic_type = !obelisk.ranged_packed_array<7 : 0 x !obelisk.integral<1, false, true, 0 : 0, logic>>, sym_name = "s7.value"} {
        }
        obelisk.sv.symbol.variable attributes {hierarchical_name = "simulation_four_state.declared_range", lifetime = 1 : i32, name = "declared_range", node_id = 8 : i64, semantic_type = !obelisk.ranged_packed_array<15 : 8 x !obelisk.integral<1, false, true, 0 : 0, logic>>, sym_name = "s8.declared_range"} {
        }
        obelisk.sv.symbol.variable attributes {hierarchical_name = "simulation_four_state.logic_read", lifetime = 1 : i32, name = "logic_read", node_id = 9 : i64, semantic_type = !obelisk.ranged_packed_array<3 : 0 x !obelisk.integral<1, false, true, 0 : 0, logic>>, sym_name = "s9.logic_read"} {
        }
        obelisk.sv.symbol.variable attributes {hierarchical_name = "simulation_four_state.bits", lifetime = 1 : i32, name = "bits", node_id = 10 : i64, semantic_type = !obelisk.ranged_packed_array<7 : 0 x !obelisk.integral<1, false, false, 0 : 0, bit>>, sym_name = "s10.bits"} {
        }
        obelisk.sv.symbol.variable attributes {hierarchical_name = "simulation_four_state.literal_bits", lifetime = 1 : i32, name = "literal_bits", node_id = 11 : i64, semantic_type = !obelisk.ranged_packed_array<7 : 0 x !obelisk.integral<1, false, false, 0 : 0, bit>>, sym_name = "s11.literal_bits"} {
        }
        obelisk.sv.symbol.variable attributes {hierarchical_name = "simulation_four_state.bits_read", lifetime = 1 : i32, name = "bits_read", node_id = 12 : i64, semantic_type = !obelisk.ranged_packed_array<3 : 0 x !obelisk.integral<1, false, false, 0 : 0, bit>>, sym_name = "s12.bits_read"} {
        }
        obelisk.sv.symbol.variable attributes {hierarchical_name = "simulation_four_state.bit_read", lifetime = 1 : i32, name = "bit_read", node_id = 13 : i64, semantic_type = !obelisk.integral<1, false, false, 0 : 0, bit>, sym_name = "s13.bit_read"} {
        }
        obelisk.sv.symbol.variable attributes {hierarchical_name = "simulation_four_state.logic_index", lifetime = 1 : i32, name = "logic_index", node_id = 14 : i64, semantic_type = !obelisk.ranged_packed_array<31 : 0 x !obelisk.integral<1, false, true, 0 : 0, logic>>, sym_name = "s14.logic_index"} {
        }
        obelisk.sv.symbol.variable attributes {hierarchical_name = "simulation_four_state.integer_index", lifetime = 1 : i32, name = "integer_index", node_id = 15 : i64, semantic_type = !obelisk.integral<32, true, false, 31 : 0, int>, sym_name = "s15.integer_index"} {
        }
        obelisk.sv.symbol.procedural_block attributes {hierarchical_name = "simulation_four_state", node_id = 16 : i64, procedure_kind = 0 : i32, sym_name = "s16", time_precision_fs = 1000000 : i64, time_unit_fs = 1000000 : i64} {
          obelisk.sv.statement.block attributes {node_id = 17 : i64} {
            obelisk.sv.statement.list attributes {node_id = 18 : i64} {
              obelisk.sv.statement.conditional attributes {check_kind = 0 : i32, condition_count = 1 : i64, condition_pattern_flags = array<i64: 0>, has_else = false, node_id = 19 : i64} {
                obelisk.sv.expression.named_value attributes {node_id = 20 : i64, referenced_path = "simulation_four_state.condition", referenced_symbol = @s1.$root::@s3.simulation_four_state::@s4.simulation_four_state::@s6.condition, semantic_type = !obelisk.integral<1, false, true, 0 : 0, logic>} {
                }
                obelisk.sv.statement.expression_statement attributes {node_id = 21 : i64} {
                  obelisk.sv.expression.assignment attributes {assignment_kind = 0 : i32, node_id = 22 : i64, semantic_type = !obelisk.ranged_packed_array<7 : 0 x !obelisk.integral<1, false, true, 0 : 0, logic>>} {
                    obelisk.sv.expression.named_value attributes {node_id = 23 : i64, referenced_path = "simulation_four_state.value", referenced_symbol = @s1.$root::@s3.simulation_four_state::@s4.simulation_four_state::@s7.value, semantic_type = !obelisk.ranged_packed_array<7 : 0 x !obelisk.integral<1, false, true, 0 : 0, logic>>} {
                    }
                    obelisk.sv.expression.conversion attributes {node_id = 24 : i64, semantic_type = !obelisk.ranged_packed_array<7 : 0 x !obelisk.integral<1, false, true, 0 : 0, logic>>} {
                      obelisk.sv.expression.conversion attributes {node_id = 25 : i64, semantic_type = !obelisk.ranged_packed_array<31 : 0 x !obelisk.integral<1, true, true, 0 : 0, logic>>} {
                        obelisk.sv.expression.integer_literal attributes {constant_value = "1", node_id = 26 : i64, semantic_type = !obelisk.integral<32, true, false, 31 : 0, int>} {
                        }
                      }
                    }
                  }
                }
              }
              obelisk.sv.statement.while_loop attributes {node_id = 27 : i64} {
                obelisk.sv.expression.named_value attributes {node_id = 28 : i64, referenced_path = "simulation_four_state.condition", referenced_symbol = @s1.$root::@s3.simulation_four_state::@s4.simulation_four_state::@s6.condition, semantic_type = !obelisk.integral<1, false, true, 0 : 0, logic>} {
                }
                obelisk.sv.statement.block attributes {node_id = 29 : i64} {
                  obelisk.sv.statement.list attributes {node_id = 30 : i64} {
                    obelisk.sv.statement.expression_statement attributes {node_id = 31 : i64} {
                      obelisk.sv.expression.assignment attributes {assignment_kind = 0 : i32, node_id = 32 : i64, semantic_type = !obelisk.ranged_packed_array<7 : 0 x !obelisk.integral<1, false, true, 0 : 0, logic>>} {
                        obelisk.sv.expression.named_value attributes {node_id = 33 : i64, referenced_path = "simulation_four_state.value", referenced_symbol = @s1.$root::@s3.simulation_four_state::@s4.simulation_four_state::@s7.value, semantic_type = !obelisk.ranged_packed_array<7 : 0 x !obelisk.integral<1, false, true, 0 : 0, logic>>} {
                        }
                        obelisk.sv.expression.conversion attributes {node_id = 34 : i64, semantic_type = !obelisk.ranged_packed_array<7 : 0 x !obelisk.integral<1, false, true, 0 : 0, logic>>} {
                          obelisk.sv.expression.binary_op attributes {node_id = 35 : i64, operator_kind = 0 : i32, semantic_type = !obelisk.ranged_packed_array<31 : 0 x !obelisk.integral<1, false, true, 0 : 0, logic>>} {
                            obelisk.sv.expression.conversion attributes {node_id = 36 : i64, semantic_type = !obelisk.ranged_packed_array<31 : 0 x !obelisk.integral<1, false, true, 0 : 0, logic>>} {
                              obelisk.sv.expression.named_value attributes {node_id = 37 : i64, referenced_path = "simulation_four_state.value", referenced_symbol = @s1.$root::@s3.simulation_four_state::@s4.simulation_four_state::@s7.value, semantic_type = !obelisk.ranged_packed_array<7 : 0 x !obelisk.integral<1, false, true, 0 : 0, logic>>} {
                              }
                            }
                            obelisk.sv.expression.conversion attributes {node_id = 38 : i64, semantic_type = !obelisk.ranged_packed_array<31 : 0 x !obelisk.integral<1, false, true, 0 : 0, logic>>} {
                              obelisk.sv.expression.integer_literal attributes {constant_value = "1", node_id = 39 : i64, semantic_type = !obelisk.integral<32, true, false, 31 : 0, int>} {
                              }
                            }
                          }
                        }
                      }
                    }
                    obelisk.sv.statement.break attributes {node_id = 40 : i64} {
                    }
                  }
                }
              }
              obelisk.sv.statement.expression_statement attributes {node_id = 41 : i64} {
                obelisk.sv.expression.assignment attributes {assignment_kind = 0 : i32, node_id = 42 : i64, semantic_type = !obelisk.integral<32, true, false, 31 : 0, int>} {
                  obelisk.sv.expression.named_value attributes {node_id = 43 : i64, referenced_path = "simulation_four_state.integer_index", referenced_symbol = @s1.$root::@s3.simulation_four_state::@s4.simulation_four_state::@s15.integer_index, semantic_type = !obelisk.integral<32, true, false, 31 : 0, int>} {
                  }
                  obelisk.sv.expression.integer_literal attributes {constant_value = "0", node_id = 44 : i64, semantic_type = !obelisk.integral<32, true, false, 31 : 0, int>} {
                  }
                }
              }
              obelisk.sv.statement.for_loop attributes {has_condition = true, initializer_count = 0 : i64, node_id = 45 : i64, step_count = 1 : i64} {
                obelisk.sv.expression.named_value attributes {node_id = 46 : i64, referenced_path = "simulation_four_state.condition", referenced_symbol = @s1.$root::@s3.simulation_four_state::@s4.simulation_four_state::@s6.condition, semantic_type = !obelisk.integral<1, false, true, 0 : 0, logic>} {
                }
                obelisk.sv.expression.unary_op attributes {node_id = 47 : i64, operator_kind = 12 : i32, semantic_type = !obelisk.integral<32, true, false, 31 : 0, int>} {
                  obelisk.sv.expression.named_value attributes {node_id = 48 : i64, referenced_path = "simulation_four_state.integer_index", referenced_symbol = @s1.$root::@s3.simulation_four_state::@s4.simulation_four_state::@s15.integer_index, semantic_type = !obelisk.integral<32, true, false, 31 : 0, int>} {
                  }
                }
                obelisk.sv.statement.block attributes {node_id = 49 : i64} {
                  obelisk.sv.statement.list attributes {node_id = 50 : i64} {
                    obelisk.sv.statement.expression_statement attributes {node_id = 51 : i64} {
                      obelisk.sv.expression.assignment attributes {assignment_kind = 0 : i32, node_id = 52 : i64, semantic_type = !obelisk.ranged_packed_array<7 : 0 x !obelisk.integral<1, false, true, 0 : 0, logic>>} {
                        obelisk.sv.expression.named_value attributes {node_id = 53 : i64, referenced_path = "simulation_four_state.value", referenced_symbol = @s1.$root::@s3.simulation_four_state::@s4.simulation_four_state::@s7.value, semantic_type = !obelisk.ranged_packed_array<7 : 0 x !obelisk.integral<1, false, true, 0 : 0, logic>>} {
                        }
                        obelisk.sv.expression.conversion attributes {node_id = 54 : i64, semantic_type = !obelisk.ranged_packed_array<7 : 0 x !obelisk.integral<1, false, true, 0 : 0, logic>>} {
                          obelisk.sv.expression.binary_op attributes {node_id = 55 : i64, operator_kind = 0 : i32, semantic_type = !obelisk.ranged_packed_array<31 : 0 x !obelisk.integral<1, false, true, 0 : 0, logic>>} {
                            obelisk.sv.expression.conversion attributes {node_id = 56 : i64, semantic_type = !obelisk.ranged_packed_array<31 : 0 x !obelisk.integral<1, false, true, 0 : 0, logic>>} {
                              obelisk.sv.expression.named_value attributes {node_id = 57 : i64, referenced_path = "simulation_four_state.value", referenced_symbol = @s1.$root::@s3.simulation_four_state::@s4.simulation_four_state::@s7.value, semantic_type = !obelisk.ranged_packed_array<7 : 0 x !obelisk.integral<1, false, true, 0 : 0, logic>>} {
                              }
                            }
                            obelisk.sv.expression.conversion attributes {node_id = 58 : i64, semantic_type = !obelisk.ranged_packed_array<31 : 0 x !obelisk.integral<1, false, true, 0 : 0, logic>>} {
                              obelisk.sv.expression.integer_literal attributes {constant_value = "1", node_id = 59 : i64, semantic_type = !obelisk.integral<32, true, false, 31 : 0, int>} {
                              }
                            }
                          }
                        }
                      }
                    }
                    obelisk.sv.statement.break attributes {node_id = 60 : i64} {
                    }
                  }
                }
              }
              obelisk.sv.statement.expression_statement attributes {node_id = 61 : i64} {
                obelisk.sv.expression.assignment attributes {assignment_kind = 0 : i32, node_id = 62 : i64, semantic_type = !obelisk.ranged_packed_array<7 : 0 x !obelisk.integral<1, false, false, 0 : 0, bit>>} {
                  obelisk.sv.expression.named_value attributes {node_id = 63 : i64, referenced_path = "simulation_four_state.bits", referenced_symbol = @s1.$root::@s3.simulation_four_state::@s4.simulation_four_state::@s10.bits, semantic_type = !obelisk.ranged_packed_array<7 : 0 x !obelisk.integral<1, false, false, 0 : 0, bit>>} {
                  }
                  obelisk.sv.expression.conversion attributes {node_id = 64 : i64, semantic_type = !obelisk.ranged_packed_array<7 : 0 x !obelisk.integral<1, false, false, 0 : 0, bit>>} {
                    obelisk.sv.expression.named_value attributes {node_id = 65 : i64, referenced_path = "simulation_four_state.four_state_input", referenced_symbol = @s1.$root::@s3.simulation_four_state::@s4.simulation_four_state::@s5.four_state_input, semantic_type = !obelisk.ranged_packed_array<7 : 0 x !obelisk.integral<1, false, true, 0 : 0, logic>>} {
                    }
                  }
                }
              }
              obelisk.sv.statement.expression_statement attributes {node_id = 66 : i64} {
                obelisk.sv.expression.assignment attributes {assignment_kind = 0 : i32, node_id = 67 : i64, semantic_type = !obelisk.ranged_packed_array<7 : 0 x !obelisk.integral<1, false, false, 0 : 0, bit>>} {
                  obelisk.sv.expression.named_value attributes {node_id = 68 : i64, referenced_path = "simulation_four_state.literal_bits", referenced_symbol = @s1.$root::@s3.simulation_four_state::@s4.simulation_four_state::@s11.literal_bits, semantic_type = !obelisk.ranged_packed_array<7 : 0 x !obelisk.integral<1, false, false, 0 : 0, bit>>} {
                  }
                  obelisk.sv.expression.conversion attributes {node_id = 69 : i64, semantic_type = !obelisk.ranged_packed_array<7 : 0 x !obelisk.integral<1, false, false, 0 : 0, bit>>} {
                    obelisk.sv.expression.integer_literal attributes {constant_value = "8'bxxzz0101", node_id = 70 : i64, semantic_type = !obelisk.ranged_packed_array<7 : 0 x !obelisk.integral<1, false, true, 0 : 0, logic>>} {
                    }
                  }
                }
              }
              obelisk.sv.statement.expression_statement attributes {node_id = 71 : i64} {
                obelisk.sv.expression.assignment attributes {assignment_kind = 0 : i32, node_id = 72 : i64, semantic_type = !obelisk.ranged_packed_array<3 : 0 x !obelisk.integral<1, false, true, 0 : 0, logic>>} {
                  obelisk.sv.expression.named_value attributes {node_id = 73 : i64, referenced_path = "simulation_four_state.logic_read", referenced_symbol = @s1.$root::@s3.simulation_four_state::@s4.simulation_four_state::@s9.logic_read, semantic_type = !obelisk.ranged_packed_array<3 : 0 x !obelisk.integral<1, false, true, 0 : 0, logic>>} {
                  }
                  obelisk.sv.expression.range_select attributes {node_id = 74 : i64, selection_kind = 1 : i32, semantic_type = !obelisk.ranged_packed_array<3 : 0 x !obelisk.integral<1, false, true, 0 : 0, logic>>} {
                    obelisk.sv.expression.named_value attributes {node_id = 75 : i64, referenced_path = "simulation_four_state.value", referenced_symbol = @s1.$root::@s3.simulation_four_state::@s4.simulation_four_state::@s7.value, semantic_type = !obelisk.ranged_packed_array<7 : 0 x !obelisk.integral<1, false, true, 0 : 0, logic>>} {
                    }
                    obelisk.sv.expression.named_value attributes {node_id = 76 : i64, referenced_path = "simulation_four_state.logic_index", referenced_symbol = @s1.$root::@s3.simulation_four_state::@s4.simulation_four_state::@s14.logic_index, semantic_type = !obelisk.ranged_packed_array<31 : 0 x !obelisk.integral<1, false, true, 0 : 0, logic>>} {
                    }
                    obelisk.sv.expression.integer_literal attributes {constant_value = "4", node_id = 77 : i64, semantic_type = !obelisk.integral<32, true, false, 31 : 0, int>} {
                    }
                  }
                }
              }
              obelisk.sv.statement.expression_statement attributes {node_id = 78 : i64} {
                obelisk.sv.expression.assignment attributes {assignment_kind = 0 : i32, node_id = 79 : i64, semantic_type = !obelisk.ranged_packed_array<3 : 0 x !obelisk.integral<1, false, true, 0 : 0, logic>>} {
                  obelisk.sv.expression.named_value attributes {node_id = 80 : i64, referenced_path = "simulation_four_state.logic_read", referenced_symbol = @s1.$root::@s3.simulation_four_state::@s4.simulation_four_state::@s9.logic_read, semantic_type = !obelisk.ranged_packed_array<3 : 0 x !obelisk.integral<1, false, true, 0 : 0, logic>>} {
                  }
                  obelisk.sv.expression.range_select attributes {node_id = 81 : i64, selection_kind = 1 : i32, semantic_type = !obelisk.ranged_packed_array<3 : 0 x !obelisk.integral<1, false, true, 0 : 0, logic>>} {
                    obelisk.sv.expression.named_value attributes {node_id = 82 : i64, referenced_path = "simulation_four_state.value", referenced_symbol = @s1.$root::@s3.simulation_four_state::@s4.simulation_four_state::@s7.value, semantic_type = !obelisk.ranged_packed_array<7 : 0 x !obelisk.integral<1, false, true, 0 : 0, logic>>} {
                    }
                    obelisk.sv.expression.named_value attributes {node_id = 83 : i64, referenced_path = "simulation_four_state.integer_index", referenced_symbol = @s1.$root::@s3.simulation_four_state::@s4.simulation_four_state::@s15.integer_index, semantic_type = !obelisk.integral<32, true, false, 31 : 0, int>} {
                    }
                    obelisk.sv.expression.integer_literal attributes {constant_value = "4", node_id = 84 : i64, semantic_type = !obelisk.integral<32, true, false, 31 : 0, int>} {
                    }
                  }
                }
              }
              obelisk.sv.statement.expression_statement attributes {node_id = 85 : i64} {
                obelisk.sv.expression.assignment attributes {assignment_kind = 0 : i32, node_id = 86 : i64, semantic_type = !obelisk.ranged_packed_array<3 : 0 x !obelisk.integral<1, false, true, 0 : 0, logic>>} {
                  obelisk.sv.expression.named_value attributes {node_id = 87 : i64, referenced_path = "simulation_four_state.logic_read", referenced_symbol = @s1.$root::@s3.simulation_four_state::@s4.simulation_four_state::@s9.logic_read, semantic_type = !obelisk.ranged_packed_array<3 : 0 x !obelisk.integral<1, false, true, 0 : 0, logic>>} {
                  }
                  obelisk.sv.expression.range_select attributes {node_id = 88 : i64, selection_kind = 1 : i32, semantic_type = !obelisk.ranged_packed_array<11 : 8 x !obelisk.integral<1, false, true, 0 : 0, logic>>} {
                    obelisk.sv.expression.named_value attributes {node_id = 89 : i64, referenced_path = "simulation_four_state.declared_range", referenced_symbol = @s1.$root::@s3.simulation_four_state::@s4.simulation_four_state::@s8.declared_range, semantic_type = !obelisk.ranged_packed_array<15 : 8 x !obelisk.integral<1, false, true, 0 : 0, logic>>} {
                    }
                    obelisk.sv.expression.named_value attributes {node_id = 90 : i64, referenced_path = "simulation_four_state.logic_index", referenced_symbol = @s1.$root::@s3.simulation_four_state::@s4.simulation_four_state::@s14.logic_index, semantic_type = !obelisk.ranged_packed_array<31 : 0 x !obelisk.integral<1, false, true, 0 : 0, logic>>} {
                    }
                    obelisk.sv.expression.integer_literal attributes {constant_value = "4", node_id = 91 : i64, semantic_type = !obelisk.integral<32, true, false, 31 : 0, int>} {
                    }
                  }
                }
              }
              obelisk.sv.statement.expression_statement attributes {node_id = 92 : i64} {
                obelisk.sv.expression.assignment attributes {assignment_kind = 0 : i32, node_id = 93 : i64, semantic_type = !obelisk.integral<1, false, false, 0 : 0, bit>} {
                  obelisk.sv.expression.named_value attributes {node_id = 94 : i64, referenced_path = "simulation_four_state.bit_read", referenced_symbol = @s1.$root::@s3.simulation_four_state::@s4.simulation_four_state::@s13.bit_read, semantic_type = !obelisk.integral<1, false, false, 0 : 0, bit>} {
                  }
                  obelisk.sv.expression.conversion attributes {node_id = 95 : i64, semantic_type = !obelisk.integral<1, false, false, 0 : 0, bit>} {
                    obelisk.sv.expression.element_select attributes {node_id = 96 : i64, semantic_type = !obelisk.integral<1, false, true, 0 : 0, logic>} {
                      obelisk.sv.expression.named_value attributes {node_id = 97 : i64, referenced_path = "simulation_four_state.value", referenced_symbol = @s1.$root::@s3.simulation_four_state::@s4.simulation_four_state::@s7.value, semantic_type = !obelisk.ranged_packed_array<7 : 0 x !obelisk.integral<1, false, true, 0 : 0, logic>>} {
                      }
                      obelisk.sv.expression.named_value attributes {node_id = 98 : i64, referenced_path = "simulation_four_state.logic_index", referenced_symbol = @s1.$root::@s3.simulation_four_state::@s4.simulation_four_state::@s14.logic_index, semantic_type = !obelisk.ranged_packed_array<31 : 0 x !obelisk.integral<1, false, true, 0 : 0, logic>>} {
                      }
                    }
                  }
                }
              }
              obelisk.sv.statement.expression_statement attributes {node_id = 99 : i64} {
                obelisk.sv.expression.assignment attributes {assignment_kind = 0 : i32, node_id = 100 : i64, semantic_type = !obelisk.integral<1, false, false, 0 : 0, bit>} {
                  obelisk.sv.expression.named_value attributes {node_id = 101 : i64, referenced_path = "simulation_four_state.bit_read", referenced_symbol = @s1.$root::@s3.simulation_four_state::@s4.simulation_four_state::@s13.bit_read, semantic_type = !obelisk.integral<1, false, false, 0 : 0, bit>} {
                  }
                  obelisk.sv.expression.conversion attributes {node_id = 102 : i64, semantic_type = !obelisk.integral<1, false, false, 0 : 0, bit>} {
                    obelisk.sv.expression.element_select attributes {node_id = 103 : i64, semantic_type = !obelisk.integral<1, false, true, 0 : 0, logic>} {
                      obelisk.sv.expression.named_value attributes {node_id = 104 : i64, referenced_path = "simulation_four_state.value", referenced_symbol = @s1.$root::@s3.simulation_four_state::@s4.simulation_four_state::@s7.value, semantic_type = !obelisk.ranged_packed_array<7 : 0 x !obelisk.integral<1, false, true, 0 : 0, logic>>} {
                      }
                      obelisk.sv.expression.named_value attributes {node_id = 105 : i64, referenced_path = "simulation_four_state.integer_index", referenced_symbol = @s1.$root::@s3.simulation_four_state::@s4.simulation_four_state::@s15.integer_index, semantic_type = !obelisk.integral<32, true, false, 31 : 0, int>} {
                      }
                    }
                  }
                }
              }
              obelisk.sv.statement.expression_statement attributes {node_id = 106 : i64} {
                obelisk.sv.expression.assignment attributes {assignment_kind = 0 : i32, node_id = 107 : i64, semantic_type = !obelisk.integral<1, false, true, 0 : 0, logic>} {
                  obelisk.sv.expression.element_select attributes {node_id = 108 : i64, semantic_type = !obelisk.integral<1, false, true, 0 : 0, logic>} {
                    obelisk.sv.expression.named_value attributes {node_id = 109 : i64, referenced_path = "simulation_four_state.value", referenced_symbol = @s1.$root::@s3.simulation_four_state::@s4.simulation_four_state::@s7.value, semantic_type = !obelisk.ranged_packed_array<7 : 0 x !obelisk.integral<1, false, true, 0 : 0, logic>>} {
                    }
                    obelisk.sv.expression.named_value attributes {node_id = 110 : i64, referenced_path = "simulation_four_state.logic_index", referenced_symbol = @s1.$root::@s3.simulation_four_state::@s4.simulation_four_state::@s14.logic_index, semantic_type = !obelisk.ranged_packed_array<31 : 0 x !obelisk.integral<1, false, true, 0 : 0, logic>>} {
                    }
                  }
                  obelisk.sv.expression.conversion attributes {node_id = 111 : i64, semantic_type = !obelisk.integral<1, false, true, 0 : 0, logic>} {
                    obelisk.sv.expression.integer_literal attributes {constant_value = "1'b1", node_id = 112 : i64, semantic_type = !obelisk.ranged_packed_array<0 : 0 x !obelisk.integral<1, false, false, 0 : 0, bit>>} {
                    }
                  }
                }
              }
              obelisk.sv.statement.expression_statement attributes {node_id = 113 : i64} {
                obelisk.sv.expression.assignment attributes {assignment_kind = 0 : i32, node_id = 114 : i64, semantic_type = !obelisk.integral<1, false, true, 0 : 0, logic>} {
                  obelisk.sv.expression.element_select attributes {node_id = 115 : i64, semantic_type = !obelisk.integral<1, false, true, 0 : 0, logic>} {
                    obelisk.sv.expression.named_value attributes {node_id = 116 : i64, referenced_path = "simulation_four_state.value", referenced_symbol = @s1.$root::@s3.simulation_four_state::@s4.simulation_four_state::@s7.value, semantic_type = !obelisk.ranged_packed_array<7 : 0 x !obelisk.integral<1, false, true, 0 : 0, logic>>} {
                    }
                    obelisk.sv.expression.named_value attributes {node_id = 117 : i64, referenced_path = "simulation_four_state.integer_index", referenced_symbol = @s1.$root::@s3.simulation_four_state::@s4.simulation_four_state::@s15.integer_index, semantic_type = !obelisk.integral<32, true, false, 31 : 0, int>} {
                    }
                  }
                  obelisk.sv.expression.conversion attributes {node_id = 118 : i64, semantic_type = !obelisk.integral<1, false, true, 0 : 0, logic>} {
                    obelisk.sv.expression.integer_literal attributes {constant_value = "1'b0", node_id = 119 : i64, semantic_type = !obelisk.ranged_packed_array<0 : 0 x !obelisk.integral<1, false, false, 0 : 0, bit>>} {
                    }
                  }
                }
              }
              obelisk.sv.statement.expression_statement attributes {node_id = 120 : i64} {
                obelisk.sv.expression.assignment attributes {assignment_kind = 0 : i32, node_id = 121 : i64, semantic_type = !obelisk.ranged_packed_array<3 : 0 x !obelisk.integral<1, false, true, 0 : 0, logic>>} {
                  obelisk.sv.expression.range_select attributes {node_id = 122 : i64, selection_kind = 1 : i32, semantic_type = !obelisk.ranged_packed_array<3 : 0 x !obelisk.integral<1, false, true, 0 : 0, logic>>} {
                    obelisk.sv.expression.named_value attributes {node_id = 123 : i64, referenced_path = "simulation_four_state.value", referenced_symbol = @s1.$root::@s3.simulation_four_state::@s4.simulation_four_state::@s7.value, semantic_type = !obelisk.ranged_packed_array<7 : 0 x !obelisk.integral<1, false, true, 0 : 0, logic>>} {
                    }
                    obelisk.sv.expression.named_value attributes {node_id = 124 : i64, referenced_path = "simulation_four_state.logic_index", referenced_symbol = @s1.$root::@s3.simulation_four_state::@s4.simulation_four_state::@s14.logic_index, semantic_type = !obelisk.ranged_packed_array<31 : 0 x !obelisk.integral<1, false, true, 0 : 0, logic>>} {
                    }
                    obelisk.sv.expression.integer_literal attributes {constant_value = "4", node_id = 125 : i64, semantic_type = !obelisk.integral<32, true, false, 31 : 0, int>} {
                    }
                  }
                  obelisk.sv.expression.named_value attributes {node_id = 126 : i64, referenced_path = "simulation_four_state.logic_read", referenced_symbol = @s1.$root::@s3.simulation_four_state::@s4.simulation_four_state::@s9.logic_read, semantic_type = !obelisk.ranged_packed_array<3 : 0 x !obelisk.integral<1, false, true, 0 : 0, logic>>} {
                  }
                }
              }
              obelisk.sv.statement.expression_statement attributes {node_id = 127 : i64} {
                obelisk.sv.expression.assignment attributes {assignment_kind = 0 : i32, node_id = 128 : i64, semantic_type = !obelisk.ranged_packed_array<3 : 0 x !obelisk.integral<1, false, true, 0 : 0, logic>>} {
                  obelisk.sv.expression.range_select attributes {node_id = 129 : i64, selection_kind = 1 : i32, semantic_type = !obelisk.ranged_packed_array<3 : 0 x !obelisk.integral<1, false, true, 0 : 0, logic>>} {
                    obelisk.sv.expression.named_value attributes {node_id = 130 : i64, referenced_path = "simulation_four_state.value", referenced_symbol = @s1.$root::@s3.simulation_four_state::@s4.simulation_four_state::@s7.value, semantic_type = !obelisk.ranged_packed_array<7 : 0 x !obelisk.integral<1, false, true, 0 : 0, logic>>} {
                    }
                    obelisk.sv.expression.named_value attributes {node_id = 131 : i64, referenced_path = "simulation_four_state.integer_index", referenced_symbol = @s1.$root::@s3.simulation_four_state::@s4.simulation_four_state::@s15.integer_index, semantic_type = !obelisk.integral<32, true, false, 31 : 0, int>} {
                    }
                    obelisk.sv.expression.integer_literal attributes {constant_value = "4", node_id = 132 : i64, semantic_type = !obelisk.integral<32, true, false, 31 : 0, int>} {
                    }
                  }
                  obelisk.sv.expression.named_value attributes {node_id = 133 : i64, referenced_path = "simulation_four_state.logic_read", referenced_symbol = @s1.$root::@s3.simulation_four_state::@s4.simulation_four_state::@s9.logic_read, semantic_type = !obelisk.ranged_packed_array<3 : 0 x !obelisk.integral<1, false, true, 0 : 0, logic>>} {
                  }
                }
              }
              obelisk.sv.statement.expression_statement attributes {node_id = 134 : i64} {
                obelisk.sv.expression.assignment attributes {assignment_kind = 0 : i32, node_id = 135 : i64, semantic_type = !obelisk.integral<1, false, false, 0 : 0, bit>} {
                  obelisk.sv.expression.named_value attributes {node_id = 136 : i64, referenced_path = "simulation_four_state.bit_read", referenced_symbol = @s1.$root::@s3.simulation_four_state::@s4.simulation_four_state::@s13.bit_read, semantic_type = !obelisk.integral<1, false, false, 0 : 0, bit>} {
                  }
                  obelisk.sv.expression.element_select attributes {node_id = 137 : i64, semantic_type = !obelisk.integral<1, false, false, 0 : 0, bit>} {
                    obelisk.sv.expression.named_value attributes {node_id = 138 : i64, referenced_path = "simulation_four_state.bits", referenced_symbol = @s1.$root::@s3.simulation_four_state::@s4.simulation_four_state::@s10.bits, semantic_type = !obelisk.ranged_packed_array<7 : 0 x !obelisk.integral<1, false, false, 0 : 0, bit>>} {
                    }
                    obelisk.sv.expression.named_value attributes {node_id = 139 : i64, referenced_path = "simulation_four_state.logic_index", referenced_symbol = @s1.$root::@s3.simulation_four_state::@s4.simulation_four_state::@s14.logic_index, semantic_type = !obelisk.ranged_packed_array<31 : 0 x !obelisk.integral<1, false, true, 0 : 0, logic>>} {
                    }
                  }
                }
              }
              obelisk.sv.statement.expression_statement attributes {node_id = 140 : i64} {
                obelisk.sv.expression.assignment attributes {assignment_kind = 0 : i32, node_id = 141 : i64, semantic_type = !obelisk.integral<1, false, false, 0 : 0, bit>} {
                  obelisk.sv.expression.named_value attributes {node_id = 142 : i64, referenced_path = "simulation_four_state.bit_read", referenced_symbol = @s1.$root::@s3.simulation_four_state::@s4.simulation_four_state::@s13.bit_read, semantic_type = !obelisk.integral<1, false, false, 0 : 0, bit>} {
                  }
                  obelisk.sv.expression.element_select attributes {node_id = 143 : i64, semantic_type = !obelisk.integral<1, false, false, 0 : 0, bit>} {
                    obelisk.sv.expression.named_value attributes {node_id = 144 : i64, referenced_path = "simulation_four_state.bits", referenced_symbol = @s1.$root::@s3.simulation_four_state::@s4.simulation_four_state::@s10.bits, semantic_type = !obelisk.ranged_packed_array<7 : 0 x !obelisk.integral<1, false, false, 0 : 0, bit>>} {
                    }
                    obelisk.sv.expression.named_value attributes {node_id = 145 : i64, referenced_path = "simulation_four_state.integer_index", referenced_symbol = @s1.$root::@s3.simulation_four_state::@s4.simulation_four_state::@s15.integer_index, semantic_type = !obelisk.integral<32, true, false, 31 : 0, int>} {
                    }
                  }
                }
              }
              obelisk.sv.statement.expression_statement attributes {node_id = 146 : i64} {
                obelisk.sv.expression.assignment attributes {assignment_kind = 0 : i32, node_id = 147 : i64, semantic_type = !obelisk.ranged_packed_array<3 : 0 x !obelisk.integral<1, false, false, 0 : 0, bit>>} {
                  obelisk.sv.expression.named_value attributes {node_id = 148 : i64, referenced_path = "simulation_four_state.bits_read", referenced_symbol = @s1.$root::@s3.simulation_four_state::@s4.simulation_four_state::@s12.bits_read, semantic_type = !obelisk.ranged_packed_array<3 : 0 x !obelisk.integral<1, false, false, 0 : 0, bit>>} {
                  }
                  obelisk.sv.expression.range_select attributes {node_id = 149 : i64, selection_kind = 1 : i32, semantic_type = !obelisk.ranged_packed_array<3 : 0 x !obelisk.integral<1, false, false, 0 : 0, bit>>} {
                    obelisk.sv.expression.named_value attributes {node_id = 150 : i64, referenced_path = "simulation_four_state.bits", referenced_symbol = @s1.$root::@s3.simulation_four_state::@s4.simulation_four_state::@s10.bits, semantic_type = !obelisk.ranged_packed_array<7 : 0 x !obelisk.integral<1, false, false, 0 : 0, bit>>} {
                    }
                    obelisk.sv.expression.named_value attributes {node_id = 151 : i64, referenced_path = "simulation_four_state.logic_index", referenced_symbol = @s1.$root::@s3.simulation_four_state::@s4.simulation_four_state::@s14.logic_index, semantic_type = !obelisk.ranged_packed_array<31 : 0 x !obelisk.integral<1, false, true, 0 : 0, logic>>} {
                    }
                    obelisk.sv.expression.integer_literal attributes {constant_value = "4", node_id = 152 : i64, semantic_type = !obelisk.integral<32, true, false, 31 : 0, int>} {
                    }
                  }
                }
              }
              obelisk.sv.statement.expression_statement attributes {node_id = 153 : i64} {
                obelisk.sv.expression.assignment attributes {assignment_kind = 0 : i32, node_id = 154 : i64, semantic_type = !obelisk.ranged_packed_array<3 : 0 x !obelisk.integral<1, false, false, 0 : 0, bit>>} {
                  obelisk.sv.expression.named_value attributes {node_id = 155 : i64, referenced_path = "simulation_four_state.bits_read", referenced_symbol = @s1.$root::@s3.simulation_four_state::@s4.simulation_four_state::@s12.bits_read, semantic_type = !obelisk.ranged_packed_array<3 : 0 x !obelisk.integral<1, false, false, 0 : 0, bit>>} {
                  }
                  obelisk.sv.expression.range_select attributes {node_id = 156 : i64, selection_kind = 1 : i32, semantic_type = !obelisk.ranged_packed_array<3 : 0 x !obelisk.integral<1, false, false, 0 : 0, bit>>} {
                    obelisk.sv.expression.named_value attributes {node_id = 157 : i64, referenced_path = "simulation_four_state.bits", referenced_symbol = @s1.$root::@s3.simulation_four_state::@s4.simulation_four_state::@s10.bits, semantic_type = !obelisk.ranged_packed_array<7 : 0 x !obelisk.integral<1, false, false, 0 : 0, bit>>} {
                    }
                    obelisk.sv.expression.named_value attributes {node_id = 158 : i64, referenced_path = "simulation_four_state.integer_index", referenced_symbol = @s1.$root::@s3.simulation_four_state::@s4.simulation_four_state::@s15.integer_index, semantic_type = !obelisk.integral<32, true, false, 31 : 0, int>} {
                    }
                    obelisk.sv.expression.integer_literal attributes {constant_value = "4", node_id = 159 : i64, semantic_type = !obelisk.integral<32, true, false, 31 : 0, int>} {
                    }
                  }
                }
              }
              obelisk.sv.statement.expression_statement attributes {node_id = 160 : i64} {
                obelisk.sv.expression.assignment attributes {assignment_kind = 0 : i32, node_id = 161 : i64, semantic_type = !obelisk.integral<1, false, false, 0 : 0, bit>} {
                  obelisk.sv.expression.named_value attributes {node_id = 162 : i64, referenced_path = "simulation_four_state.bit_read", referenced_symbol = @s1.$root::@s3.simulation_four_state::@s4.simulation_four_state::@s13.bit_read, semantic_type = !obelisk.integral<1, false, false, 0 : 0, bit>} {
                  }
                  obelisk.sv.expression.element_select attributes {node_id = 163 : i64, semantic_type = !obelisk.integral<1, false, false, 0 : 0, bit>} {
                    obelisk.sv.expression.named_value attributes {node_id = 164 : i64, referenced_path = "simulation_four_state.literal_bits", referenced_symbol = @s1.$root::@s3.simulation_four_state::@s4.simulation_four_state::@s11.literal_bits, semantic_type = !obelisk.ranged_packed_array<7 : 0 x !obelisk.integral<1, false, false, 0 : 0, bit>>} {
                    }
                    obelisk.sv.expression.named_value attributes {node_id = 165 : i64, referenced_path = "simulation_four_state.logic_index", referenced_symbol = @s1.$root::@s3.simulation_four_state::@s4.simulation_four_state::@s14.logic_index, semantic_type = !obelisk.ranged_packed_array<31 : 0 x !obelisk.integral<1, false, true, 0 : 0, logic>>} {
                    }
                  }
                }
              }
              obelisk.sv.statement.expression_statement attributes {node_id = 166 : i64} {
                obelisk.sv.expression.assignment attributes {assignment_kind = 0 : i32, node_id = 167 : i64, semantic_type = !obelisk.ranged_packed_array<31 : 0 x !obelisk.integral<1, false, true, 0 : 0, logic>>} {
                  obelisk.sv.expression.named_value attributes {node_id = 168 : i64, referenced_path = "simulation_four_state.logic_index", referenced_symbol = @s1.$root::@s3.simulation_four_state::@s4.simulation_four_state::@s14.logic_index, semantic_type = !obelisk.ranged_packed_array<31 : 0 x !obelisk.integral<1, false, true, 0 : 0, logic>>} {
                  }
                  obelisk.sv.expression.unbased_unsized_integer_literal attributes {constant_value = "32'bxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxx", node_id = 169 : i64, semantic_type = !obelisk.ranged_packed_array<31 : 0 x !obelisk.integral<1, false, true, 0 : 0, logic>>} {
                  }
                }
              }
              obelisk.sv.statement.expression_statement attributes {node_id = 170 : i64} {
                obelisk.sv.expression.assignment attributes {assignment_kind = 0 : i32, node_id = 171 : i64, semantic_type = !obelisk.ranged_packed_array<3 : 0 x !obelisk.integral<1, false, true, 0 : 0, logic>>} {
                  obelisk.sv.expression.named_value attributes {node_id = 172 : i64, referenced_path = "simulation_four_state.logic_read", referenced_symbol = @s1.$root::@s3.simulation_four_state::@s4.simulation_four_state::@s9.logic_read, semantic_type = !obelisk.ranged_packed_array<3 : 0 x !obelisk.integral<1, false, true, 0 : 0, logic>>} {
                  }
                  obelisk.sv.expression.range_select attributes {node_id = 173 : i64, selection_kind = 1 : i32, semantic_type = !obelisk.ranged_packed_array<3 : 0 x !obelisk.integral<1, false, true, 0 : 0, logic>>} {
                    obelisk.sv.expression.named_value attributes {node_id = 174 : i64, referenced_path = "simulation_four_state.value", referenced_symbol = @s1.$root::@s3.simulation_four_state::@s4.simulation_four_state::@s7.value, semantic_type = !obelisk.ranged_packed_array<7 : 0 x !obelisk.integral<1, false, true, 0 : 0, logic>>} {
                    }
                    obelisk.sv.expression.named_value attributes {node_id = 175 : i64, referenced_path = "simulation_four_state.logic_index", referenced_symbol = @s1.$root::@s3.simulation_four_state::@s4.simulation_four_state::@s14.logic_index, semantic_type = !obelisk.ranged_packed_array<31 : 0 x !obelisk.integral<1, false, true, 0 : 0, logic>>} {
                    }
                    obelisk.sv.expression.integer_literal attributes {constant_value = "4", node_id = 176 : i64, semantic_type = !obelisk.integral<32, true, false, 31 : 0, int>} {
                    }
                  }
                }
              }
              obelisk.sv.statement.expression_statement attributes {node_id = 177 : i64} {
                obelisk.sv.expression.assignment attributes {assignment_kind = 0 : i32, node_id = 178 : i64, semantic_type = !obelisk.integral<1, false, false, 0 : 0, bit>} {
                  obelisk.sv.expression.named_value attributes {node_id = 179 : i64, referenced_path = "simulation_four_state.bit_read", referenced_symbol = @s1.$root::@s3.simulation_four_state::@s4.simulation_four_state::@s13.bit_read, semantic_type = !obelisk.integral<1, false, false, 0 : 0, bit>} {
                  }
                  obelisk.sv.expression.element_select attributes {node_id = 180 : i64, semantic_type = !obelisk.integral<1, false, false, 0 : 0, bit>} {
                    obelisk.sv.expression.named_value attributes {node_id = 181 : i64, referenced_path = "simulation_four_state.bits", referenced_symbol = @s1.$root::@s3.simulation_four_state::@s4.simulation_four_state::@s10.bits, semantic_type = !obelisk.ranged_packed_array<7 : 0 x !obelisk.integral<1, false, false, 0 : 0, bit>>} {
                    }
                    obelisk.sv.expression.named_value attributes {node_id = 182 : i64, referenced_path = "simulation_four_state.logic_index", referenced_symbol = @s1.$root::@s3.simulation_four_state::@s4.simulation_four_state::@s14.logic_index, semantic_type = !obelisk.ranged_packed_array<31 : 0 x !obelisk.integral<1, false, true, 0 : 0, logic>>} {
                    }
                  }
                }
              }
              obelisk.sv.statement.expression_statement attributes {node_id = 183 : i64} {
                obelisk.sv.expression.assignment attributes {assignment_kind = 0 : i32, node_id = 184 : i64, semantic_type = !obelisk.integral<1, false, true, 0 : 0, logic>} {
                  obelisk.sv.expression.element_select attributes {node_id = 185 : i64, semantic_type = !obelisk.integral<1, false, true, 0 : 0, logic>} {
                    obelisk.sv.expression.named_value attributes {node_id = 186 : i64, referenced_path = "simulation_four_state.value", referenced_symbol = @s1.$root::@s3.simulation_four_state::@s4.simulation_four_state::@s7.value, semantic_type = !obelisk.ranged_packed_array<7 : 0 x !obelisk.integral<1, false, true, 0 : 0, logic>>} {
                    }
                    obelisk.sv.expression.named_value attributes {node_id = 187 : i64, referenced_path = "simulation_four_state.logic_index", referenced_symbol = @s1.$root::@s3.simulation_four_state::@s4.simulation_four_state::@s14.logic_index, semantic_type = !obelisk.ranged_packed_array<31 : 0 x !obelisk.integral<1, false, true, 0 : 0, logic>>} {
                    }
                  }
                  obelisk.sv.expression.conversion attributes {node_id = 188 : i64, semantic_type = !obelisk.integral<1, false, true, 0 : 0, logic>} {
                    obelisk.sv.expression.integer_literal attributes {constant_value = "1'b1", node_id = 189 : i64, semantic_type = !obelisk.ranged_packed_array<0 : 0 x !obelisk.integral<1, false, false, 0 : 0, bit>>} {
                    }
                  }
                }
              }
              obelisk.sv.statement.expression_statement attributes {node_id = 190 : i64} {
                obelisk.sv.expression.assignment attributes {assignment_kind = 0 : i32, node_id = 191 : i64, semantic_type = !obelisk.integral<32, true, false, 31 : 0, int>} {
                  obelisk.sv.expression.named_value attributes {node_id = 192 : i64, referenced_path = "simulation_four_state.integer_index", referenced_symbol = @s1.$root::@s3.simulation_four_state::@s4.simulation_four_state::@s15.integer_index, semantic_type = !obelisk.integral<32, true, false, 31 : 0, int>} {
                  }
                  obelisk.sv.expression.integer_literal attributes {constant_value = "6", node_id = 193 : i64, semantic_type = !obelisk.integral<32, true, false, 31 : 0, int>} {
                  }
                }
              }
              obelisk.sv.statement.expression_statement attributes {node_id = 194 : i64} {
                obelisk.sv.expression.assignment attributes {assignment_kind = 0 : i32, node_id = 195 : i64, semantic_type = !obelisk.ranged_packed_array<3 : 0 x !obelisk.integral<1, false, true, 0 : 0, logic>>} {
                  obelisk.sv.expression.named_value attributes {node_id = 196 : i64, referenced_path = "simulation_four_state.logic_read", referenced_symbol = @s1.$root::@s3.simulation_four_state::@s4.simulation_four_state::@s9.logic_read, semantic_type = !obelisk.ranged_packed_array<3 : 0 x !obelisk.integral<1, false, true, 0 : 0, logic>>} {
                  }
                  obelisk.sv.expression.range_select attributes {node_id = 197 : i64, selection_kind = 1 : i32, semantic_type = !obelisk.ranged_packed_array<3 : 0 x !obelisk.integral<1, false, true, 0 : 0, logic>>} {
                    obelisk.sv.expression.named_value attributes {node_id = 198 : i64, referenced_path = "simulation_four_state.value", referenced_symbol = @s1.$root::@s3.simulation_four_state::@s4.simulation_four_state::@s7.value, semantic_type = !obelisk.ranged_packed_array<7 : 0 x !obelisk.integral<1, false, true, 0 : 0, logic>>} {
                    }
                    obelisk.sv.expression.named_value attributes {node_id = 199 : i64, referenced_path = "simulation_four_state.integer_index", referenced_symbol = @s1.$root::@s3.simulation_four_state::@s4.simulation_four_state::@s15.integer_index, semantic_type = !obelisk.integral<32, true, false, 31 : 0, int>} {
                    }
                    obelisk.sv.expression.integer_literal attributes {constant_value = "4", node_id = 200 : i64, semantic_type = !obelisk.integral<32, true, false, 31 : 0, int>} {
                    }
                  }
                }
              }
              obelisk.sv.statement.expression_statement attributes {node_id = 201 : i64} {
                obelisk.sv.expression.assignment attributes {assignment_kind = 0 : i32, node_id = 202 : i64, semantic_type = !obelisk.integral<32, true, false, 31 : 0, int>} {
                  obelisk.sv.expression.named_value attributes {node_id = 203 : i64, referenced_path = "simulation_four_state.integer_index", referenced_symbol = @s1.$root::@s3.simulation_four_state::@s4.simulation_four_state::@s15.integer_index, semantic_type = !obelisk.integral<32, true, false, 31 : 0, int>} {
                  }
                  obelisk.sv.expression.integer_literal attributes {constant_value = "99", node_id = 204 : i64, semantic_type = !obelisk.integral<32, true, false, 31 : 0, int>} {
                  }
                }
              }
              obelisk.sv.statement.expression_statement attributes {node_id = 205 : i64} {
                obelisk.sv.expression.assignment attributes {assignment_kind = 0 : i32, node_id = 206 : i64, semantic_type = !obelisk.integral<1, false, true, 0 : 0, logic>} {
                  obelisk.sv.expression.element_select attributes {node_id = 207 : i64, semantic_type = !obelisk.integral<1, false, true, 0 : 0, logic>} {
                    obelisk.sv.expression.named_value attributes {node_id = 208 : i64, referenced_path = "simulation_four_state.value", referenced_symbol = @s1.$root::@s3.simulation_four_state::@s4.simulation_four_state::@s7.value, semantic_type = !obelisk.ranged_packed_array<7 : 0 x !obelisk.integral<1, false, true, 0 : 0, logic>>} {
                    }
                    obelisk.sv.expression.named_value attributes {node_id = 209 : i64, referenced_path = "simulation_four_state.integer_index", referenced_symbol = @s1.$root::@s3.simulation_four_state::@s4.simulation_four_state::@s15.integer_index, semantic_type = !obelisk.integral<32, true, false, 31 : 0, int>} {
                    }
                  }
                  obelisk.sv.expression.conversion attributes {node_id = 210 : i64, semantic_type = !obelisk.integral<1, false, true, 0 : 0, logic>} {
                    obelisk.sv.expression.integer_literal attributes {constant_value = "1'b0", node_id = 211 : i64, semantic_type = !obelisk.ranged_packed_array<0 : 0 x !obelisk.integral<1, false, false, 0 : 0, bit>>} {
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

// CHECK-DAG: obelisk_sim.logic.constant 0 : i32, -1 : i32
// CHECK-DAG: arith.constant 5 : i8
// CHECK-DAG: arith.constant 6 : i32
// CHECK-DAG: arith.constant 99 : i32
// CHECK-COUNT-3: obelisk_sim.logic.is_true
// CHECK: obelisk_sim.logic.to_bits
// CHECK-DAG: obelisk_sim.logic.binary sub {{.*}} : !obelisk_sim.logic<66>
// CHECK-DAG: obelisk_sim.logic.dyn_extract {{.*}} from {{.*}}!obelisk_sim.logic<66>
// CHECK-DAG: obelisk_sim.logic.dyn_extract {{.*}} from {{.*}}i66
// CHECK-DAG: obelisk_sim.ref.dyn_extract {{.*}} from {{.*}}!obelisk_sim.logic<66>
// CHECK-DAG: obelisk_sim.ref.dyn_extract {{.*}} from {{.*}}i66
// CHECK-DAG: obelisk_sim.bits.dyn_extract {{.*}} from {{.*}}!obelisk_sim.logic<66>
// CHECK-DAG: obelisk_sim.bits.dyn_extract {{.*}} from {{.*}}i66
// CHECK-NOT: obelisk.sv.
