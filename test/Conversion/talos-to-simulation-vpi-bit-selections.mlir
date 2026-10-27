// RUN: talos-opt %s '--lower-talos-to-sim=vpi=full opt-level=0' -o %t
// RUN: env TALOS_TEST_INPUT=%t %obj_root/test/talos-design-database-dump-test --gtest_filter=GeneratedDesignDatabase.SourceBitSelections
// RUN: talos-opt %s '--lower-talos-to-sim=vpi=full opt-level=3' -o %t.opt
// RUN: env TALOS_TEST_INPUT=%t.opt %obj_root/test/talos-design-database-dump-test --gtest_filter=GeneratedDesignDatabase.SourceBitSelections
// RUN: talos-opt %s '--lower-talos-to-sim=vpi=full opt-level=3' --convert-talos-sim-processes-to-llvm-coroutines -o /dev/null

// Authored source IR preserves each selection occurrence, the escaped parent
// identifier, the natural declaration types, and propagated index expression.
module attributes {coverage.language_version = 2023 : i32, llvm.data_layout = "e-p:64:64-i64:64-i32:32-i16:16-i8:8", llvm.target_triple = "x86_64-unknown-linux-gnu"} {
  talos.sv.symbol.definition @s0.top attributes {definition_kind = 0 : i32, hierarchical_name = "top", name = "top", node_id = 0 : i64} {
  }
  talos.sv.symbol.root @s1.$root attributes {hierarchical_name = "\\$root ", name = "$root", node_id = 1 : i64} {
    talos.sv.symbol.compilation_unit @s2 attributes {hierarchical_name = "$unit", node_id = 2 : i64, simulation.vpi_definition_name = "$unit"} {
    }
    talos.sv.symbol.instance @s3.top attributes {hierarchical_name = "top", is_uninstantiated = false, name = "top", node_id = 3 : i64, referenced_path = "top", referenced_symbol = @s0.top} {
      talos.sv.symbol.instance_body @s4.top attributes {hierarchical_name = "top", name = "top", node_id = 4 : i64, simulation.vpi_automatic = false, simulation.vpi_cell_instance = false, simulation.vpi_definition_name = "top", simulation.vpi_top = true, time_precision_fs = 1000000 : i64, time_unit_fs = 1000000 : i64, vpi_scope_kind = 32 : i32} {
        talos.sv.symbol.variable @s5.a.with.dot attributes {hierarchical_name = "top.\\a.with.dot ", lifetime = 1 : i32, name = "a.with.dot", node_id = 5 : i64, semantic_type = !talos.ranged_packed_array<7 : 0 x !talos.integral<1, false, false, 0 : 0, bit>>} {
        }
        talos.sv.symbol.variable @s6.l attributes {hierarchical_name = "top.l", lifetime = 1 : i32, name = "l", node_id = 6 : i64, semantic_type = !talos.ranged_packed_array<7 : 0 x !talos.integral<1, false, true, 0 : 0, logic>>} {
        }
        talos.sv.symbol.net @s7.n attributes {hierarchical_name = "top.\\n.with.dot ", is_implicit = false, name = "n.with.dot", net_kind = 1 : i32, node_id = 7 : i64, semantic_type = !talos.ranged_packed_array<7 : 0 x !talos.integral<1, false, true, 0 : 0, logic>>} {
          talos.sv.expression.conversion attributes {is_implicit = true, is_signed = false, node_id = 8 : i64, semantic_type = !talos.ranged_packed_array<7 : 0 x !talos.integral<1, false, true, 0 : 0, logic>>} {
            talos.sv.expression.named_value attributes {is_signed = false, node_id = 9 : i64, referenced_path = "top.\\a.with.dot ", referenced_symbol = @s1.$root::@s3.top::@s4.top::@s5.a.with.dot, semantic_type = !talos.ranged_packed_array<7 : 0 x !talos.integral<1, false, false, 0 : 0, bit>>} {
            }
          }
        }
        talos.sv.symbol.variable @s8.index attributes {hierarchical_name = "top.index", lifetime = 1 : i32, name = "index", node_id = 10 : i64, semantic_type = !talos.integral<32, true, true, 31 : 0, integer>} {
        }
        talos.sv.symbol.parameter @s9.P attributes {constant_value = "8'd165", has_explicit_range, hierarchical_name = "top.P", is_local_param, name = "P", node_id = 11 : i64, semantic_type = !talos.ranged_packed_array<7 : 0 x !talos.integral<1, false, false, 0 : 0, bit>>} {
          talos.sv.expression.integer_literal attributes {constant_value = "8'd165", is_signed = false, node_id = 12 : i64, semantic_type = !talos.ranged_packed_array<7 : 0 x !talos.integral<1, false, false, 0 : 0, bit>>} {
          }
          talos.sv.source.unsupported_default attributes {node_id = 13 : i64, reason = "original parameter default requires an unimplemented source form", syntax = " P = 8'ha5"} {
          }
        }
        talos.sv.symbol.variable @s10.constant_bit attributes {hierarchical_name = "top.constant_bit", lifetime = 1 : i32, name = "constant_bit", node_id = 14 : i64, semantic_type = !talos.integral<1, false, false, 0 : 0, bit>} {
        }
        talos.sv.symbol.variable @s11.dynamic_bit attributes {hierarchical_name = "top.dynamic_bit", lifetime = 1 : i32, name = "dynamic_bit", node_id = 15 : i64, semantic_type = !talos.integral<1, false, false, 0 : 0, bit>} {
        }
        talos.sv.symbol.variable @s12.parameter_bit attributes {hierarchical_name = "top.parameter_bit", lifetime = 1 : i32, name = "parameter_bit", node_id = 16 : i64, semantic_type = !talos.integral<1, false, false, 0 : 0, bit>} {
        }
        talos.sv.symbol.variable @s13.computed_bit attributes {hierarchical_name = "top.computed_bit", lifetime = 1 : i32, name = "computed_bit", node_id = 17 : i64, semantic_type = !talos.integral<1, false, false, 0 : 0, bit>} {
        }
        talos.sv.symbol.variable @s14.logic_bit attributes {hierarchical_name = "top.logic_bit", lifetime = 1 : i32, name = "logic_bit", node_id = 18 : i64, semantic_type = !talos.integral<1, false, true, 0 : 0, logic>} {
        }
        talos.sv.symbol.variable @s15.net_bit attributes {hierarchical_name = "top.net_bit", lifetime = 1 : i32, name = "net_bit", node_id = 19 : i64, semantic_type = !talos.integral<1, false, true, 0 : 0, logic>} {
        }
        talos.sv.symbol.procedural_block @s16 attributes {hierarchical_name = "top", node_id = 20 : i64, procedure_kind = 0 : i32, time_precision_fs = 1000000 : i64, time_unit_fs = 1000000 : i64} {
          talos.sv.statement.expression_statement attributes {node_id = 21 : i64} {
            talos.sv.expression.assignment attributes {assignment_kind = 0 : i32, is_signed = false, node_id = 22 : i64, semantic_type = !talos.integral<1, false, false, 0 : 0, bit>} {
              talos.sv.expression.named_value attributes {is_signed = false, node_id = 23 : i64, referenced_path = "top.constant_bit", referenced_symbol = @s1.$root::@s3.top::@s4.top::@s10.constant_bit, semantic_type = !talos.integral<1, false, false, 0 : 0, bit>} {
              }
              talos.sv.expression.element_select attributes {is_signed = false, node_id = 24 : i64, semantic_type = !talos.integral<1, false, false, 0 : 0, bit>} {
                talos.sv.expression.named_value attributes {is_signed = false, node_id = 25 : i64, referenced_path = "top.\\a.with.dot ", referenced_symbol = @s1.$root::@s3.top::@s4.top::@s5.a.with.dot, semantic_type = !talos.ranged_packed_array<7 : 0 x !talos.integral<1, false, false, 0 : 0, bit>>} {
                }
                talos.sv.expression.integer_literal attributes {constant_value = "7", is_declared_unsized = true, is_signed = true, node_id = 26 : i64, semantic_type = !talos.integral<32, true, false, 31 : 0, int>} {
                }
              }
            }
          }
        }
        talos.sv.symbol.procedural_block @s17 attributes {hierarchical_name = "top", node_id = 27 : i64, procedure_kind = 0 : i32, time_precision_fs = 1000000 : i64, time_unit_fs = 1000000 : i64} {
          talos.sv.statement.expression_statement attributes {node_id = 28 : i64} {
            talos.sv.expression.assignment attributes {assignment_kind = 0 : i32, is_signed = false, node_id = 29 : i64, semantic_type = !talos.integral<1, false, false, 0 : 0, bit>} {
              talos.sv.expression.named_value attributes {is_signed = false, node_id = 30 : i64, referenced_path = "top.computed_bit", referenced_symbol = @s1.$root::@s3.top::@s4.top::@s13.computed_bit, semantic_type = !talos.integral<1, false, false, 0 : 0, bit>} {
              }
              talos.sv.expression.element_select attributes {is_signed = false, node_id = 31 : i64, semantic_type = !talos.integral<1, false, false, 0 : 0, bit>} {
                talos.sv.expression.named_value attributes {is_signed = false, node_id = 32 : i64, referenced_path = "top.\\a.with.dot ", referenced_symbol = @s1.$root::@s3.top::@s4.top::@s5.a.with.dot, semantic_type = !talos.ranged_packed_array<7 : 0 x !talos.integral<1, false, false, 0 : 0, bit>>} {
                }
                talos.sv.expression.binary_op attributes {folded_constant = "7", is_signed = true, node_id = 33 : i64, operator_kind = 0 : i32, semantic_type = !talos.integral<32, true, false, 31 : 0, int>} {
                  talos.sv.expression.integer_literal attributes {constant_value = "3", is_declared_unsized = true, is_signed = true, node_id = 34 : i64, semantic_type = !talos.integral<32, true, false, 31 : 0, int>} {
                  }
                  talos.sv.expression.integer_literal attributes {constant_value = "4", is_declared_unsized = true, is_signed = true, node_id = 35 : i64, semantic_type = !talos.integral<32, true, false, 31 : 0, int>} {
                  }
                }
              }
            }
          }
        }
        talos.sv.symbol.procedural_block @s18 attributes {hierarchical_name = "top", node_id = 36 : i64, procedure_kind = 0 : i32, time_precision_fs = 1000000 : i64, time_unit_fs = 1000000 : i64} {
          talos.sv.statement.expression_statement attributes {node_id = 37 : i64} {
            talos.sv.expression.assignment attributes {assignment_kind = 0 : i32, is_signed = false, node_id = 38 : i64, semantic_type = !talos.integral<1, false, false, 0 : 0, bit>} {
              talos.sv.expression.named_value attributes {is_signed = false, node_id = 39 : i64, referenced_path = "top.dynamic_bit", referenced_symbol = @s1.$root::@s3.top::@s4.top::@s11.dynamic_bit, semantic_type = !talos.integral<1, false, false, 0 : 0, bit>} {
              }
              talos.sv.expression.element_select attributes {is_signed = false, node_id = 40 : i64, semantic_type = !talos.integral<1, false, false, 0 : 0, bit>} {
                talos.sv.expression.named_value attributes {is_signed = false, node_id = 41 : i64, referenced_path = "top.\\a.with.dot ", referenced_symbol = @s1.$root::@s3.top::@s4.top::@s5.a.with.dot, semantic_type = !talos.ranged_packed_array<7 : 0 x !talos.integral<1, false, false, 0 : 0, bit>>} {
                }
                talos.sv.expression.named_value attributes {is_signed = true, node_id = 42 : i64, referenced_path = "top.index", referenced_symbol = @s1.$root::@s3.top::@s4.top::@s8.index, semantic_type = !talos.integral<32, true, true, 31 : 0, integer>} {
                }
              }
            }
          }
        }
        talos.sv.symbol.procedural_block @s19 attributes {hierarchical_name = "top", node_id = 43 : i64, procedure_kind = 0 : i32, time_precision_fs = 1000000 : i64, time_unit_fs = 1000000 : i64} {
          talos.sv.statement.expression_statement attributes {node_id = 44 : i64} {
            talos.sv.expression.assignment attributes {assignment_kind = 0 : i32, is_signed = false, node_id = 45 : i64, semantic_type = !talos.integral<1, false, false, 0 : 0, bit>} {
              talos.sv.expression.named_value attributes {is_signed = false, node_id = 46 : i64, referenced_path = "top.parameter_bit", referenced_symbol = @s1.$root::@s3.top::@s4.top::@s12.parameter_bit, semantic_type = !talos.integral<1, false, false, 0 : 0, bit>} {
              }
              talos.sv.expression.element_select attributes {is_signed = false, node_id = 47 : i64, semantic_type = !talos.integral<1, false, false, 0 : 0, bit>} {
                talos.sv.expression.named_value attributes {is_signed = false, node_id = 48 : i64, referenced_path = "top.P", referenced_symbol = @s1.$root::@s3.top::@s4.top::@s9.P, semantic_type = !talos.ranged_packed_array<7 : 0 x !talos.integral<1, false, false, 0 : 0, bit>>} {
                }
                talos.sv.expression.integer_literal attributes {constant_value = "7", is_declared_unsized = true, is_signed = true, node_id = 49 : i64, semantic_type = !talos.integral<32, true, false, 31 : 0, int>} {
                }
              }
            }
          }
        }
        talos.sv.symbol.procedural_block @s20 attributes {hierarchical_name = "top", node_id = 50 : i64, procedure_kind = 0 : i32, time_precision_fs = 1000000 : i64, time_unit_fs = 1000000 : i64} {
          talos.sv.statement.expression_statement attributes {node_id = 51 : i64} {
            talos.sv.expression.assignment attributes {assignment_kind = 0 : i32, is_signed = false, node_id = 52 : i64, semantic_type = !talos.integral<1, false, true, 0 : 0, logic>} {
              talos.sv.expression.named_value attributes {is_signed = false, node_id = 53 : i64, referenced_path = "top.logic_bit", referenced_symbol = @s1.$root::@s3.top::@s4.top::@s14.logic_bit, semantic_type = !talos.integral<1, false, true, 0 : 0, logic>} {
              }
              talos.sv.expression.element_select attributes {is_signed = false, node_id = 54 : i64, semantic_type = !talos.integral<1, false, true, 0 : 0, logic>} {
                talos.sv.expression.named_value attributes {is_signed = false, node_id = 55 : i64, referenced_path = "top.l", referenced_symbol = @s1.$root::@s3.top::@s4.top::@s6.l, semantic_type = !talos.ranged_packed_array<7 : 0 x !talos.integral<1, false, true, 0 : 0, logic>>} {
                }
                talos.sv.expression.named_value attributes {is_signed = true, node_id = 56 : i64, referenced_path = "top.index", referenced_symbol = @s1.$root::@s3.top::@s4.top::@s8.index, semantic_type = !talos.integral<32, true, true, 31 : 0, integer>} {
                }
              }
            }
          }
        }
        talos.sv.symbol.procedural_block @s21 attributes {hierarchical_name = "top", node_id = 57 : i64, procedure_kind = 0 : i32, time_precision_fs = 1000000 : i64, time_unit_fs = 1000000 : i64} {
          talos.sv.statement.expression_statement attributes {node_id = 58 : i64} {
            talos.sv.expression.assignment attributes {assignment_kind = 0 : i32, is_signed = false, node_id = 59 : i64, semantic_type = !talos.integral<1, false, true, 0 : 0, logic>} {
              talos.sv.expression.named_value attributes {is_signed = false, node_id = 60 : i64, referenced_path = "top.net_bit", referenced_symbol = @s1.$root::@s3.top::@s4.top::@s15.net_bit, semantic_type = !talos.integral<1, false, true, 0 : 0, logic>} {
              }
              talos.sv.expression.element_select attributes {is_signed = false, node_id = 61 : i64, semantic_type = !talos.integral<1, false, true, 0 : 0, logic>} {
                talos.sv.expression.named_value attributes {is_signed = false, node_id = 62 : i64, referenced_path = "top.\\n.with.dot ", referenced_symbol = @s1.$root::@s3.top::@s4.top::@s7.n, semantic_type = !talos.ranged_packed_array<7 : 0 x !talos.integral<1, false, true, 0 : 0, logic>>} {
                }
                talos.sv.expression.named_value attributes {is_signed = true, node_id = 63 : i64, referenced_path = "top.index", referenced_symbol = @s1.$root::@s3.top::@s4.top::@s8.index, semantic_type = !talos.integral<32, true, true, 31 : 0, integer>} {
                }
              }
            }
          }
        }
      }
    }
  }
}
