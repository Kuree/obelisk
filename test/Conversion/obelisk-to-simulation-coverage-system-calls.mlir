// RUN: obelisk-opt %s '--lower-obelisk-to-sim=opt-level=0' | FileCheck %s

module {
  obelisk.sv.symbol.definition attributes {definition_kind = 0 : i32, hierarchical_name = "DUT", name = "DUT", node_id = 0 : i64, sym_name = "s0.DUT"} {
  }
  obelisk.sv.symbol.definition attributes {definition_kind = 0 : i32, hierarchical_name = "top", name = "top", node_id = 1 : i64, sym_name = "s1.top"} {
  }
  obelisk.sv.symbol.root attributes {hierarchical_name = "\\$root ", name = "$root", node_id = 2 : i64, sym_name = "s2.$root"} {
    obelisk.sv.symbol.compilation_unit attributes {hierarchical_name = "$unit", node_id = 3 : i64, sym_name = "s3"} {
    }
    obelisk.sv.symbol.instance attributes {hierarchical_name = "top", is_uninstantiated = false, name = "top", node_id = 4 : i64, referenced_path = "top", referenced_symbol = @s1.top, sym_name = "s4.top"} {
      obelisk.sv.symbol.instance_body attributes {hierarchical_name = "top", name = "top", node_id = 5 : i64, sym_name = "s5.top", time_precision_fs = 1000000 : i64, time_unit_fs = 1000000 : i64} {
        obelisk.sv.symbol.variable attributes {hierarchical_name = "top.i", lifetime = 1 : i32, name = "i", node_id = 6 : i64, semantic_type = !obelisk.integral<32, false, false, 31 : 0, int>, sym_name = "s6.i"} {
        }
        obelisk.sv.symbol.variable attributes {hierarchical_name = "top.r", lifetime = 1 : i32, name = "r", node_id = 7 : i64, semantic_type = !obelisk.real, sym_name = "s7.r"} {
        }
        obelisk.sv.symbol.instance attributes {hierarchical_name = "top.unit1", is_uninstantiated = false, name = "unit1", node_id = 8 : i64, referenced_path = "DUT", referenced_symbol = @s0.DUT, sym_name = "s8.unit1"} {
          obelisk.sv.symbol.instance_body attributes {hierarchical_name = "top.unit1", name = "DUT", node_id = 9 : i64, sym_name = "s9.DUT", time_precision_fs = 1000000 : i64, time_unit_fs = 1000000 : i64} {
          }
        }
        obelisk.sv.symbol.instance attributes {hierarchical_name = "top.unit2", is_uninstantiated = false, name = "unit2", node_id = 10 : i64, referenced_path = "DUT", referenced_symbol = @s0.DUT, sym_name = "s10.unit2"} {
          obelisk.sv.symbol.instance_body attributes {hierarchical_name = "top.unit2", name = "DUT", node_id = 11 : i64, sym_name = "s11.DUT", time_precision_fs = 1000000 : i64, time_unit_fs = 1000000 : i64} {
          }
        }
        obelisk.sv.symbol.procedural_block attributes {hierarchical_name = "top", node_id = 12 : i64, procedure_kind = 0 : i32, sym_name = "s12", time_precision_fs = 1000000 : i64, time_unit_fs = 1000000 : i64} {
          obelisk.sv.statement.block attributes {node_id = 13 : i64} {
            obelisk.sv.statement.list attributes {node_id = 14 : i64} {
              obelisk.sv.statement.expression_statement attributes {node_id = 15 : i64} {
                obelisk.sv.expression.assignment attributes {assignment_kind = 0 : i32, is_signed = false, node_id = 16 : i64, semantic_type = !obelisk.integral<32, false, false, 31 : 0, int>} {
                  obelisk.sv.expression.named_value attributes {is_signed = false, node_id = 17 : i64, referenced_path = "top.i", referenced_symbol = @s2.$root::@s4.top::@s5.top::@s6.i, semantic_type = !obelisk.integral<32, false, false, 31 : 0, int>} {
                  }
                  obelisk.sv.expression.conversion attributes {is_signed = false, node_id = 18 : i64, semantic_type = !obelisk.integral<32, false, false, 31 : 0, int>} {
                    obelisk.sv.expression.call attributes {argument_count = 4 : i64, callee_name = "$coverage_control", constraint_restrictions = [], defaulted_arguments = array<i64: 0, 0, 0, 0>, has_inline_constraints = false, has_iterator_expression = false, has_output_arguments = false, has_this_class = false, is_signed = true, is_super_class = false, is_system_call = true, node_id = 19 : i64, semantic_type = !obelisk.integral<32, true, false, 31 : 0, int>, subroutine_kind = 0 : i32, system_library_cell = "work.top", system_scope_path = "top", system_scope_symbol = @s2.$root::@s4.top::@s5.top} {
                      obelisk.sv.expression.integer_literal attributes {constant_value = "3", is_declared_unsized = true, is_signed = true, node_id = 20 : i64, semantic_type = !obelisk.integral<32, true, false, 31 : 0, int>} {
                      }
                      obelisk.sv.expression.integer_literal attributes {constant_value = "23", is_declared_unsized = true, is_signed = true, node_id = 21 : i64, semantic_type = !obelisk.integral<32, true, false, 31 : 0, int>} {
                      }
                      obelisk.sv.expression.integer_literal attributes {constant_value = "11", is_declared_unsized = true, is_signed = true, node_id = 22 : i64, semantic_type = !obelisk.integral<32, true, false, 31 : 0, int>} {
                      }
                      obelisk.sv.expression.arbitrary_symbol attributes {is_signed = false, node_id = 23 : i64, referenced_path = "\\$root ", referenced_symbol = @s2.$root, semantic_type = !obelisk.void} {
                      }
                    }
                  }
                }
              }
              obelisk.sv.statement.expression_statement attributes {node_id = 24 : i64} {
                obelisk.sv.expression.assignment attributes {assignment_kind = 0 : i32, is_signed = false, node_id = 25 : i64, semantic_type = !obelisk.integral<32, false, false, 31 : 0, int>} {
                  obelisk.sv.expression.named_value attributes {is_signed = false, node_id = 26 : i64, referenced_path = "top.i", referenced_symbol = @s2.$root::@s4.top::@s5.top::@s6.i, semantic_type = !obelisk.integral<32, false, false, 31 : 0, int>} {
                  }
                  obelisk.sv.expression.conversion attributes {is_signed = false, node_id = 27 : i64, semantic_type = !obelisk.integral<32, false, false, 31 : 0, int>} {
                    obelisk.sv.expression.call attributes {argument_count = 4 : i64, callee_name = "$coverage_control", constraint_restrictions = [], defaulted_arguments = array<i64: 0, 0, 0, 0>, has_inline_constraints = false, has_iterator_expression = false, has_output_arguments = false, has_this_class = false, is_signed = true, is_super_class = false, is_system_call = true, node_id = 28 : i64, semantic_type = !obelisk.integral<32, true, false, 31 : 0, int>, subroutine_kind = 0 : i32, system_library_cell = "work.top", system_scope_path = "top", system_scope_symbol = @s2.$root::@s4.top::@s5.top} {
                      obelisk.sv.expression.integer_literal attributes {constant_value = "2", is_declared_unsized = true, is_signed = true, node_id = 29 : i64, semantic_type = !obelisk.integral<32, true, false, 31 : 0, int>} {
                      }
                      obelisk.sv.expression.integer_literal attributes {constant_value = "23", is_declared_unsized = true, is_signed = true, node_id = 30 : i64, semantic_type = !obelisk.integral<32, true, false, 31 : 0, int>} {
                      }
                      obelisk.sv.expression.integer_literal attributes {constant_value = "10", is_declared_unsized = true, is_signed = true, node_id = 31 : i64, semantic_type = !obelisk.integral<32, true, false, 31 : 0, int>} {
                      }
                      obelisk.sv.expression.conversion attributes {is_signed = false, node_id = 32 : i64, semantic_type = !obelisk.string} {
                        obelisk.sv.expression.string_literal attributes {constant_value = "DUT", is_signed = false, node_id = 33 : i64, semantic_type = !obelisk.ranged_packed_array<23 : 0 x !obelisk.integral<1, false, false, 0 : 0, bit>>} {
                        }
                      }
                    }
                  }
                }
              }
              obelisk.sv.statement.expression_statement attributes {node_id = 34 : i64} {
                obelisk.sv.expression.assignment attributes {assignment_kind = 0 : i32, is_signed = false, node_id = 35 : i64, semantic_type = !obelisk.integral<32, false, false, 31 : 0, int>} {
                  obelisk.sv.expression.named_value attributes {is_signed = false, node_id = 36 : i64, referenced_path = "top.i", referenced_symbol = @s2.$root::@s4.top::@s5.top::@s6.i, semantic_type = !obelisk.integral<32, false, false, 31 : 0, int>} {
                  }
                  obelisk.sv.expression.conversion attributes {is_signed = false, node_id = 37 : i64, semantic_type = !obelisk.integral<32, false, false, 31 : 0, int>} {
                    obelisk.sv.expression.call attributes {argument_count = 4 : i64, callee_name = "$coverage_control", constraint_restrictions = [], defaulted_arguments = array<i64: 0, 0, 0, 0>, has_inline_constraints = false, has_iterator_expression = false, has_output_arguments = false, has_this_class = false, is_signed = true, is_super_class = false, is_system_call = true, node_id = 38 : i64, semantic_type = !obelisk.integral<32, true, false, 31 : 0, int>, subroutine_kind = 0 : i32, system_library_cell = "work.top", system_scope_path = "top", system_scope_symbol = @s2.$root::@s4.top::@s5.top} {
                      obelisk.sv.expression.integer_literal attributes {constant_value = "2", is_declared_unsized = true, is_signed = true, node_id = 39 : i64, semantic_type = !obelisk.integral<32, true, false, 31 : 0, int>} {
                      }
                      obelisk.sv.expression.integer_literal attributes {constant_value = "23", is_declared_unsized = true, is_signed = true, node_id = 40 : i64, semantic_type = !obelisk.integral<32, true, false, 31 : 0, int>} {
                      }
                      obelisk.sv.expression.integer_literal attributes {constant_value = "10", is_declared_unsized = true, is_signed = true, node_id = 41 : i64, semantic_type = !obelisk.integral<32, true, false, 31 : 0, int>} {
                      }
                      obelisk.sv.expression.arbitrary_symbol attributes {is_signed = false, node_id = 42 : i64, referenced_path = "top.unit1", referenced_symbol = @s2.$root::@s4.top::@s5.top::@s8.unit1, semantic_type = !obelisk.void} {
                      }
                    }
                  }
                }
              }
              obelisk.sv.statement.expression_statement attributes {node_id = 43 : i64} {
                obelisk.sv.expression.assignment attributes {assignment_kind = 0 : i32, is_signed = false, node_id = 44 : i64, semantic_type = !obelisk.integral<32, false, false, 31 : 0, int>} {
                  obelisk.sv.expression.named_value attributes {is_signed = false, node_id = 45 : i64, referenced_path = "top.i", referenced_symbol = @s2.$root::@s4.top::@s5.top::@s6.i, semantic_type = !obelisk.integral<32, false, false, 31 : 0, int>} {
                  }
                  obelisk.sv.expression.conversion attributes {is_signed = false, node_id = 46 : i64, semantic_type = !obelisk.integral<32, false, false, 31 : 0, int>} {
                    obelisk.sv.expression.call attributes {argument_count = 4 : i64, callee_name = "$coverage_control", constraint_restrictions = [], defaulted_arguments = array<i64: 0, 0, 0, 0>, has_inline_constraints = false, has_iterator_expression = false, has_output_arguments = false, has_this_class = false, is_signed = true, is_super_class = false, is_system_call = true, node_id = 47 : i64, semantic_type = !obelisk.integral<32, true, false, 31 : 0, int>, subroutine_kind = 0 : i32, system_library_cell = "work.top", system_scope_path = "top", system_scope_symbol = @s2.$root::@s4.top::@s5.top} {
                      obelisk.sv.expression.integer_literal attributes {constant_value = "1", is_declared_unsized = true, is_signed = true, node_id = 48 : i64, semantic_type = !obelisk.integral<32, true, false, 31 : 0, int>} {
                      }
                      obelisk.sv.expression.integer_literal attributes {constant_value = "23", is_declared_unsized = true, is_signed = true, node_id = 49 : i64, semantic_type = !obelisk.integral<32, true, false, 31 : 0, int>} {
                      }
                      obelisk.sv.expression.integer_literal attributes {constant_value = "11", is_declared_unsized = true, is_signed = true, node_id = 50 : i64, semantic_type = !obelisk.integral<32, true, false, 31 : 0, int>} {
                      }
                      obelisk.sv.expression.arbitrary_symbol attributes {is_signed = false, node_id = 51 : i64, referenced_path = "top.unit2", referenced_symbol = @s2.$root::@s4.top::@s5.top::@s10.unit2, semantic_type = !obelisk.void} {
                      }
                    }
                  }
                }
              }
              obelisk.sv.statement.expression_statement attributes {node_id = 52 : i64} {
                obelisk.sv.expression.assignment attributes {assignment_kind = 0 : i32, is_signed = false, node_id = 53 : i64, semantic_type = !obelisk.integral<32, false, false, 31 : 0, int>} {
                  obelisk.sv.expression.named_value attributes {is_signed = false, node_id = 54 : i64, referenced_path = "top.i", referenced_symbol = @s2.$root::@s4.top::@s5.top::@s6.i, semantic_type = !obelisk.integral<32, false, false, 31 : 0, int>} {
                  }
                  obelisk.sv.expression.conversion attributes {is_signed = false, node_id = 55 : i64, semantic_type = !obelisk.integral<32, false, false, 31 : 0, int>} {
                    obelisk.sv.expression.call attributes {argument_count = 4 : i64, callee_name = "$coverage_control", constraint_restrictions = [], defaulted_arguments = array<i64: 0, 0, 0, 0>, has_inline_constraints = false, has_iterator_expression = false, has_output_arguments = false, has_this_class = false, is_signed = true, is_super_class = false, is_system_call = true, node_id = 56 : i64, semantic_type = !obelisk.integral<32, true, false, 31 : 0, int>, subroutine_kind = 0 : i32, system_library_cell = "work.top", system_scope_path = "top", system_scope_symbol = @s2.$root::@s4.top::@s5.top} {
                      obelisk.sv.expression.integer_literal attributes {constant_value = "0", is_declared_unsized = true, is_signed = true, node_id = 57 : i64, semantic_type = !obelisk.integral<32, true, false, 31 : 0, int>} {
                      }
                      obelisk.sv.expression.integer_literal attributes {constant_value = "23", is_declared_unsized = true, is_signed = true, node_id = 58 : i64, semantic_type = !obelisk.integral<32, true, false, 31 : 0, int>} {
                      }
                      obelisk.sv.expression.integer_literal attributes {constant_value = "11", is_declared_unsized = true, is_signed = true, node_id = 59 : i64, semantic_type = !obelisk.integral<32, true, false, 31 : 0, int>} {
                      }
                      obelisk.sv.expression.conversion attributes {is_signed = false, node_id = 60 : i64, semantic_type = !obelisk.string} {
                        obelisk.sv.expression.string_literal attributes {constant_value = "DUT", is_signed = false, node_id = 61 : i64, semantic_type = !obelisk.ranged_packed_array<23 : 0 x !obelisk.integral<1, false, false, 0 : 0, bit>>} {
                        }
                      }
                    }
                  }
                }
              }
              obelisk.sv.statement.expression_statement attributes {node_id = 62 : i64} {
                obelisk.sv.expression.assignment attributes {assignment_kind = 0 : i32, is_signed = false, node_id = 63 : i64, semantic_type = !obelisk.integral<32, false, false, 31 : 0, int>} {
                  obelisk.sv.expression.named_value attributes {is_signed = false, node_id = 64 : i64, referenced_path = "top.i", referenced_symbol = @s2.$root::@s4.top::@s5.top::@s6.i, semantic_type = !obelisk.integral<32, false, false, 31 : 0, int>} {
                  }
                  obelisk.sv.expression.conversion attributes {is_signed = false, node_id = 65 : i64, semantic_type = !obelisk.integral<32, false, false, 31 : 0, int>} {
                    obelisk.sv.expression.call attributes {argument_count = 3 : i64, callee_name = "$coverage_get_max", constraint_restrictions = [], defaulted_arguments = array<i64: 0, 0, 0>, has_inline_constraints = false, has_iterator_expression = false, has_output_arguments = false, has_this_class = false, is_signed = true, is_super_class = false, is_system_call = true, node_id = 66 : i64, semantic_type = !obelisk.integral<32, true, false, 31 : 0, int>, subroutine_kind = 0 : i32, system_library_cell = "work.top", system_scope_path = "top", system_scope_symbol = @s2.$root::@s4.top::@s5.top} {
                      obelisk.sv.expression.integer_literal attributes {constant_value = "23", is_declared_unsized = true, is_signed = true, node_id = 67 : i64, semantic_type = !obelisk.integral<32, true, false, 31 : 0, int>} {
                      }
                      obelisk.sv.expression.integer_literal attributes {constant_value = "11", is_declared_unsized = true, is_signed = true, node_id = 68 : i64, semantic_type = !obelisk.integral<32, true, false, 31 : 0, int>} {
                      }
                      obelisk.sv.expression.conversion attributes {is_signed = false, node_id = 69 : i64, semantic_type = !obelisk.string} {
                        obelisk.sv.expression.string_literal attributes {constant_value = "DUT", is_signed = false, node_id = 70 : i64, semantic_type = !obelisk.ranged_packed_array<23 : 0 x !obelisk.integral<1, false, false, 0 : 0, bit>>} {
                        }
                      }
                    }
                  }
                }
              }
              obelisk.sv.statement.expression_statement attributes {node_id = 71 : i64} {
                obelisk.sv.expression.assignment attributes {assignment_kind = 0 : i32, is_signed = false, node_id = 72 : i64, semantic_type = !obelisk.real} {
                  obelisk.sv.expression.named_value attributes {is_signed = false, node_id = 73 : i64, referenced_path = "top.r", referenced_symbol = @s2.$root::@s4.top::@s5.top::@s7.r, semantic_type = !obelisk.real} {
                  }
                  obelisk.sv.expression.conversion attributes {is_signed = false, node_id = 74 : i64, semantic_type = !obelisk.real} {
                    obelisk.sv.expression.call attributes {argument_count = 3 : i64, callee_name = "$coverage_get", constraint_restrictions = [], defaulted_arguments = array<i64: 0, 0, 0>, has_inline_constraints = false, has_iterator_expression = false, has_output_arguments = false, has_this_class = false, is_signed = true, is_super_class = false, is_system_call = true, node_id = 75 : i64, semantic_type = !obelisk.integral<32, true, false, 31 : 0, int>, subroutine_kind = 0 : i32, system_library_cell = "work.top", system_scope_path = "top", system_scope_symbol = @s2.$root::@s4.top::@s5.top} {
                      obelisk.sv.expression.integer_literal attributes {constant_value = "22", is_declared_unsized = true, is_signed = true, node_id = 76 : i64, semantic_type = !obelisk.integral<32, true, false, 31 : 0, int>} {
                      }
                      obelisk.sv.expression.integer_literal attributes {constant_value = "11", is_declared_unsized = true, is_signed = true, node_id = 77 : i64, semantic_type = !obelisk.integral<32, true, false, 31 : 0, int>} {
                      }
                      obelisk.sv.expression.arbitrary_symbol attributes {is_signed = false, node_id = 78 : i64, referenced_path = "top.unit1", referenced_symbol = @s2.$root::@s4.top::@s5.top::@s8.unit1, semantic_type = !obelisk.void} {
                      }
                    }
                  }
                }
              }
              obelisk.sv.statement.expression_statement attributes {node_id = 79 : i64} {
                obelisk.sv.expression.assignment attributes {assignment_kind = 0 : i32, is_signed = false, node_id = 80 : i64, semantic_type = !obelisk.integral<32, false, false, 31 : 0, int>} {
                  obelisk.sv.expression.named_value attributes {is_signed = false, node_id = 81 : i64, referenced_path = "top.i", referenced_symbol = @s2.$root::@s4.top::@s5.top::@s6.i, semantic_type = !obelisk.integral<32, false, false, 31 : 0, int>} {
                  }
                  obelisk.sv.expression.conversion attributes {is_signed = false, node_id = 82 : i64, semantic_type = !obelisk.integral<32, false, false, 31 : 0, int>} {
                    obelisk.sv.expression.call attributes {argument_count = 2 : i64, callee_name = "$coverage_merge", constraint_restrictions = [], defaulted_arguments = array<i64: 0, 0>, has_inline_constraints = false, has_iterator_expression = false, has_output_arguments = false, has_this_class = false, is_signed = true, is_super_class = false, is_system_call = true, node_id = 83 : i64, semantic_type = !obelisk.integral<32, true, false, 31 : 0, int>, subroutine_kind = 0 : i32, system_library_cell = "work.top", system_scope_path = "top", system_scope_symbol = @s2.$root::@s4.top::@s5.top} {
                      obelisk.sv.expression.integer_literal attributes {constant_value = "20", is_declared_unsized = true, is_signed = true, node_id = 84 : i64, semantic_type = !obelisk.integral<32, true, false, 31 : 0, int>} {
                      }
                      obelisk.sv.expression.conversion attributes {is_signed = false, node_id = 85 : i64, semantic_type = !obelisk.string} {
                        obelisk.sv.expression.string_literal attributes {constant_value = "some_name", is_signed = false, node_id = 86 : i64, semantic_type = !obelisk.ranged_packed_array<71 : 0 x !obelisk.integral<1, false, false, 0 : 0, bit>>} {
                        }
                      }
                    }
                  }
                }
              }
              obelisk.sv.statement.expression_statement attributes {node_id = 87 : i64} {
                obelisk.sv.expression.assignment attributes {assignment_kind = 0 : i32, is_signed = false, node_id = 88 : i64, semantic_type = !obelisk.integral<32, false, false, 31 : 0, int>} {
                  obelisk.sv.expression.named_value attributes {is_signed = false, node_id = 89 : i64, referenced_path = "top.i", referenced_symbol = @s2.$root::@s4.top::@s5.top::@s6.i, semantic_type = !obelisk.integral<32, false, false, 31 : 0, int>} {
                  }
                  obelisk.sv.expression.conversion attributes {is_signed = false, node_id = 90 : i64, semantic_type = !obelisk.integral<32, false, false, 31 : 0, int>} {
                    obelisk.sv.expression.call attributes {argument_count = 2 : i64, callee_name = "$coverage_save", constraint_restrictions = [], defaulted_arguments = array<i64: 0, 0>, has_inline_constraints = false, has_iterator_expression = false, has_output_arguments = false, has_this_class = false, is_signed = true, is_super_class = false, is_system_call = true, node_id = 91 : i64, semantic_type = !obelisk.integral<32, true, false, 31 : 0, int>, subroutine_kind = 0 : i32, system_library_cell = "work.top", system_scope_path = "top", system_scope_symbol = @s2.$root::@s4.top::@s5.top} {
                      obelisk.sv.expression.integer_literal attributes {constant_value = "21", is_declared_unsized = true, is_signed = true, node_id = 92 : i64, semantic_type = !obelisk.integral<32, true, false, 31 : 0, int>} {
                      }
                      obelisk.sv.expression.conversion attributes {is_signed = false, node_id = 93 : i64, semantic_type = !obelisk.string} {
                        obelisk.sv.expression.string_literal attributes {constant_value = "some_name", is_signed = false, node_id = 94 : i64, semantic_type = !obelisk.ranged_packed_array<71 : 0 x !obelisk.integral<1, false, false, 0 : 0, bit>>} {
                        }
                      }
                    }
                  }
                }
              }
              obelisk.sv.statement.expression_statement attributes {node_id = 95 : i64} {
                obelisk.sv.expression.call attributes {argument_count = 1 : i64, callee_name = "$set_coverage_db_name", constraint_restrictions = [], defaulted_arguments = array<i64: 0>, has_inline_constraints = false, has_iterator_expression = false, has_output_arguments = false, has_this_class = false, is_signed = false, is_super_class = false, is_system_call = true, node_id = 96 : i64, semantic_type = !obelisk.void, subroutine_kind = 0 : i32, system_library_cell = "work.top", system_scope_path = "top", system_scope_symbol = @s2.$root::@s4.top::@s5.top} {
                  obelisk.sv.expression.conversion attributes {is_signed = false, node_id = 97 : i64, semantic_type = !obelisk.string} {
                    obelisk.sv.expression.string_literal attributes {constant_value = "coverage.db", is_signed = false, node_id = 98 : i64, semantic_type = !obelisk.ranged_packed_array<87 : 0 x !obelisk.integral<1, false, false, 0 : 0, bit>>} {
                    }
                  }
                }
              }
              obelisk.sv.statement.expression_statement attributes {node_id = 99 : i64} {
                obelisk.sv.expression.call attributes {argument_count = 1 : i64, callee_name = "$load_coverage_db", constraint_restrictions = [], defaulted_arguments = array<i64: 0>, has_inline_constraints = false, has_iterator_expression = false, has_output_arguments = false, has_this_class = false, is_signed = false, is_super_class = false, is_system_call = true, node_id = 100 : i64, semantic_type = !obelisk.void, subroutine_kind = 0 : i32, system_library_cell = "work.top", system_scope_path = "top", system_scope_symbol = @s2.$root::@s4.top::@s5.top} {
                  obelisk.sv.expression.conversion attributes {is_signed = false, node_id = 101 : i64, semantic_type = !obelisk.string} {
                    obelisk.sv.expression.string_literal attributes {constant_value = "coverage.db", is_signed = false, node_id = 102 : i64, semantic_type = !obelisk.ranged_packed_array<87 : 0 x !obelisk.integral<1, false, false, 0 : 0, bit>>} {
                    }
                  }
                }
              }
              obelisk.sv.statement.expression_statement attributes {node_id = 103 : i64} {
                obelisk.sv.expression.assignment attributes {assignment_kind = 0 : i32, is_signed = false, node_id = 104 : i64, semantic_type = !obelisk.real} {
                  obelisk.sv.expression.named_value attributes {is_signed = false, node_id = 105 : i64, referenced_path = "top.r", referenced_symbol = @s2.$root::@s4.top::@s5.top::@s7.r, semantic_type = !obelisk.real} {
                  }
                  obelisk.sv.expression.call attributes {argument_count = 0 : i64, callee_name = "$get_coverage", constraint_restrictions = [], defaulted_arguments = array<i64>, has_inline_constraints = false, has_iterator_expression = false, has_output_arguments = false, has_this_class = false, is_signed = false, is_super_class = false, is_system_call = true, node_id = 106 : i64, semantic_type = !obelisk.real, subroutine_kind = 0 : i32, system_library_cell = "work.top", system_scope_path = "top", system_scope_symbol = @s2.$root::@s4.top::@s5.top} {
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

// IEEE 1800-2017 40.3.2 status values for a simulator without code-coverage
// instrumentation. String module names are validated against elaborated
// definitions; hierarchical scope operands are selection syntax, not values.
// CHECK-LABEL: obelisk_sim.func private @{{unit_[0-9]+}}
// CHECK-DAG: %[[ZERO:.*]] = arith.constant 0 : i32
// CHECK-DAG: %[[ONE:.*]] = arith.constant 1 : i32
// CHECK-DAG: %[[ERROR:.*]] = arith.constant -1 : i32
// CHECK-DAG: %[[COVERAGE_ZERO:.*]] = arith.constant 0.000000e+00 : f64
// CHECK: obelisk_sim.ref.store %[[ZERO]]
// CHECK: %[[DUT_INPUT:.*]] = obelisk_sim.string.literal "DUT"
// CHECK: %[[DUT_NAME:.*]] = obelisk_sim.string.literal "DUT"
// CHECK-NOT: obelisk_sim.string.compare %[[DUT_INPUT]], %[[DUT_NAME]]
// CHECK: obelisk_sim.ref.store %[[ONE]]
// CHECK: obelisk_sim.ref.store %[[ONE]]
// CHECK: obelisk_sim.ref.store %[[ONE]]
// CHECK: obelisk_sim.ref.store
// CHECK: obelisk_sim.ref.store
// CHECK: obelisk_sim.real.from_integer %[[ZERO]] signed = true : i32 -> f64
// CHECK: obelisk_sim.ref.store %[[ERROR]]
// CHECK: obelisk_sim.ref.store %[[ZERO]]
// CHECK: obelisk_sim.ref.store %[[COVERAGE_ZERO]]
