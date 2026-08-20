// RUN: obelisk-opt %s '--lower-obelisk-to-sim=opt-level=0' | FileCheck %s
// RUN: obelisk-opt %s '--lower-obelisk-to-sim=opt-level=0' | FileCheck %s --check-prefix=O0
// RUN: obelisk-opt %s '--lower-obelisk-to-sim=opt-level=3' | FileCheck %s
// RUN: obelisk-opt %s '--lower-obelisk-to-sim=opt-level=3' '--encode-obelisk-sim-to-bytecode=vpi=off' -o /dev/null

module attributes {llvm.data_layout = "e-m:e-p:64:64-i64:64-n8:16:32:64-S128", llvm.target_triple = "x86_64-unknown-linux-gnu"} {
  obelisk.sv.symbol.definition attributes {definition_kind = 0 : i32, hierarchical_name = "top", name = "top", node_id = 0 : i64, sym_name = "s0.top"} {
  }
  obelisk.sv.symbol.root attributes {hierarchical_name = "\\$root ", name = "$root", node_id = 1 : i64, sym_name = "s1.$root"} {
    obelisk.sv.symbol.compilation_unit attributes {hierarchical_name = "$unit", node_id = 2 : i64, sym_name = "s2"} {
    }
    obelisk.sv.symbol.instance attributes {hierarchical_name = "top", is_uninstantiated = false, name = "top", node_id = 3 : i64, referenced_path = "top", referenced_symbol = @s0.top, sym_name = "s3.top"} {
      obelisk.sv.symbol.instance_body attributes {hierarchical_name = "top", name = "top", node_id = 4 : i64, sym_name = "s4.top", time_precision_fs = 1000000 : i64, time_unit_fs = 1000000 : i64} {
        obelisk.sv.symbol.port attributes {direction = 0 : i32, hierarchical_name = "top.clk", name = "clk", node_id = 5 : i64, semantic_type = !obelisk.integral<1, false, true, 0 : 0, logic>, sym_name = "s5.clk"} {
        }
        obelisk.sv.symbol.variable attributes {hierarchical_name = "top.clk", lifetime = 1 : i32, name = "clk", node_id = 6 : i64, semantic_type = !obelisk.integral<1, false, true, 0 : 0, logic>, sym_name = "s6.clk"} {
        }
        obelisk.sv.symbol.port attributes {direction = 0 : i32, hierarchical_name = "top.a", name = "a", node_id = 7 : i64, semantic_type = !obelisk.integral<1, false, true, 0 : 0, logic>, sym_name = "s7.a"} {
        }
        obelisk.sv.symbol.variable attributes {hierarchical_name = "top.a", lifetime = 1 : i32, name = "a", node_id = 8 : i64, semantic_type = !obelisk.integral<1, false, true, 0 : 0, logic>, sym_name = "s8.a"} {
        }
        obelisk.sv.symbol.port attributes {direction = 0 : i32, hierarchical_name = "top.b", name = "b", node_id = 9 : i64, semantic_type = !obelisk.integral<1, false, true, 0 : 0, logic>, sym_name = "s9.b"} {
        }
        obelisk.sv.symbol.variable attributes {hierarchical_name = "top.b", lifetime = 1 : i32, name = "b", node_id = 10 : i64, semantic_type = !obelisk.integral<1, false, true, 0 : 0, logic>, sym_name = "s10.b"} {
        }
        obelisk.sv.symbol.port attributes {direction = 0 : i32, hierarchical_name = "top.c", name = "c", node_id = 11 : i64, semantic_type = !obelisk.integral<1, false, true, 0 : 0, logic>, sym_name = "s11.c"} {
        }
        obelisk.sv.symbol.variable attributes {hierarchical_name = "top.c", lifetime = 1 : i32, name = "c", node_id = 12 : i64, semantic_type = !obelisk.integral<1, false, true, 0 : 0, logic>, sym_name = "s12.c"} {
        }
        obelisk.sv.symbol.port attributes {direction = 0 : i32, hierarchical_name = "top.d", name = "d", node_id = 13 : i64, semantic_type = !obelisk.integral<1, false, true, 0 : 0, logic>, sym_name = "s13.d"} {
        }
        obelisk.sv.symbol.variable attributes {hierarchical_name = "top.d", lifetime = 1 : i32, name = "d", node_id = 14 : i64, semantic_type = !obelisk.integral<1, false, true, 0 : 0, logic>, sym_name = "s14.d"} {
        }
        obelisk.sv.symbol.port attributes {direction = 0 : i32, hierarchical_name = "top.en", name = "en", node_id = 15 : i64, semantic_type = !obelisk.integral<1, false, true, 0 : 0, logic>, sym_name = "s15.en"} {
        }
        obelisk.sv.symbol.variable attributes {hierarchical_name = "top.en", lifetime = 1 : i32, name = "en", node_id = 16 : i64, semantic_type = !obelisk.integral<1, false, true, 0 : 0, logic>, sym_name = "s16.en"} {
        }
        obelisk.sv.symbol.statement_block attributes {block_kind = 0 : i32, hierarchical_name = "top.a0", name = "a0", node_id = 17 : i64, sym_name = "s17.a0"} {
        }
        obelisk.sv.symbol.procedural_block attributes {hierarchical_name = "top", node_id = 18 : i64, procedure_kind = 2 : i32, sym_name = "s18", time_precision_fs = 1000000 : i64, time_unit_fs = 1000000 : i64} {
          obelisk.sv.statement.block attributes {block_path = "top.a0", block_symbol = @s1.$root::@s3.top::@s4.top::@s17.a0, node_id = 19 : i64} {
            obelisk.sv.statement.concurrent_assertion attributes {assertion_kind = 0 : i32, has_default_disable = false, has_fail_action = true, has_pass_action = true, node_id = 20 : i64} {
              obelisk.sv.assertion.clocking attributes {node_id = 21 : i64} {
                obelisk.sv.timing.signal_event attributes {edge_kind = 1 : i32, has_iff = false, node_id = 22 : i64} {
                  obelisk.sv.expression.named_value attributes {is_signed = false, node_id = 23 : i64, referenced_path = "top.clk", referenced_symbol = @s1.$root::@s3.top::@s4.top::@s6.clk, semantic_type = !obelisk.integral<1, false, true, 0 : 0, logic>} {
                  }
                }
                obelisk.sv.assertion.binary attributes {node_id = 24 : i64, operator_kind = 12 : i32} {
                  obelisk.sv.assertion.simple attributes {has_repetition = true, is_null = false, node_id = 25 : i64, repetition_is_unbounded = false, repetition_kind = 0 : i32, repetition_max = 1 : i64, repetition_min = 0 : i64} {
                    obelisk.sv.expression.named_value attributes {is_signed = false, node_id = 26 : i64, referenced_path = "top.a", referenced_symbol = @s1.$root::@s3.top::@s4.top::@s8.a, semantic_type = !obelisk.integral<1, false, true, 0 : 0, logic>} {
                    }
                  }
                  obelisk.sv.assertion.simple attributes {has_repetition = false, is_null = false, node_id = 27 : i64, repetition_is_unbounded = false} {
                    obelisk.sv.expression.named_value attributes {is_signed = false, node_id = 28 : i64, referenced_path = "top.b", referenced_symbol = @s1.$root::@s3.top::@s4.top::@s10.b, semantic_type = !obelisk.integral<1, false, true, 0 : 0, logic>} {
                    }
                  }
                }
              }
              obelisk.sv.statement.expression_statement attributes {node_id = 29 : i64} {
                obelisk.sv.expression.call attributes {argument_count = 1 : i64, callee_name = "$display", constraint_restrictions = [], defaulted_arguments = array<i64: 0>, has_inline_constraints = false, has_iterator_expression = false, has_output_arguments = false, has_this_class = false, is_signed = false, is_super_class = false, is_system_call = true, node_id = 30 : i64, semantic_type = !obelisk.void, subroutine_kind = 1 : i32, system_library_cell = "work.top", system_scope_path = "top.a0", system_scope_symbol = @s1.$root::@s3.top::@s4.top::@s17.a0} {
                  obelisk.sv.expression.string_literal attributes {constant_value = "imp-pass", is_signed = false, node_id = 31 : i64, semantic_type = !obelisk.ranged_packed_array<63 : 0 x !obelisk.integral<1, false, false, 0 : 0, bit>>} {
                  }
                }
              }
              obelisk.sv.statement.expression_statement attributes {node_id = 32 : i64} {
                obelisk.sv.expression.call attributes {argument_count = 1 : i64, callee_name = "$display", constraint_restrictions = [], defaulted_arguments = array<i64: 0>, has_inline_constraints = false, has_iterator_expression = false, has_output_arguments = false, has_this_class = false, is_signed = false, is_super_class = false, is_system_call = true, node_id = 33 : i64, semantic_type = !obelisk.void, subroutine_kind = 1 : i32, system_library_cell = "work.top", system_scope_path = "top.a0", system_scope_symbol = @s1.$root::@s3.top::@s4.top::@s17.a0} {
                  obelisk.sv.expression.string_literal attributes {constant_value = "imp-fail", is_signed = false, node_id = 34 : i64, semantic_type = !obelisk.ranged_packed_array<63 : 0 x !obelisk.integral<1, false, false, 0 : 0, bit>>} {
                  }
                }
              }
            }
          }
        }
        obelisk.sv.symbol.statement_block attributes {block_kind = 0 : i32, hierarchical_name = "top.a1", name = "a1", node_id = 35 : i64, sym_name = "s19.a1"} {
        }
        obelisk.sv.symbol.procedural_block attributes {hierarchical_name = "top", node_id = 36 : i64, procedure_kind = 2 : i32, sym_name = "s20", time_precision_fs = 1000000 : i64, time_unit_fs = 1000000 : i64} {
          obelisk.sv.statement.block attributes {block_path = "top.a1", block_symbol = @s1.$root::@s3.top::@s4.top::@s19.a1, node_id = 37 : i64} {
            obelisk.sv.statement.concurrent_assertion attributes {assertion_kind = 0 : i32, has_default_disable = false, has_fail_action = true, has_pass_action = true, node_id = 38 : i64} {
              obelisk.sv.assertion.clocking attributes {node_id = 39 : i64} {
                obelisk.sv.timing.signal_event attributes {edge_kind = 1 : i32, has_iff = false, node_id = 40 : i64} {
                  obelisk.sv.expression.named_value attributes {is_signed = false, node_id = 41 : i64, referenced_path = "top.clk", referenced_symbol = @s1.$root::@s3.top::@s4.top::@s6.clk, semantic_type = !obelisk.integral<1, false, true, 0 : 0, logic>} {
                  }
                }
                obelisk.sv.assertion.binary attributes {node_id = 42 : i64, operator_kind = 14 : i32} {
                  obelisk.sv.assertion.simple attributes {has_repetition = true, is_null = false, node_id = 43 : i64, repetition_is_unbounded = false, repetition_kind = 0 : i32, repetition_max = 1 : i64, repetition_min = 0 : i64} {
                    obelisk.sv.expression.named_value attributes {is_signed = false, node_id = 44 : i64, referenced_path = "top.a", referenced_symbol = @s1.$root::@s3.top::@s4.top::@s8.a, semantic_type = !obelisk.integral<1, false, true, 0 : 0, logic>} {
                    }
                  }
                  obelisk.sv.assertion.simple attributes {has_repetition = false, is_null = false, node_id = 45 : i64, repetition_is_unbounded = false} {
                    obelisk.sv.expression.named_value attributes {is_signed = false, node_id = 46 : i64, referenced_path = "top.b", referenced_symbol = @s1.$root::@s3.top::@s4.top::@s10.b, semantic_type = !obelisk.integral<1, false, true, 0 : 0, logic>} {
                    }
                  }
                }
              }
              obelisk.sv.statement.expression_statement attributes {node_id = 47 : i64} {
                obelisk.sv.expression.call attributes {argument_count = 1 : i64, callee_name = "$display", constraint_restrictions = [], defaulted_arguments = array<i64: 0>, has_inline_constraints = false, has_iterator_expression = false, has_output_arguments = false, has_this_class = false, is_signed = false, is_super_class = false, is_system_call = true, node_id = 48 : i64, semantic_type = !obelisk.void, subroutine_kind = 1 : i32, system_library_cell = "work.top", system_scope_path = "top.a1", system_scope_symbol = @s1.$root::@s3.top::@s4.top::@s19.a1} {
                  obelisk.sv.expression.string_literal attributes {constant_value = "follow-pass", is_signed = false, node_id = 49 : i64, semantic_type = !obelisk.ranged_packed_array<87 : 0 x !obelisk.integral<1, false, false, 0 : 0, bit>>} {
                  }
                }
              }
              obelisk.sv.statement.expression_statement attributes {node_id = 50 : i64} {
                obelisk.sv.expression.call attributes {argument_count = 1 : i64, callee_name = "$display", constraint_restrictions = [], defaulted_arguments = array<i64: 0>, has_inline_constraints = false, has_iterator_expression = false, has_output_arguments = false, has_this_class = false, is_signed = false, is_super_class = false, is_system_call = true, node_id = 51 : i64, semantic_type = !obelisk.void, subroutine_kind = 1 : i32, system_library_cell = "work.top", system_scope_path = "top.a1", system_scope_symbol = @s1.$root::@s3.top::@s4.top::@s19.a1} {
                  obelisk.sv.expression.string_literal attributes {constant_value = "follow-fail", is_signed = false, node_id = 52 : i64, semantic_type = !obelisk.ranged_packed_array<87 : 0 x !obelisk.integral<1, false, false, 0 : 0, bit>>} {
                  }
                }
              }
            }
          }
        }
        obelisk.sv.symbol.statement_block attributes {block_kind = 0 : i32, hierarchical_name = "top.a2", name = "a2", node_id = 53 : i64, sym_name = "s21.a2"} {
        }
        obelisk.sv.symbol.procedural_block attributes {hierarchical_name = "top", node_id = 54 : i64, procedure_kind = 2 : i32, sym_name = "s22", time_precision_fs = 1000000 : i64, time_unit_fs = 1000000 : i64} {
          obelisk.sv.statement.block attributes {block_path = "top.a2", block_symbol = @s1.$root::@s3.top::@s4.top::@s21.a2, node_id = 55 : i64} {
            obelisk.sv.statement.concurrent_assertion attributes {assertion_kind = 0 : i32, has_default_disable = false, has_fail_action = true, has_pass_action = true, node_id = 56 : i64} {
              obelisk.sv.assertion.clocking attributes {node_id = 57 : i64} {
                obelisk.sv.timing.signal_event attributes {edge_kind = 1 : i32, has_iff = false, node_id = 58 : i64} {
                  obelisk.sv.expression.named_value attributes {is_signed = false, node_id = 59 : i64, referenced_path = "top.clk", referenced_symbol = @s1.$root::@s3.top::@s4.top::@s6.clk, semantic_type = !obelisk.integral<1, false, true, 0 : 0, logic>} {
                  }
                }
                obelisk.sv.assertion.binary attributes {node_id = 60 : i64, operator_kind = 12 : i32} {
                  obelisk.sv.assertion.binary attributes {node_id = 61 : i64, operator_kind = 1 : i32} {
                    obelisk.sv.assertion.simple attributes {has_repetition = true, is_null = false, node_id = 62 : i64, repetition_is_unbounded = false, repetition_kind = 0 : i32, repetition_max = 0 : i64, repetition_min = 0 : i64} {
                      obelisk.sv.expression.named_value attributes {is_signed = false, node_id = 63 : i64, referenced_path = "top.a", referenced_symbol = @s1.$root::@s3.top::@s4.top::@s8.a, semantic_type = !obelisk.integral<1, false, true, 0 : 0, logic>} {
                      }
                    }
                    obelisk.sv.assertion.sequence_concat attributes {delays = [{is_unbounded = false, max = 0 : i64, min = 0 : i64}, {is_unbounded = false, max = 1 : i64, min = 1 : i64}], node_id = 64 : i64} {
                      obelisk.sv.assertion.simple attributes {has_repetition = false, is_null = false, node_id = 65 : i64, repetition_is_unbounded = false} {
                        obelisk.sv.expression.named_value attributes {is_signed = false, node_id = 66 : i64, referenced_path = "top.a", referenced_symbol = @s1.$root::@s3.top::@s4.top::@s8.a, semantic_type = !obelisk.integral<1, false, true, 0 : 0, logic>} {
                        }
                      }
                      obelisk.sv.assertion.simple attributes {has_repetition = false, is_null = false, node_id = 67 : i64, repetition_is_unbounded = false} {
                        obelisk.sv.expression.named_value attributes {is_signed = false, node_id = 68 : i64, referenced_path = "top.b", referenced_symbol = @s1.$root::@s3.top::@s4.top::@s10.b, semantic_type = !obelisk.integral<1, false, true, 0 : 0, logic>} {
                        }
                      }
                    }
                  }
                  obelisk.sv.assertion.sequence_concat attributes {delays = [{is_unbounded = false, max = 0 : i64, min = 0 : i64}, {is_unbounded = false, max = 1 : i64, min = 1 : i64}], node_id = 69 : i64} {
                    obelisk.sv.assertion.simple attributes {has_repetition = false, is_null = false, node_id = 70 : i64, repetition_is_unbounded = false} {
                      obelisk.sv.expression.named_value attributes {is_signed = false, node_id = 71 : i64, referenced_path = "top.c", referenced_symbol = @s1.$root::@s3.top::@s4.top::@s12.c, semantic_type = !obelisk.integral<1, false, true, 0 : 0, logic>} {
                      }
                    }
                    obelisk.sv.assertion.simple attributes {has_repetition = false, is_null = false, node_id = 72 : i64, repetition_is_unbounded = false} {
                      obelisk.sv.expression.named_value attributes {is_signed = false, node_id = 73 : i64, referenced_path = "top.d", referenced_symbol = @s1.$root::@s3.top::@s4.top::@s14.d, semantic_type = !obelisk.integral<1, false, true, 0 : 0, logic>} {
                      }
                    }
                  }
                }
              }
              obelisk.sv.statement.expression_statement attributes {node_id = 74 : i64} {
                obelisk.sv.expression.call attributes {argument_count = 1 : i64, callee_name = "$display", constraint_restrictions = [], defaulted_arguments = array<i64: 0>, has_inline_constraints = false, has_iterator_expression = false, has_output_arguments = false, has_this_class = false, is_signed = false, is_super_class = false, is_system_call = true, node_id = 75 : i64, semantic_type = !obelisk.void, subroutine_kind = 1 : i32, system_library_cell = "work.top", system_scope_path = "top.a2", system_scope_symbol = @s1.$root::@s3.top::@s4.top::@s21.a2} {
                  obelisk.sv.expression.string_literal attributes {constant_value = "long-pass", is_signed = false, node_id = 76 : i64, semantic_type = !obelisk.ranged_packed_array<71 : 0 x !obelisk.integral<1, false, false, 0 : 0, bit>>} {
                  }
                }
              }
              obelisk.sv.statement.expression_statement attributes {node_id = 77 : i64} {
                obelisk.sv.expression.call attributes {argument_count = 1 : i64, callee_name = "$display", constraint_restrictions = [], defaulted_arguments = array<i64: 0>, has_inline_constraints = false, has_iterator_expression = false, has_output_arguments = false, has_this_class = false, is_signed = false, is_super_class = false, is_system_call = true, node_id = 78 : i64, semantic_type = !obelisk.void, subroutine_kind = 1 : i32, system_library_cell = "work.top", system_scope_path = "top.a2", system_scope_symbol = @s1.$root::@s3.top::@s4.top::@s21.a2} {
                  obelisk.sv.expression.string_literal attributes {constant_value = "long-fail", is_signed = false, node_id = 79 : i64, semantic_type = !obelisk.ranged_packed_array<71 : 0 x !obelisk.integral<1, false, false, 0 : 0, bit>>} {
                  }
                }
              }
            }
          }
        }
        obelisk.sv.symbol.statement_block attributes {block_kind = 0 : i32, hierarchical_name = "top.a3", name = "a3", node_id = 80 : i64, sym_name = "s23.a3"} {
        }
        obelisk.sv.symbol.procedural_block attributes {hierarchical_name = "top", node_id = 81 : i64, procedure_kind = 2 : i32, sym_name = "s24", time_precision_fs = 1000000 : i64, time_unit_fs = 1000000 : i64} {
          obelisk.sv.statement.block attributes {block_path = "top.a3", block_symbol = @s1.$root::@s3.top::@s4.top::@s23.a3, node_id = 82 : i64} {
            obelisk.sv.statement.concurrent_assertion attributes {assertion_kind = 2 : i32, has_default_disable = false, has_fail_action = false, has_pass_action = true, node_id = 83 : i64} {
              obelisk.sv.assertion.clocking attributes {node_id = 84 : i64} {
                obelisk.sv.timing.signal_event attributes {edge_kind = 1 : i32, has_iff = false, node_id = 85 : i64} {
                  obelisk.sv.expression.named_value attributes {is_signed = false, node_id = 86 : i64, referenced_path = "top.clk", referenced_symbol = @s1.$root::@s3.top::@s4.top::@s6.clk, semantic_type = !obelisk.integral<1, false, true, 0 : 0, logic>} {
                  }
                }
                obelisk.sv.assertion.binary attributes {node_id = 87 : i64, operator_kind = 12 : i32} {
                  obelisk.sv.assertion.simple attributes {has_repetition = true, is_null = false, node_id = 88 : i64, repetition_is_unbounded = false, repetition_kind = 0 : i32, repetition_max = 1 : i64, repetition_min = 0 : i64} {
                    obelisk.sv.expression.named_value attributes {is_signed = false, node_id = 89 : i64, referenced_path = "top.a", referenced_symbol = @s1.$root::@s3.top::@s4.top::@s8.a, semantic_type = !obelisk.integral<1, false, true, 0 : 0, logic>} {
                    }
                  }
                  obelisk.sv.assertion.simple attributes {has_repetition = false, is_null = false, node_id = 90 : i64, repetition_is_unbounded = false} {
                    obelisk.sv.expression.named_value attributes {is_signed = false, node_id = 91 : i64, referenced_path = "top.b", referenced_symbol = @s1.$root::@s3.top::@s4.top::@s10.b, semantic_type = !obelisk.integral<1, false, true, 0 : 0, logic>} {
                    }
                  }
                }
              }
              obelisk.sv.statement.expression_statement attributes {node_id = 92 : i64} {
                obelisk.sv.expression.call attributes {argument_count = 1 : i64, callee_name = "$display", constraint_restrictions = [], defaulted_arguments = array<i64: 0>, has_inline_constraints = false, has_iterator_expression = false, has_output_arguments = false, has_this_class = false, is_signed = false, is_super_class = false, is_system_call = true, node_id = 93 : i64, semantic_type = !obelisk.void, subroutine_kind = 1 : i32, system_library_cell = "work.top", system_scope_path = "top.a3", system_scope_symbol = @s1.$root::@s3.top::@s4.top::@s23.a3} {
                  obelisk.sv.expression.string_literal attributes {constant_value = "cover-hit", is_signed = false, node_id = 94 : i64, semantic_type = !obelisk.ranged_packed_array<71 : 0 x !obelisk.integral<1, false, false, 0 : 0, bit>>} {
                  }
                }
              }
            }
          }
        }
        obelisk.sv.symbol.statement_block attributes {block_kind = 0 : i32, hierarchical_name = "top.a4", name = "a4", node_id = 95 : i64, sym_name = "s25.a4"} {
        }
        obelisk.sv.symbol.procedural_block attributes {hierarchical_name = "top", node_id = 96 : i64, procedure_kind = 2 : i32, sym_name = "s26", time_precision_fs = 1000000 : i64, time_unit_fs = 1000000 : i64} {
          obelisk.sv.statement.block attributes {block_path = "top.a4", block_symbol = @s1.$root::@s3.top::@s4.top::@s25.a4, node_id = 97 : i64} {
            obelisk.sv.statement.concurrent_assertion attributes {assertion_kind = 0 : i32, has_default_disable = false, has_fail_action = true, has_pass_action = true, node_id = 98 : i64} {
              obelisk.sv.assertion.clocking attributes {node_id = 99 : i64} {
                obelisk.sv.timing.signal_event attributes {edge_kind = 1 : i32, has_iff = false, node_id = 100 : i64} {
                  obelisk.sv.expression.named_value attributes {is_signed = false, node_id = 101 : i64, referenced_path = "top.clk", referenced_symbol = @s1.$root::@s3.top::@s4.top::@s6.clk, semantic_type = !obelisk.integral<1, false, true, 0 : 0, logic>} {
                  }
                }
                obelisk.sv.assertion.disable_iff attributes {node_id = 102 : i64} {
                  obelisk.sv.expression.unary_op attributes {is_signed = false, node_id = 103 : i64, operator_kind = 9 : i32, semantic_type = !obelisk.integral<1, false, true, 0 : 0, logic>} {
                    obelisk.sv.expression.named_value attributes {is_signed = false, node_id = 104 : i64, referenced_path = "top.en", referenced_symbol = @s1.$root::@s3.top::@s4.top::@s16.en, semantic_type = !obelisk.integral<1, false, true, 0 : 0, logic>} {
                    }
                  }
                  obelisk.sv.assertion.binary attributes {node_id = 105 : i64, operator_kind = 12 : i32} {
                    obelisk.sv.assertion.simple attributes {has_repetition = true, is_null = false, node_id = 106 : i64, repetition_is_unbounded = false, repetition_kind = 0 : i32, repetition_max = 1 : i64, repetition_min = 0 : i64} {
                      obelisk.sv.expression.named_value attributes {is_signed = false, node_id = 107 : i64, referenced_path = "top.a", referenced_symbol = @s1.$root::@s3.top::@s4.top::@s8.a, semantic_type = !obelisk.integral<1, false, true, 0 : 0, logic>} {
                      }
                    }
                    obelisk.sv.assertion.sequence_concat attributes {delays = [{is_unbounded = false, max = 0 : i64, min = 0 : i64}, {is_unbounded = false, max = 1 : i64, min = 1 : i64}], node_id = 108 : i64} {
                      obelisk.sv.assertion.simple attributes {has_repetition = false, is_null = false, node_id = 109 : i64, repetition_is_unbounded = false} {
                        obelisk.sv.expression.named_value attributes {is_signed = false, node_id = 110 : i64, referenced_path = "top.b", referenced_symbol = @s1.$root::@s3.top::@s4.top::@s10.b, semantic_type = !obelisk.integral<1, false, true, 0 : 0, logic>} {
                        }
                      }
                      obelisk.sv.assertion.simple attributes {has_repetition = false, is_null = false, node_id = 111 : i64, repetition_is_unbounded = false} {
                        obelisk.sv.expression.named_value attributes {is_signed = false, node_id = 112 : i64, referenced_path = "top.c", referenced_symbol = @s1.$root::@s3.top::@s4.top::@s12.c, semantic_type = !obelisk.integral<1, false, true, 0 : 0, logic>} {
                        }
                      }
                    }
                  }
                }
              }
              obelisk.sv.statement.expression_statement attributes {node_id = 113 : i64} {
                obelisk.sv.expression.call attributes {argument_count = 1 : i64, callee_name = "$display", constraint_restrictions = [], defaulted_arguments = array<i64: 0>, has_inline_constraints = false, has_iterator_expression = false, has_output_arguments = false, has_this_class = false, is_signed = false, is_super_class = false, is_system_call = true, node_id = 114 : i64, semantic_type = !obelisk.void, subroutine_kind = 1 : i32, system_library_cell = "work.top", system_scope_path = "top.a4", system_scope_symbol = @s1.$root::@s3.top::@s4.top::@s25.a4} {
                  obelisk.sv.expression.string_literal attributes {constant_value = "disable-pass", is_signed = false, node_id = 115 : i64, semantic_type = !obelisk.ranged_packed_array<95 : 0 x !obelisk.integral<1, false, false, 0 : 0, bit>>} {
                  }
                }
              }
              obelisk.sv.statement.expression_statement attributes {node_id = 116 : i64} {
                obelisk.sv.expression.call attributes {argument_count = 1 : i64, callee_name = "$display", constraint_restrictions = [], defaulted_arguments = array<i64: 0>, has_inline_constraints = false, has_iterator_expression = false, has_output_arguments = false, has_this_class = false, is_signed = false, is_super_class = false, is_system_call = true, node_id = 117 : i64, semantic_type = !obelisk.void, subroutine_kind = 1 : i32, system_library_cell = "work.top", system_scope_path = "top.a4", system_scope_symbol = @s1.$root::@s3.top::@s4.top::@s25.a4} {
                  obelisk.sv.expression.string_literal attributes {constant_value = "disable-fail", is_signed = false, node_id = 118 : i64, semantic_type = !obelisk.ranged_packed_array<95 : 0 x !obelisk.integral<1, false, false, 0 : 0, bit>>} {
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

// `a[*0:1] |=> b` evaluates the empty match's consequent on the current tick
// and retains the nonempty endpoint's consequent for the next tick. The
// weak EOS coordinator closes only the latter pending obligation. Both live
// source ages have one coalesced pass/fail dispatch site.
// CHECK-LABEL: obelisk_sim.func private @unit_0.$concurrent_eos_branch_report.
// CHECK: obelisk_sim.bytes.constant "imp-pass"
// CHECK-LABEL: obelisk_sim.func private @unit_0.$concurrent_eos_branch.
// CHECK-SAME: obelisk_sim.branching_antecedent_eos_coalescer
// CHECK: obelisk_sim.branching_antecedent_eos_result = "pass"
// CHECK-SAME: obelisk_sim.branching_antecedent_eos_source_age = 1 : i64
// CHECK-LABEL: obelisk_sim.func private @unit_0(
// CHECK-SAME: obelisk_sim.branching_antecedent_alternatives = 2 : i64
// CHECK-SAME: obelisk_sim.branching_antecedent_consequent_eos_strength = "weak"
// CHECK-SAME: obelisk_sim.branching_antecedent_result_horizon = 2 : i64
// CHECK-SAME: obelisk_sim.mixed_empty_antecedent_current_tick_channels = 1 : i64
// CHECK-SAME: obelisk_sim.mixed_empty_antecedent_handoff_channels = 1 : i64
// CHECK-SAME: obelisk_sim.mixed_empty_antecedent_nonoverlap
// O0-LABEL: obelisk_sim.func private @unit_0(
// O0-COUNT-2: obelisk_sim.ref.alloc
// O0: obelisk_sim.spawn @unit_0.$concurrent_eos_branch.
// O0-NOT: obelisk_sim.ref.alloc
// O0: obelisk_sim.suspend.edge posedge
// CHECK: obelisk_sim.suspend.edge posedge
// CHECK-NOT: obelisk_sim.assert.sampled_read
// CHECK: obelisk_sim.assert.sampled_read {{%.*}} from %arg2
// CHECK-NOT: obelisk_sim.assert.sampled_read
// CHECK: obelisk_sim.assert.sampled_read {{%.*}} from %arg3
// CHECK-NOT: obelisk_sim.assert.sampled_read
// CHECK: obelisk_sim.branching_antecedent_universal_failure
// CHECK: obelisk_sim.branching_antecedent_universal_success
// CHECK: obelisk_sim.spawn @unit_0.fork.20.1.1
// CHECK: obelisk_sim.spawn @unit_0.fork.20.0.0
// CHECK: obelisk_sim.spawn @unit_0.fork.20.1.1
// CHECK: obelisk_sim.spawn @unit_0.fork.20.0.0
// CHECK-NOT: obelisk_sim.spawn @unit_0.fork.20

// The same two endpoint channels obey followed-by's existential rule. A live
// success resolves immediately; failure waits until all possible antecedent
// matches and consequent obligations are exhausted.
// CHECK-LABEL: obelisk_sim.func private @unit_1.$concurrent_eos_branch_report.
// CHECK: obelisk_sim.bytes.constant "follow-pass"
// CHECK-LABEL: obelisk_sim.func private @unit_1(
// CHECK-SAME: obelisk_sim.branching_antecedent_result_horizon = 2 : i64
// CHECK-SAME: obelisk_sim.followed_by_monitor
// CHECK-SAME: obelisk_sim.mixed_empty_antecedent_nonoverlap
// CHECK: obelisk_sim.assert.sampled_read {{%.*}} from %arg2
// CHECK: obelisk_sim.assert.sampled_read {{%.*}} from %arg3
// CHECK: obelisk_sim.branching_antecedent_existential_success
// CHECK: obelisk_sim.branching_antecedent_existential_failure
// CHECK: obelisk_sim.spawn @unit_1.fork.38.1.1
// CHECK: obelisk_sim.spawn @unit_1.fork.38.0.0
// CHECK: obelisk_sim.spawn @unit_1.fork.38.1.1
// CHECK: obelisk_sim.spawn @unit_1.fork.38.0.0
// CHECK-NOT: obelisk_sim.spawn @unit_1.fork.38

// A two-age nonempty endpoint and two-age consequent prove that the current
// empty channel and delayed nonempty channel retain distinct relative ages.
// EOS emits ordered completion checks for source ages 3, 2, and 1, and the
// monitor samples c immediately while retaining d as the continuation.
// CHECK-LABEL: obelisk_sim.func private @unit_2.$concurrent_eos_branch.
// CHECK: obelisk_sim.branching_antecedent_eos_source_age = 3 : i64
// CHECK: obelisk_sim.branching_antecedent_eos_source_age = 2 : i64
// CHECK: obelisk_sim.branching_antecedent_eos_source_age = 1 : i64
// CHECK-LABEL: obelisk_sim.func private @unit_2(
// CHECK-SAME: obelisk_sim.bounded_antecedent_horizon = 2 : i64
// CHECK-SAME: obelisk_sim.branching_antecedent_result_horizon = 4 : i64
// CHECK-SAME: obelisk_sim.mixed_empty_antecedent_nonoverlap
// O0-LABEL: obelisk_sim.func private @unit_2(
// O0-COUNT-4: obelisk_sim.ref.alloc
// O0: obelisk_sim.spawn @unit_2.$concurrent_eos_branch.
// O0-NOT: obelisk_sim.ref.alloc
// O0: obelisk_sim.suspend.edge posedge
// CHECK: obelisk_sim.assert.sampled_read {{%.*}} from %arg2
// CHECK: obelisk_sim.assert.sampled_read {{%.*}} from %arg3
// CHECK: obelisk_sim.assert.sampled_read {{%.*}} from %arg5
// CHECK: obelisk_sim.assert.sampled_read {{%.*}} from %arg4
// CHECK-COUNT-3: obelisk_sim.branching_antecedent_matched_history

// Cover-property has a default-strong consequent. It has no EOS hit actor,
// but either live endpoint can produce the one cover action for its source
// attempt.
// CHECK-NOT: @unit_3.$concurrent_eos
// CHECK-LABEL: obelisk_sim.func private @unit_3(
// CHECK-SAME: obelisk_sim.branching_antecedent_result_horizon = 2 : i64
// CHECK-SAME: obelisk_sim.mixed_empty_antecedent_nonoverlap
// CHECK-COUNT-2: obelisk_sim.spawn @unit_3.fork.83.0.0
// CHECK-NOT: obelisk_sim.spawn @unit_3.fork.83.0.0
// CHECK-NOT: @unit_3.$concurrent_eos

// Disable owns the antecedent, both relative consequent channels, and the
// epoch. It clears all three temporal cells and increments the epoch so queued
// live/EOS reports are suppressed.
// CHECK-LABEL: obelisk_sim.func private @unit_4.$concurrent_cancel.
// CHECK-SAME: obelisk_sim.concurrent_cancel
// CHECK-COUNT-3: obelisk_sim.ref.store {{%.*}} to %arg{{[4-6]}}
// CHECK: obelisk_sim.ref.load %arg7
// CHECK: arith.addi
// CHECK: obelisk_sim.ref.store {{%.*}} to %arg7
// CHECK-LABEL: obelisk_sim.func private @unit_4(
// CHECK-SAME: obelisk_sim.branching_antecedent_result_horizon = 3 : i64
// CHECK-SAME: obelisk_sim.mixed_empty_antecedent_nonoverlap
// CHECK: obelisk_sim.spawn @unit_4.$concurrent_cancel.
// CHECK: obelisk_sim.spawn @unit_4.$concurrent_eos_branch.
