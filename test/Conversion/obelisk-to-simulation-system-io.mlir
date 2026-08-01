// RUN: obelisk-opt %s '--lower-obelisk-to-sim=opt-level=0' | FileCheck %s

module {
  obelisk.sv.symbol.definition attributes {definition_kind = 0 : i32, hierarchical_name = "system_io", name = "system_io", node_id = 0 : i64, sym_name = "s0.system_io"} {
  }
  obelisk.sv.symbol.root attributes {hierarchical_name = "\\$root ", name = "$root", node_id = 1 : i64, sym_name = "s1.$root"} {
    obelisk.sv.symbol.compilation_unit attributes {hierarchical_name = "$unit", node_id = 2 : i64, sym_name = "s2"} {
    }
    obelisk.sv.symbol.instance attributes {hierarchical_name = "system_io", is_uninstantiated = false, name = "system_io", node_id = 3 : i64, referenced_path = "system_io", referenced_symbol = @s0.system_io, sym_name = "s3.system_io"} {
      obelisk.sv.symbol.instance_body attributes {hierarchical_name = "system_io", name = "system_io", node_id = 4 : i64, sym_name = "s4.system_io"} {
        obelisk.sv.symbol.variable attributes {hierarchical_name = "system_io.fd", lifetime = 1 : i32, name = "fd", node_id = 5 : i64, semantic_type = !obelisk.integral<32, true, true, 31 : 0, integer>, sym_name = "s5.fd"} {
        }
        obelisk.sv.symbol.variable attributes {hierarchical_name = "system_io.mcd", lifetime = 1 : i32, name = "mcd", node_id = 6 : i64, semantic_type = !obelisk.integral<32, true, true, 31 : 0, integer>, sym_name = "s6.mcd"} {
        }
        obelisk.sv.symbol.variable attributes {hierarchical_name = "system_io.code", lifetime = 1 : i32, name = "code", node_id = 7 : i64, semantic_type = !obelisk.integral<32, true, true, 31 : 0, integer>, sym_name = "s7.code"} {
        }
        obelisk.sv.symbol.variable attributes {hierarchical_name = "system_io.c", lifetime = 1 : i32, name = "c", node_id = 8 : i64, semantic_type = !obelisk.integral<32, true, true, 31 : 0, integer>, sym_name = "s8.c"} {
        }
        obelisk.sv.symbol.variable attributes {hierarchical_name = "system_io.line", lifetime = 1 : i32, name = "line", node_id = 9 : i64, semantic_type = !obelisk.ranged_packed_array<79 : 0 x !obelisk.integral<1, false, true, 0 : 0, reg>>, sym_name = "s9.line"} {
        }
        obelisk.sv.symbol.variable attributes {hierarchical_name = "system_io.data", lifetime = 1 : i32, name = "data", node_id = 10 : i64, semantic_type = !obelisk.ranged_packed_array<79 : 0 x !obelisk.integral<1, false, true, 0 : 0, reg>>, sym_name = "s10.data"} {
        }
        obelisk.sv.symbol.statement_block attributes {block_kind = 0 : i32, hierarchical_name = "system_io.named", name = "named", node_id = 11 : i64, sym_name = "s11.named"} {
        }
        obelisk.sv.symbol.procedural_block attributes {hierarchical_name = "system_io", node_id = 12 : i64, procedure_kind = 0 : i32, sym_name = "s12", time_precision_fs = 1000 : i64, time_unit_fs = 1000000 : i64} {
          obelisk.sv.statement.block attributes {node_id = 13 : i64} {
            obelisk.sv.statement.list attributes {node_id = 14 : i64} {
              obelisk.sv.statement.expression_statement attributes {node_id = 15 : i64} {
                obelisk.sv.expression.call attributes {argument_count = 2 : i64, callee_name = "$display", constraint_restrictions = [], defaulted_arguments = array<i64: 0, 0>, has_inline_constraints = false, has_iterator_expression = false, has_output_arguments = false, has_this_class = false, is_super_class = false, is_system_call = true, node_id = 16 : i64, semantic_type = !obelisk.void, subroutine_kind = 1 : i32, system_library_cell = "work.system_io", system_scope_path = "system_io", system_scope_symbol = @s1.$root::@s3.system_io::@s4.system_io} {
                  obelisk.sv.expression.string_literal attributes {constant_value = "value=%0h", node_id = 17 : i64, semantic_type = !obelisk.ranged_packed_array<71 : 0 x !obelisk.integral<1, false, false, 0 : 0, bit>>} {
                  }
                  obelisk.sv.expression.named_value attributes {node_id = 18 : i64, referenced_path = "system_io.data", referenced_symbol = @s1.$root::@s3.system_io::@s4.system_io::@s10.data, semantic_type = !obelisk.ranged_packed_array<79 : 0 x !obelisk.integral<1, false, true, 0 : 0, reg>>} {
                  }
                }
              }
              obelisk.sv.statement.expression_statement attributes {node_id = 19 : i64} {
                obelisk.sv.expression.call attributes {argument_count = 3 : i64, callee_name = "$display", constraint_restrictions = [], defaulted_arguments = array<i64: 0, 0, 0>, has_inline_constraints = false, has_iterator_expression = false, has_output_arguments = false, has_this_class = false, is_super_class = false, is_system_call = true, node_id = 20 : i64, semantic_type = !obelisk.void, subroutine_kind = 1 : i32, system_library_cell = "work.system_io", system_scope_path = "system_io", system_scope_symbol = @s1.$root::@s3.system_io::@s4.system_io} {
                  obelisk.sv.expression.string_literal attributes {constant_value = "%m %l", node_id = 21 : i64, semantic_type = !obelisk.ranged_packed_array<39 : 0 x !obelisk.integral<1, false, false, 0 : 0, bit>>} {
                  }
                  obelisk.sv.expression.empty_argument attributes {node_id = 22 : i64, semantic_type = !obelisk.void} {
                  }
                  obelisk.sv.expression.named_value attributes {node_id = 23 : i64, referenced_path = "system_io.data", referenced_symbol = @s1.$root::@s3.system_io::@s4.system_io::@s10.data, semantic_type = !obelisk.ranged_packed_array<79 : 0 x !obelisk.integral<1, false, true, 0 : 0, reg>>} {
                  }
                }
              }
              obelisk.sv.statement.block attributes {block_path = "system_io.named", block_symbol = @s1.$root::@s3.system_io::@s4.system_io::@s11.named, node_id = 24 : i64} {
                obelisk.sv.statement.expression_statement attributes {node_id = 25 : i64} {
                  obelisk.sv.expression.call attributes {argument_count = 2 : i64, callee_name = "$display", constraint_restrictions = [], defaulted_arguments = array<i64: 0, 0>, has_inline_constraints = false, has_iterator_expression = false, has_output_arguments = false, has_this_class = false, is_super_class = false, is_system_call = true, node_id = 26 : i64, semantic_type = !obelisk.void, subroutine_kind = 1 : i32, system_library_cell = "work.system_io", system_scope_path = "system_io.named", system_scope_symbol = @s1.$root::@s3.system_io::@s4.system_io::@s11.named} {
                    obelisk.sv.expression.string_literal attributes {constant_value = "%m %l %0t", node_id = 27 : i64, semantic_type = !obelisk.ranged_packed_array<71 : 0 x !obelisk.integral<1, false, false, 0 : 0, bit>>} {
                    }
                    obelisk.sv.expression.integer_literal attributes {constant_value = "2", node_id = 28 : i64, semantic_type = !obelisk.integral<32, true, false, 31 : 0, int>} {
                    }
                  }
                }
              }
              obelisk.sv.statement.expression_statement attributes {node_id = 29 : i64} {
                obelisk.sv.expression.call attributes {argument_count = 1 : i64, callee_name = "$displayb", constraint_restrictions = [], defaulted_arguments = array<i64: 0>, has_inline_constraints = false, has_iterator_expression = false, has_output_arguments = false, has_this_class = false, is_super_class = false, is_system_call = true, node_id = 30 : i64, semantic_type = !obelisk.void, subroutine_kind = 1 : i32, system_library_cell = "work.system_io", system_scope_path = "system_io", system_scope_symbol = @s1.$root::@s3.system_io::@s4.system_io} {
                  obelisk.sv.expression.named_value attributes {node_id = 31 : i64, referenced_path = "system_io.data", referenced_symbol = @s1.$root::@s3.system_io::@s4.system_io::@s10.data, semantic_type = !obelisk.ranged_packed_array<79 : 0 x !obelisk.integral<1, false, true, 0 : 0, reg>>} {
                  }
                }
              }
              obelisk.sv.statement.expression_statement attributes {node_id = 32 : i64} {
                obelisk.sv.expression.call attributes {argument_count = 1 : i64, callee_name = "$displayo", constraint_restrictions = [], defaulted_arguments = array<i64: 0>, has_inline_constraints = false, has_iterator_expression = false, has_output_arguments = false, has_this_class = false, is_super_class = false, is_system_call = true, node_id = 33 : i64, semantic_type = !obelisk.void, subroutine_kind = 1 : i32, system_library_cell = "work.system_io", system_scope_path = "system_io", system_scope_symbol = @s1.$root::@s3.system_io::@s4.system_io} {
                  obelisk.sv.expression.named_value attributes {node_id = 34 : i64, referenced_path = "system_io.data", referenced_symbol = @s1.$root::@s3.system_io::@s4.system_io::@s10.data, semantic_type = !obelisk.ranged_packed_array<79 : 0 x !obelisk.integral<1, false, true, 0 : 0, reg>>} {
                  }
                }
              }
              obelisk.sv.statement.expression_statement attributes {node_id = 35 : i64} {
                obelisk.sv.expression.call attributes {argument_count = 1 : i64, callee_name = "$displayh", constraint_restrictions = [], defaulted_arguments = array<i64: 0>, has_inline_constraints = false, has_iterator_expression = false, has_output_arguments = false, has_this_class = false, is_super_class = false, is_system_call = true, node_id = 36 : i64, semantic_type = !obelisk.void, subroutine_kind = 1 : i32, system_library_cell = "work.system_io", system_scope_path = "system_io", system_scope_symbol = @s1.$root::@s3.system_io::@s4.system_io} {
                  obelisk.sv.expression.named_value attributes {node_id = 37 : i64, referenced_path = "system_io.data", referenced_symbol = @s1.$root::@s3.system_io::@s4.system_io::@s10.data, semantic_type = !obelisk.ranged_packed_array<79 : 0 x !obelisk.integral<1, false, true, 0 : 0, reg>>} {
                  }
                }
              }
              obelisk.sv.statement.expression_statement attributes {node_id = 38 : i64} {
                obelisk.sv.expression.call attributes {argument_count = 1 : i64, callee_name = "$write", constraint_restrictions = [], defaulted_arguments = array<i64: 0>, has_inline_constraints = false, has_iterator_expression = false, has_output_arguments = false, has_this_class = false, is_super_class = false, is_system_call = true, node_id = 39 : i64, semantic_type = !obelisk.void, subroutine_kind = 1 : i32, system_library_cell = "work.system_io", system_scope_path = "system_io", system_scope_symbol = @s1.$root::@s3.system_io::@s4.system_io} {
                  obelisk.sv.expression.named_value attributes {node_id = 40 : i64, referenced_path = "system_io.data", referenced_symbol = @s1.$root::@s3.system_io::@s4.system_io::@s10.data, semantic_type = !obelisk.ranged_packed_array<79 : 0 x !obelisk.integral<1, false, true, 0 : 0, reg>>} {
                  }
                }
              }
              obelisk.sv.statement.expression_statement attributes {node_id = 41 : i64} {
                obelisk.sv.expression.call attributes {argument_count = 1 : i64, callee_name = "$writeb", constraint_restrictions = [], defaulted_arguments = array<i64: 0>, has_inline_constraints = false, has_iterator_expression = false, has_output_arguments = false, has_this_class = false, is_super_class = false, is_system_call = true, node_id = 42 : i64, semantic_type = !obelisk.void, subroutine_kind = 1 : i32, system_library_cell = "work.system_io", system_scope_path = "system_io", system_scope_symbol = @s1.$root::@s3.system_io::@s4.system_io} {
                  obelisk.sv.expression.named_value attributes {node_id = 43 : i64, referenced_path = "system_io.data", referenced_symbol = @s1.$root::@s3.system_io::@s4.system_io::@s10.data, semantic_type = !obelisk.ranged_packed_array<79 : 0 x !obelisk.integral<1, false, true, 0 : 0, reg>>} {
                  }
                }
              }
              obelisk.sv.statement.expression_statement attributes {node_id = 44 : i64} {
                obelisk.sv.expression.call attributes {argument_count = 1 : i64, callee_name = "$writeo", constraint_restrictions = [], defaulted_arguments = array<i64: 0>, has_inline_constraints = false, has_iterator_expression = false, has_output_arguments = false, has_this_class = false, is_super_class = false, is_system_call = true, node_id = 45 : i64, semantic_type = !obelisk.void, subroutine_kind = 1 : i32, system_library_cell = "work.system_io", system_scope_path = "system_io", system_scope_symbol = @s1.$root::@s3.system_io::@s4.system_io} {
                  obelisk.sv.expression.named_value attributes {node_id = 46 : i64, referenced_path = "system_io.data", referenced_symbol = @s1.$root::@s3.system_io::@s4.system_io::@s10.data, semantic_type = !obelisk.ranged_packed_array<79 : 0 x !obelisk.integral<1, false, true, 0 : 0, reg>>} {
                  }
                }
              }
              obelisk.sv.statement.expression_statement attributes {node_id = 47 : i64} {
                obelisk.sv.expression.call attributes {argument_count = 1 : i64, callee_name = "$writeh", constraint_restrictions = [], defaulted_arguments = array<i64: 0>, has_inline_constraints = false, has_iterator_expression = false, has_output_arguments = false, has_this_class = false, is_super_class = false, is_system_call = true, node_id = 48 : i64, semantic_type = !obelisk.void, subroutine_kind = 1 : i32, system_library_cell = "work.system_io", system_scope_path = "system_io", system_scope_symbol = @s1.$root::@s3.system_io::@s4.system_io} {
                  obelisk.sv.expression.named_value attributes {node_id = 49 : i64, referenced_path = "system_io.data", referenced_symbol = @s1.$root::@s3.system_io::@s4.system_io::@s10.data, semantic_type = !obelisk.ranged_packed_array<79 : 0 x !obelisk.integral<1, false, true, 0 : 0, reg>>} {
                  }
                }
              }
              obelisk.sv.statement.expression_statement attributes {node_id = 50 : i64} {
                obelisk.sv.expression.assignment attributes {assignment_kind = 0 : i32, node_id = 51 : i64, semantic_type = !obelisk.integral<32, true, true, 31 : 0, integer>} {
                  obelisk.sv.expression.named_value attributes {node_id = 52 : i64, referenced_path = "system_io.mcd", referenced_symbol = @s1.$root::@s3.system_io::@s4.system_io::@s6.mcd, semantic_type = !obelisk.integral<32, true, true, 31 : 0, integer>} {
                  }
                  obelisk.sv.expression.conversion attributes {node_id = 53 : i64, semantic_type = !obelisk.integral<32, true, true, 31 : 0, integer>} {
                    obelisk.sv.expression.call attributes {argument_count = 1 : i64, callee_name = "$fopen", constraint_restrictions = [], defaulted_arguments = array<i64: 0>, has_inline_constraints = false, has_iterator_expression = false, has_output_arguments = false, has_this_class = false, is_super_class = false, is_system_call = true, node_id = 54 : i64, semantic_type = !obelisk.integral<32, true, false, 31 : 0, int>, subroutine_kind = 0 : i32, system_library_cell = "work.system_io", system_scope_path = "system_io", system_scope_symbol = @s1.$root::@s3.system_io::@s4.system_io} {
                      obelisk.sv.expression.conversion attributes {node_id = 55 : i64, semantic_type = !obelisk.string} {
                        obelisk.sv.expression.string_literal attributes {constant_value = "out.log", node_id = 56 : i64, semantic_type = !obelisk.ranged_packed_array<55 : 0 x !obelisk.integral<1, false, false, 0 : 0, bit>>} {
                        }
                      }
                    }
                  }
                }
              }
              obelisk.sv.statement.expression_statement attributes {node_id = 57 : i64} {
                obelisk.sv.expression.assignment attributes {assignment_kind = 0 : i32, node_id = 58 : i64, semantic_type = !obelisk.integral<32, true, true, 31 : 0, integer>} {
                  obelisk.sv.expression.named_value attributes {node_id = 59 : i64, referenced_path = "system_io.fd", referenced_symbol = @s1.$root::@s3.system_io::@s4.system_io::@s5.fd, semantic_type = !obelisk.integral<32, true, true, 31 : 0, integer>} {
                  }
                  obelisk.sv.expression.conversion attributes {node_id = 60 : i64, semantic_type = !obelisk.integral<32, true, true, 31 : 0, integer>} {
                    obelisk.sv.expression.call attributes {argument_count = 2 : i64, callee_name = "$fopen", constraint_restrictions = [], defaulted_arguments = array<i64: 0, 0>, has_inline_constraints = false, has_iterator_expression = false, has_output_arguments = false, has_this_class = false, is_super_class = false, is_system_call = true, node_id = 61 : i64, semantic_type = !obelisk.integral<32, true, false, 31 : 0, int>, subroutine_kind = 0 : i32, system_library_cell = "work.system_io", system_scope_path = "system_io", system_scope_symbol = @s1.$root::@s3.system_io::@s4.system_io} {
                      obelisk.sv.expression.conversion attributes {node_id = 62 : i64, semantic_type = !obelisk.string} {
                        obelisk.sv.expression.string_literal attributes {constant_value = "input.bin", node_id = 63 : i64, semantic_type = !obelisk.ranged_packed_array<71 : 0 x !obelisk.integral<1, false, false, 0 : 0, bit>>} {
                        }
                      }
                      obelisk.sv.expression.conversion attributes {node_id = 64 : i64, semantic_type = !obelisk.string} {
                        obelisk.sv.expression.string_literal attributes {constant_value = "rb", node_id = 65 : i64, semantic_type = !obelisk.ranged_packed_array<15 : 0 x !obelisk.integral<1, false, false, 0 : 0, bit>>} {
                        }
                      }
                    }
                  }
                }
              }
              obelisk.sv.statement.expression_statement attributes {node_id = 66 : i64} {
                obelisk.sv.expression.call attributes {argument_count = 2 : i64, callee_name = "$fdisplay", constraint_restrictions = [], defaulted_arguments = array<i64: 0, 0>, has_inline_constraints = false, has_iterator_expression = false, has_output_arguments = false, has_this_class = false, is_super_class = false, is_system_call = true, node_id = 67 : i64, semantic_type = !obelisk.void, subroutine_kind = 1 : i32, system_library_cell = "work.system_io", system_scope_path = "system_io", system_scope_symbol = @s1.$root::@s3.system_io::@s4.system_io} {
                  obelisk.sv.expression.named_value attributes {node_id = 68 : i64, referenced_path = "system_io.fd", referenced_symbol = @s1.$root::@s3.system_io::@s4.system_io::@s5.fd, semantic_type = !obelisk.integral<32, true, true, 31 : 0, integer>} {
                  }
                  obelisk.sv.expression.named_value attributes {node_id = 69 : i64, referenced_path = "system_io.data", referenced_symbol = @s1.$root::@s3.system_io::@s4.system_io::@s10.data, semantic_type = !obelisk.ranged_packed_array<79 : 0 x !obelisk.integral<1, false, true, 0 : 0, reg>>} {
                  }
                }
              }
              obelisk.sv.statement.expression_statement attributes {node_id = 70 : i64} {
                obelisk.sv.expression.call attributes {argument_count = 2 : i64, callee_name = "$fdisplayb", constraint_restrictions = [], defaulted_arguments = array<i64: 0, 0>, has_inline_constraints = false, has_iterator_expression = false, has_output_arguments = false, has_this_class = false, is_super_class = false, is_system_call = true, node_id = 71 : i64, semantic_type = !obelisk.void, subroutine_kind = 1 : i32, system_library_cell = "work.system_io", system_scope_path = "system_io", system_scope_symbol = @s1.$root::@s3.system_io::@s4.system_io} {
                  obelisk.sv.expression.named_value attributes {node_id = 72 : i64, referenced_path = "system_io.fd", referenced_symbol = @s1.$root::@s3.system_io::@s4.system_io::@s5.fd, semantic_type = !obelisk.integral<32, true, true, 31 : 0, integer>} {
                  }
                  obelisk.sv.expression.named_value attributes {node_id = 73 : i64, referenced_path = "system_io.data", referenced_symbol = @s1.$root::@s3.system_io::@s4.system_io::@s10.data, semantic_type = !obelisk.ranged_packed_array<79 : 0 x !obelisk.integral<1, false, true, 0 : 0, reg>>} {
                  }
                }
              }
              obelisk.sv.statement.expression_statement attributes {node_id = 74 : i64} {
                obelisk.sv.expression.call attributes {argument_count = 2 : i64, callee_name = "$fdisplayo", constraint_restrictions = [], defaulted_arguments = array<i64: 0, 0>, has_inline_constraints = false, has_iterator_expression = false, has_output_arguments = false, has_this_class = false, is_super_class = false, is_system_call = true, node_id = 75 : i64, semantic_type = !obelisk.void, subroutine_kind = 1 : i32, system_library_cell = "work.system_io", system_scope_path = "system_io", system_scope_symbol = @s1.$root::@s3.system_io::@s4.system_io} {
                  obelisk.sv.expression.named_value attributes {node_id = 76 : i64, referenced_path = "system_io.fd", referenced_symbol = @s1.$root::@s3.system_io::@s4.system_io::@s5.fd, semantic_type = !obelisk.integral<32, true, true, 31 : 0, integer>} {
                  }
                  obelisk.sv.expression.named_value attributes {node_id = 77 : i64, referenced_path = "system_io.data", referenced_symbol = @s1.$root::@s3.system_io::@s4.system_io::@s10.data, semantic_type = !obelisk.ranged_packed_array<79 : 0 x !obelisk.integral<1, false, true, 0 : 0, reg>>} {
                  }
                }
              }
              obelisk.sv.statement.expression_statement attributes {node_id = 78 : i64} {
                obelisk.sv.expression.call attributes {argument_count = 3 : i64, callee_name = "$fdisplayh", constraint_restrictions = [], defaulted_arguments = array<i64: 0, 0, 0>, has_inline_constraints = false, has_iterator_expression = false, has_output_arguments = false, has_this_class = false, is_super_class = false, is_system_call = true, node_id = 79 : i64, semantic_type = !obelisk.void, subroutine_kind = 1 : i32, system_library_cell = "work.system_io", system_scope_path = "system_io", system_scope_symbol = @s1.$root::@s3.system_io::@s4.system_io} {
                  obelisk.sv.expression.named_value attributes {node_id = 80 : i64, referenced_path = "system_io.fd", referenced_symbol = @s1.$root::@s3.system_io::@s4.system_io::@s5.fd, semantic_type = !obelisk.integral<32, true, true, 31 : 0, integer>} {
                  }
                  obelisk.sv.expression.string_literal attributes {constant_value = "value=", node_id = 81 : i64, semantic_type = !obelisk.ranged_packed_array<47 : 0 x !obelisk.integral<1, false, false, 0 : 0, bit>>} {
                  }
                  obelisk.sv.expression.named_value attributes {node_id = 82 : i64, referenced_path = "system_io.data", referenced_symbol = @s1.$root::@s3.system_io::@s4.system_io::@s10.data, semantic_type = !obelisk.ranged_packed_array<79 : 0 x !obelisk.integral<1, false, true, 0 : 0, reg>>} {
                  }
                }
              }
              obelisk.sv.statement.expression_statement attributes {node_id = 83 : i64} {
                obelisk.sv.expression.call attributes {argument_count = 2 : i64, callee_name = "$fwrite", constraint_restrictions = [], defaulted_arguments = array<i64: 0, 0>, has_inline_constraints = false, has_iterator_expression = false, has_output_arguments = false, has_this_class = false, is_super_class = false, is_system_call = true, node_id = 84 : i64, semantic_type = !obelisk.void, subroutine_kind = 1 : i32, system_library_cell = "work.system_io", system_scope_path = "system_io", system_scope_symbol = @s1.$root::@s3.system_io::@s4.system_io} {
                  obelisk.sv.expression.named_value attributes {node_id = 85 : i64, referenced_path = "system_io.fd", referenced_symbol = @s1.$root::@s3.system_io::@s4.system_io::@s5.fd, semantic_type = !obelisk.integral<32, true, true, 31 : 0, integer>} {
                  }
                  obelisk.sv.expression.named_value attributes {node_id = 86 : i64, referenced_path = "system_io.data", referenced_symbol = @s1.$root::@s3.system_io::@s4.system_io::@s10.data, semantic_type = !obelisk.ranged_packed_array<79 : 0 x !obelisk.integral<1, false, true, 0 : 0, reg>>} {
                  }
                }
              }
              obelisk.sv.statement.expression_statement attributes {node_id = 87 : i64} {
                obelisk.sv.expression.call attributes {argument_count = 2 : i64, callee_name = "$fwriteb", constraint_restrictions = [], defaulted_arguments = array<i64: 0, 0>, has_inline_constraints = false, has_iterator_expression = false, has_output_arguments = false, has_this_class = false, is_super_class = false, is_system_call = true, node_id = 88 : i64, semantic_type = !obelisk.void, subroutine_kind = 1 : i32, system_library_cell = "work.system_io", system_scope_path = "system_io", system_scope_symbol = @s1.$root::@s3.system_io::@s4.system_io} {
                  obelisk.sv.expression.named_value attributes {node_id = 89 : i64, referenced_path = "system_io.fd", referenced_symbol = @s1.$root::@s3.system_io::@s4.system_io::@s5.fd, semantic_type = !obelisk.integral<32, true, true, 31 : 0, integer>} {
                  }
                  obelisk.sv.expression.named_value attributes {node_id = 90 : i64, referenced_path = "system_io.data", referenced_symbol = @s1.$root::@s3.system_io::@s4.system_io::@s10.data, semantic_type = !obelisk.ranged_packed_array<79 : 0 x !obelisk.integral<1, false, true, 0 : 0, reg>>} {
                  }
                }
              }
              obelisk.sv.statement.expression_statement attributes {node_id = 91 : i64} {
                obelisk.sv.expression.call attributes {argument_count = 2 : i64, callee_name = "$fwriteo", constraint_restrictions = [], defaulted_arguments = array<i64: 0, 0>, has_inline_constraints = false, has_iterator_expression = false, has_output_arguments = false, has_this_class = false, is_super_class = false, is_system_call = true, node_id = 92 : i64, semantic_type = !obelisk.void, subroutine_kind = 1 : i32, system_library_cell = "work.system_io", system_scope_path = "system_io", system_scope_symbol = @s1.$root::@s3.system_io::@s4.system_io} {
                  obelisk.sv.expression.named_value attributes {node_id = 93 : i64, referenced_path = "system_io.fd", referenced_symbol = @s1.$root::@s3.system_io::@s4.system_io::@s5.fd, semantic_type = !obelisk.integral<32, true, true, 31 : 0, integer>} {
                  }
                  obelisk.sv.expression.named_value attributes {node_id = 94 : i64, referenced_path = "system_io.data", referenced_symbol = @s1.$root::@s3.system_io::@s4.system_io::@s10.data, semantic_type = !obelisk.ranged_packed_array<79 : 0 x !obelisk.integral<1, false, true, 0 : 0, reg>>} {
                  }
                }
              }
              obelisk.sv.statement.expression_statement attributes {node_id = 95 : i64} {
                obelisk.sv.expression.call attributes {argument_count = 2 : i64, callee_name = "$fwriteh", constraint_restrictions = [], defaulted_arguments = array<i64: 0, 0>, has_inline_constraints = false, has_iterator_expression = false, has_output_arguments = false, has_this_class = false, is_super_class = false, is_system_call = true, node_id = 96 : i64, semantic_type = !obelisk.void, subroutine_kind = 1 : i32, system_library_cell = "work.system_io", system_scope_path = "system_io", system_scope_symbol = @s1.$root::@s3.system_io::@s4.system_io} {
                  obelisk.sv.expression.named_value attributes {node_id = 97 : i64, referenced_path = "system_io.fd", referenced_symbol = @s1.$root::@s3.system_io::@s4.system_io::@s5.fd, semantic_type = !obelisk.integral<32, true, true, 31 : 0, integer>} {
                  }
                  obelisk.sv.expression.named_value attributes {node_id = 98 : i64, referenced_path = "system_io.data", referenced_symbol = @s1.$root::@s3.system_io::@s4.system_io::@s10.data, semantic_type = !obelisk.ranged_packed_array<79 : 0 x !obelisk.integral<1, false, true, 0 : 0, reg>>} {
                  }
                }
              }
              obelisk.sv.statement.expression_statement attributes {node_id = 99 : i64} {
                obelisk.sv.expression.assignment attributes {assignment_kind = 0 : i32, node_id = 100 : i64, semantic_type = !obelisk.integral<32, true, true, 31 : 0, integer>} {
                  obelisk.sv.expression.named_value attributes {node_id = 101 : i64, referenced_path = "system_io.code", referenced_symbol = @s1.$root::@s3.system_io::@s4.system_io::@s7.code, semantic_type = !obelisk.integral<32, true, true, 31 : 0, integer>} {
                  }
                  obelisk.sv.expression.conversion attributes {node_id = 102 : i64, semantic_type = !obelisk.integral<32, true, true, 31 : 0, integer>} {
                    obelisk.sv.expression.call attributes {argument_count = 1 : i64, callee_name = "$fgetc", constraint_restrictions = [], defaulted_arguments = array<i64: 0>, has_inline_constraints = false, has_iterator_expression = false, has_output_arguments = false, has_this_class = false, is_super_class = false, is_system_call = true, node_id = 103 : i64, semantic_type = !obelisk.integral<32, true, false, 31 : 0, int>, subroutine_kind = 0 : i32, system_library_cell = "work.system_io", system_scope_path = "system_io", system_scope_symbol = @s1.$root::@s3.system_io::@s4.system_io} {
                      obelisk.sv.expression.conversion attributes {node_id = 104 : i64, semantic_type = !obelisk.integral<32, true, false, 31 : 0, int>} {
                        obelisk.sv.expression.named_value attributes {node_id = 105 : i64, referenced_path = "system_io.fd", referenced_symbol = @s1.$root::@s3.system_io::@s4.system_io::@s5.fd, semantic_type = !obelisk.integral<32, true, true, 31 : 0, integer>} {
                        }
                      }
                    }
                  }
                }
              }
              obelisk.sv.statement.expression_statement attributes {node_id = 106 : i64} {
                obelisk.sv.expression.assignment attributes {assignment_kind = 0 : i32, node_id = 107 : i64, semantic_type = !obelisk.integral<32, true, true, 31 : 0, integer>} {
                  obelisk.sv.expression.named_value attributes {node_id = 108 : i64, referenced_path = "system_io.code", referenced_symbol = @s1.$root::@s3.system_io::@s4.system_io::@s7.code, semantic_type = !obelisk.integral<32, true, true, 31 : 0, integer>} {
                  }
                  obelisk.sv.expression.conversion attributes {node_id = 109 : i64, semantic_type = !obelisk.integral<32, true, true, 31 : 0, integer>} {
                    obelisk.sv.expression.call attributes {argument_count = 2 : i64, callee_name = "$ungetc", constraint_restrictions = [], defaulted_arguments = array<i64: 0, 0>, has_inline_constraints = false, has_iterator_expression = false, has_output_arguments = false, has_this_class = false, is_super_class = false, is_system_call = true, node_id = 110 : i64, semantic_type = !obelisk.integral<32, true, false, 31 : 0, int>, subroutine_kind = 0 : i32, system_library_cell = "work.system_io", system_scope_path = "system_io", system_scope_symbol = @s1.$root::@s3.system_io::@s4.system_io} {
                      obelisk.sv.expression.conversion attributes {node_id = 111 : i64, semantic_type = !obelisk.integral<32, true, false, 31 : 0, int>} {
                        obelisk.sv.expression.named_value attributes {node_id = 112 : i64, referenced_path = "system_io.c", referenced_symbol = @s1.$root::@s3.system_io::@s4.system_io::@s8.c, semantic_type = !obelisk.integral<32, true, true, 31 : 0, integer>} {
                        }
                      }
                      obelisk.sv.expression.conversion attributes {node_id = 113 : i64, semantic_type = !obelisk.integral<32, true, false, 31 : 0, int>} {
                        obelisk.sv.expression.named_value attributes {node_id = 114 : i64, referenced_path = "system_io.fd", referenced_symbol = @s1.$root::@s3.system_io::@s4.system_io::@s5.fd, semantic_type = !obelisk.integral<32, true, true, 31 : 0, integer>} {
                        }
                      }
                    }
                  }
                }
              }
              obelisk.sv.statement.expression_statement attributes {node_id = 115 : i64} {
                obelisk.sv.expression.assignment attributes {assignment_kind = 0 : i32, node_id = 116 : i64, semantic_type = !obelisk.integral<32, true, true, 31 : 0, integer>} {
                  obelisk.sv.expression.named_value attributes {node_id = 117 : i64, referenced_path = "system_io.code", referenced_symbol = @s1.$root::@s3.system_io::@s4.system_io::@s7.code, semantic_type = !obelisk.integral<32, true, true, 31 : 0, integer>} {
                  }
                  obelisk.sv.expression.call attributes {argument_count = 2 : i64, callee_name = "$fgets", constraint_restrictions = [], defaulted_arguments = array<i64: 0, 0>, has_inline_constraints = false, has_iterator_expression = false, has_output_arguments = true, has_this_class = false, is_super_class = false, is_system_call = true, node_id = 118 : i64, semantic_type = !obelisk.integral<32, true, true, 31 : 0, integer>, subroutine_kind = 0 : i32, system_library_cell = "work.system_io", system_scope_path = "system_io", system_scope_symbol = @s1.$root::@s3.system_io::@s4.system_io} {
                    obelisk.sv.expression.assignment attributes {assignment_kind = 0 : i32, node_id = 119 : i64, semantic_type = !obelisk.ranged_packed_array<79 : 0 x !obelisk.integral<1, false, true, 0 : 0, reg>>} {
                      obelisk.sv.expression.named_value attributes {node_id = 120 : i64, referenced_path = "system_io.line", referenced_symbol = @s1.$root::@s3.system_io::@s4.system_io::@s9.line, semantic_type = !obelisk.ranged_packed_array<79 : 0 x !obelisk.integral<1, false, true, 0 : 0, reg>>} {
                      }
                      obelisk.sv.expression.empty_argument attributes {node_id = 121 : i64, semantic_type = !obelisk.ranged_packed_array<79 : 0 x !obelisk.integral<1, false, true, 0 : 0, reg>>} {
                      }
                    }
                    obelisk.sv.expression.named_value attributes {node_id = 122 : i64, referenced_path = "system_io.fd", referenced_symbol = @s1.$root::@s3.system_io::@s4.system_io::@s5.fd, semantic_type = !obelisk.integral<32, true, true, 31 : 0, integer>} {
                    }
                  }
                }
              }
              obelisk.sv.statement.expression_statement attributes {node_id = 123 : i64} {
                obelisk.sv.expression.assignment attributes {assignment_kind = 0 : i32, node_id = 124 : i64, semantic_type = !obelisk.integral<32, true, true, 31 : 0, integer>} {
                  obelisk.sv.expression.named_value attributes {node_id = 125 : i64, referenced_path = "system_io.code", referenced_symbol = @s1.$root::@s3.system_io::@s4.system_io::@s7.code, semantic_type = !obelisk.integral<32, true, true, 31 : 0, integer>} {
                  }
                  obelisk.sv.expression.call attributes {argument_count = 2 : i64, callee_name = "$fread", constraint_restrictions = [], defaulted_arguments = array<i64: 0, 0>, has_inline_constraints = false, has_iterator_expression = false, has_output_arguments = true, has_this_class = false, is_super_class = false, is_system_call = true, node_id = 126 : i64, semantic_type = !obelisk.integral<32, true, true, 31 : 0, integer>, subroutine_kind = 0 : i32, system_library_cell = "work.system_io", system_scope_path = "system_io", system_scope_symbol = @s1.$root::@s3.system_io::@s4.system_io} {
                    obelisk.sv.expression.assignment attributes {assignment_kind = 0 : i32, node_id = 127 : i64, semantic_type = !obelisk.ranged_packed_array<79 : 0 x !obelisk.integral<1, false, true, 0 : 0, reg>>} {
                      obelisk.sv.expression.named_value attributes {node_id = 128 : i64, referenced_path = "system_io.data", referenced_symbol = @s1.$root::@s3.system_io::@s4.system_io::@s10.data, semantic_type = !obelisk.ranged_packed_array<79 : 0 x !obelisk.integral<1, false, true, 0 : 0, reg>>} {
                      }
                      obelisk.sv.expression.empty_argument attributes {node_id = 129 : i64, semantic_type = !obelisk.ranged_packed_array<79 : 0 x !obelisk.integral<1, false, true, 0 : 0, reg>>} {
                      }
                    }
                    obelisk.sv.expression.named_value attributes {node_id = 130 : i64, referenced_path = "system_io.fd", referenced_symbol = @s1.$root::@s3.system_io::@s4.system_io::@s5.fd, semantic_type = !obelisk.integral<32, true, true, 31 : 0, integer>} {
                    }
                  }
                }
              }
              obelisk.sv.statement.expression_statement attributes {node_id = 131 : i64} {
                obelisk.sv.expression.assignment attributes {assignment_kind = 0 : i32, node_id = 132 : i64, semantic_type = !obelisk.integral<32, true, true, 31 : 0, integer>} {
                  obelisk.sv.expression.named_value attributes {node_id = 133 : i64, referenced_path = "system_io.code", referenced_symbol = @s1.$root::@s3.system_io::@s4.system_io::@s7.code, semantic_type = !obelisk.integral<32, true, true, 31 : 0, integer>} {
                  }
                  obelisk.sv.expression.conversion attributes {node_id = 134 : i64, semantic_type = !obelisk.integral<32, true, true, 31 : 0, integer>} {
                    obelisk.sv.expression.call attributes {argument_count = 1 : i64, callee_name = "$feof", constraint_restrictions = [], defaulted_arguments = array<i64: 0>, has_inline_constraints = false, has_iterator_expression = false, has_output_arguments = false, has_this_class = false, is_super_class = false, is_system_call = true, node_id = 135 : i64, semantic_type = !obelisk.integral<32, true, false, 31 : 0, int>, subroutine_kind = 0 : i32, system_library_cell = "work.system_io", system_scope_path = "system_io", system_scope_symbol = @s1.$root::@s3.system_io::@s4.system_io} {
                      obelisk.sv.expression.conversion attributes {node_id = 136 : i64, semantic_type = !obelisk.integral<32, true, false, 31 : 0, int>} {
                        obelisk.sv.expression.named_value attributes {node_id = 137 : i64, referenced_path = "system_io.fd", referenced_symbol = @s1.$root::@s3.system_io::@s4.system_io::@s5.fd, semantic_type = !obelisk.integral<32, true, true, 31 : 0, integer>} {
                        }
                      }
                    }
                  }
                }
              }
              obelisk.sv.statement.expression_statement attributes {node_id = 138 : i64} {
                obelisk.sv.expression.assignment attributes {assignment_kind = 0 : i32, node_id = 139 : i64, semantic_type = !obelisk.integral<32, true, true, 31 : 0, integer>} {
                  obelisk.sv.expression.named_value attributes {node_id = 140 : i64, referenced_path = "system_io.code", referenced_symbol = @s1.$root::@s3.system_io::@s4.system_io::@s7.code, semantic_type = !obelisk.integral<32, true, true, 31 : 0, integer>} {
                  }
                  obelisk.sv.expression.conversion attributes {node_id = 141 : i64, semantic_type = !obelisk.integral<32, true, true, 31 : 0, integer>} {
                    obelisk.sv.expression.call attributes {argument_count = 3 : i64, callee_name = "$fseek", constraint_restrictions = [], defaulted_arguments = array<i64: 0, 0, 0>, has_inline_constraints = false, has_iterator_expression = false, has_output_arguments = false, has_this_class = false, is_super_class = false, is_system_call = true, node_id = 142 : i64, semantic_type = !obelisk.integral<32, true, false, 31 : 0, int>, subroutine_kind = 0 : i32, system_library_cell = "work.system_io", system_scope_path = "system_io", system_scope_symbol = @s1.$root::@s3.system_io::@s4.system_io} {
                      obelisk.sv.expression.conversion attributes {node_id = 143 : i64, semantic_type = !obelisk.integral<32, true, false, 31 : 0, int>} {
                        obelisk.sv.expression.named_value attributes {node_id = 144 : i64, referenced_path = "system_io.fd", referenced_symbol = @s1.$root::@s3.system_io::@s4.system_io::@s5.fd, semantic_type = !obelisk.integral<32, true, true, 31 : 0, integer>} {
                        }
                      }
                      obelisk.sv.expression.integer_literal attributes {constant_value = "1", node_id = 145 : i64, semantic_type = !obelisk.integral<32, true, false, 31 : 0, int>} {
                      }
                      obelisk.sv.expression.integer_literal attributes {constant_value = "0", node_id = 146 : i64, semantic_type = !obelisk.integral<32, true, false, 31 : 0, int>} {
                      }
                    }
                  }
                }
              }
              obelisk.sv.statement.expression_statement attributes {node_id = 147 : i64} {
                obelisk.sv.expression.assignment attributes {assignment_kind = 0 : i32, node_id = 148 : i64, semantic_type = !obelisk.integral<32, true, true, 31 : 0, integer>} {
                  obelisk.sv.expression.named_value attributes {node_id = 149 : i64, referenced_path = "system_io.code", referenced_symbol = @s1.$root::@s3.system_io::@s4.system_io::@s7.code, semantic_type = !obelisk.integral<32, true, true, 31 : 0, integer>} {
                  }
                  obelisk.sv.expression.conversion attributes {node_id = 150 : i64, semantic_type = !obelisk.integral<32, true, true, 31 : 0, integer>} {
                    obelisk.sv.expression.call attributes {argument_count = 1 : i64, callee_name = "$ftell", constraint_restrictions = [], defaulted_arguments = array<i64: 0>, has_inline_constraints = false, has_iterator_expression = false, has_output_arguments = false, has_this_class = false, is_super_class = false, is_system_call = true, node_id = 151 : i64, semantic_type = !obelisk.integral<32, true, false, 31 : 0, int>, subroutine_kind = 0 : i32, system_library_cell = "work.system_io", system_scope_path = "system_io", system_scope_symbol = @s1.$root::@s3.system_io::@s4.system_io} {
                      obelisk.sv.expression.conversion attributes {node_id = 152 : i64, semantic_type = !obelisk.integral<32, true, false, 31 : 0, int>} {
                        obelisk.sv.expression.named_value attributes {node_id = 153 : i64, referenced_path = "system_io.fd", referenced_symbol = @s1.$root::@s3.system_io::@s4.system_io::@s5.fd, semantic_type = !obelisk.integral<32, true, true, 31 : 0, integer>} {
                        }
                      }
                    }
                  }
                }
              }
              obelisk.sv.statement.expression_statement attributes {node_id = 154 : i64} {
                obelisk.sv.expression.assignment attributes {assignment_kind = 0 : i32, node_id = 155 : i64, semantic_type = !obelisk.integral<32, true, true, 31 : 0, integer>} {
                  obelisk.sv.expression.named_value attributes {node_id = 156 : i64, referenced_path = "system_io.code", referenced_symbol = @s1.$root::@s3.system_io::@s4.system_io::@s7.code, semantic_type = !obelisk.integral<32, true, true, 31 : 0, integer>} {
                  }
                  obelisk.sv.expression.conversion attributes {node_id = 157 : i64, semantic_type = !obelisk.integral<32, true, true, 31 : 0, integer>} {
                    obelisk.sv.expression.call attributes {argument_count = 1 : i64, callee_name = "$rewind", constraint_restrictions = [], defaulted_arguments = array<i64: 0>, has_inline_constraints = false, has_iterator_expression = false, has_output_arguments = false, has_this_class = false, is_super_class = false, is_system_call = true, node_id = 158 : i64, semantic_type = !obelisk.integral<32, true, false, 31 : 0, int>, subroutine_kind = 0 : i32, system_library_cell = "work.system_io", system_scope_path = "system_io", system_scope_symbol = @s1.$root::@s3.system_io::@s4.system_io} {
                      obelisk.sv.expression.conversion attributes {node_id = 159 : i64, semantic_type = !obelisk.integral<32, true, false, 31 : 0, int>} {
                        obelisk.sv.expression.named_value attributes {node_id = 160 : i64, referenced_path = "system_io.fd", referenced_symbol = @s1.$root::@s3.system_io::@s4.system_io::@s5.fd, semantic_type = !obelisk.integral<32, true, true, 31 : 0, integer>} {
                        }
                      }
                    }
                  }
                }
              }
              obelisk.sv.statement.expression_statement attributes {node_id = 161 : i64} {
                obelisk.sv.expression.call attributes {argument_count = 1 : i64, callee_name = "$fflush", constraint_restrictions = [], defaulted_arguments = array<i64: 0>, has_inline_constraints = false, has_iterator_expression = false, has_output_arguments = false, has_this_class = false, is_super_class = false, is_system_call = true, node_id = 162 : i64, semantic_type = !obelisk.void, subroutine_kind = 0 : i32, system_library_cell = "work.system_io", system_scope_path = "system_io", system_scope_symbol = @s1.$root::@s3.system_io::@s4.system_io} {
                  obelisk.sv.expression.conversion attributes {node_id = 163 : i64, semantic_type = !obelisk.integral<32, true, false, 31 : 0, int>} {
                    obelisk.sv.expression.named_value attributes {node_id = 164 : i64, referenced_path = "system_io.fd", referenced_symbol = @s1.$root::@s3.system_io::@s4.system_io::@s5.fd, semantic_type = !obelisk.integral<32, true, true, 31 : 0, integer>} {
                    }
                  }
                }
              }
              obelisk.sv.statement.expression_statement attributes {node_id = 165 : i64} {
                obelisk.sv.expression.call attributes {argument_count = 0 : i64, callee_name = "$fflush", constraint_restrictions = [], defaulted_arguments = array<i64>, has_inline_constraints = false, has_iterator_expression = false, has_output_arguments = false, has_this_class = false, is_super_class = false, is_system_call = true, node_id = 166 : i64, semantic_type = !obelisk.void, subroutine_kind = 0 : i32, system_library_cell = "work.system_io", system_scope_path = "system_io", system_scope_symbol = @s1.$root::@s3.system_io::@s4.system_io} {
                }
              }
              obelisk.sv.statement.expression_statement attributes {node_id = 167 : i64} {
                obelisk.sv.expression.call attributes {argument_count = 1 : i64, callee_name = "$fclose", constraint_restrictions = [], defaulted_arguments = array<i64: 0>, has_inline_constraints = false, has_iterator_expression = false, has_output_arguments = false, has_this_class = false, is_super_class = false, is_system_call = true, node_id = 168 : i64, semantic_type = !obelisk.void, subroutine_kind = 0 : i32, system_library_cell = "work.system_io", system_scope_path = "system_io", system_scope_symbol = @s1.$root::@s3.system_io::@s4.system_io} {
                  obelisk.sv.expression.conversion attributes {node_id = 169 : i64, semantic_type = !obelisk.integral<32, true, false, 31 : 0, int>} {
                    obelisk.sv.expression.named_value attributes {node_id = 170 : i64, referenced_path = "system_io.fd", referenced_symbol = @s1.$root::@s3.system_io::@s4.system_io::@s5.fd, semantic_type = !obelisk.integral<32, true, true, 31 : 0, integer>} {
                    }
                  }
                }
              }
              obelisk.sv.statement.expression_statement attributes {node_id = 171 : i64} {
                obelisk.sv.expression.call attributes {argument_count = 1 : i64, callee_name = "$fclose", constraint_restrictions = [], defaulted_arguments = array<i64: 0>, has_inline_constraints = false, has_iterator_expression = false, has_output_arguments = false, has_this_class = false, is_super_class = false, is_system_call = true, node_id = 172 : i64, semantic_type = !obelisk.void, subroutine_kind = 0 : i32, system_library_cell = "work.system_io", system_scope_path = "system_io", system_scope_symbol = @s1.$root::@s3.system_io::@s4.system_io} {
                  obelisk.sv.expression.conversion attributes {node_id = 173 : i64, semantic_type = !obelisk.integral<32, true, false, 31 : 0, int>} {
                    obelisk.sv.expression.named_value attributes {node_id = 174 : i64, referenced_path = "system_io.mcd", referenced_symbol = @s1.$root::@s3.system_io::@s4.system_io::@s6.mcd, semantic_type = !obelisk.integral<32, true, true, 31 : 0, integer>} {
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

// CHECK-DAG: %[[FORMAT:.*]] = obelisk_sim.bytes.constant "value=%0h"
// CHECK-DAG: %[[STDOUT:.*]] = arith.constant 1 : i32
// CHECK-DAG: %[[OUT_PATH:.*]] = obelisk_sim.bytes.constant "out.log"
// CHECK-DAG: %[[INPUT_PATH:.*]] = obelisk_sim.bytes.constant "input.bin"
// CHECK-DAG: %[[READ_MODE:.*]] = obelisk_sim.bytes.constant "rb"
// CHECK-DAG: obelisk_sim.bytes.constant "value="
// CHECK-DAG: %[[ALL_FILES:.*]] = arith.constant 0 : i32
// CHECK: obelisk_sim.display {{.*}} to %[[STDOUT]](%[[FORMAT]], {{.*}}) newline = true radix = 10 flags = [0, 0] {library_cell = "work.system_io", scope = "system_io", time_multiplier = 1000 : i64, time_precision = -12 : i32}
// CHECK: obelisk_sim.display {{.*}} newline = true radix = 10 flags = [0, 2, 0] {library_cell = "work.system_io", scope = "system_io", time_multiplier = 1000 : i64, time_precision = -12 : i32}
// CHECK: obelisk_sim.display {{.*}} newline = true radix = 10 flags = [0, 1] {library_cell = "work.system_io", scope = "system_io.named", time_multiplier = 1000 : i64, time_precision = -12 : i32}
// CHECK: obelisk_sim.display {{.*}} newline = true radix = 2
// CHECK: obelisk_sim.display {{.*}} newline = true radix = 8
// CHECK: obelisk_sim.display {{.*}} newline = true radix = 16
// CHECK: obelisk_sim.display {{.*}} newline = false radix = 10
// CHECK: obelisk_sim.display {{.*}} newline = false radix = 2
// CHECK: obelisk_sim.display {{.*}} newline = false radix = 8
// CHECK: obelisk_sim.display {{.*}} newline = false radix = 16
// CHECK: obelisk_sim.file.open_mcd {{.*}}, %[[OUT_PATH]]
// CHECK: obelisk_sim.file.open {{.*}}, %[[INPUT_PATH]], %[[READ_MODE]]
// CHECK: obelisk_sim.display {{.*}} newline = true radix = 10
// CHECK: obelisk_sim.display {{.*}} newline = true radix = 2
// CHECK: obelisk_sim.display {{.*}} newline = true radix = 8
// CHECK: obelisk_sim.display {{.*}} newline = true radix = 16
// CHECK: obelisk_sim.display {{.*}} newline = false radix = 10
// CHECK: obelisk_sim.display {{.*}} newline = false radix = 2
// CHECK: obelisk_sim.display {{.*}} newline = false radix = 8
// CHECK: obelisk_sim.display {{.*}} newline = false radix = 16
// CHECK: obelisk_sim.file.getc
// CHECK: obelisk_sim.file.ungetc
// CHECK: %[[LINE_DATA:.*]], %[[LINE_COUNT:.*]] = obelisk_sim.file.getline
// CHECK: %[[LINE_LOGIC:.*]] = obelisk_sim.logic.from_bits %[[LINE_DATA]]
// CHECK: %[[LINE_VALUE:.*]] = obelisk_sim.packed.unflatten %[[LINE_LOGIC]]
// CHECK: obelisk_sim.ref.store %[[LINE_VALUE]]
// CHECK: obelisk_sim.logic.from_bits %[[LINE_COUNT]]
// CHECK: %[[READ_DATA:.*]], %[[READ_COUNT:.*]] = obelisk_sim.file.read_packed
// CHECK: %[[READ_LOGIC:.*]] = obelisk_sim.logic.from_bits %[[READ_DATA]]
// CHECK: %[[READ_VALUE:.*]] = obelisk_sim.packed.unflatten %[[READ_LOGIC]]
// CHECK: obelisk_sim.ref.store %[[READ_VALUE]]
// CHECK: obelisk_sim.logic.from_bits %[[READ_COUNT]]
// CHECK: obelisk_sim.file.eof
// CHECK: obelisk_sim.file.seek
// CHECK: obelisk_sim.file.tell
// CHECK: obelisk_sim.file.rewind
// CHECK: obelisk_sim.file.flush
// CHECK: obelisk_sim.file.flush {{.*}}, %[[ALL_FILES]]
// CHECK: obelisk_sim.file.close
// CHECK: obelisk_sim.file.close
