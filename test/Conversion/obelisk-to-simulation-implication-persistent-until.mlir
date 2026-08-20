// RUN: obelisk-opt %s '--lower-obelisk-to-sim=opt-level=0' | FileCheck %s
// RUN: obelisk-opt %s '--lower-obelisk-to-sim=opt-level=3' -o /dev/null
// RUN: obelisk-opt %s '--lower-obelisk-to-sim=opt-level=3' '--encode-obelisk-sim-to-bytecode=vpi=off' -o /dev/null

// Generated from five assertions over one clock:
//   a |-> (a until b)
//   a |=> (b s_until c)
//   a #-# (b until_with c)
//   not (a #=# (b s_until_with c))
//   disable iff (d) a |=> (a until_with b)

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
        obelisk.sv.symbol.procedural_block attributes {hierarchical_name = "top", node_id = 15 : i64, procedure_kind = 2 : i32, sym_name = "s15", time_precision_fs = 1000000 : i64, time_unit_fs = 1000000 : i64} {
          obelisk.sv.statement.concurrent_assertion attributes {assertion_kind = 0 : i32, has_default_disable = false, has_fail_action = true, has_pass_action = true, node_id = 16 : i64} {
            obelisk.sv.assertion.clocking attributes {node_id = 17 : i64} {
              obelisk.sv.timing.signal_event attributes {edge_kind = 1 : i32, has_iff = false, node_id = 18 : i64} {
                obelisk.sv.expression.named_value attributes {is_signed = false, node_id = 19 : i64, referenced_path = "top.clk", referenced_symbol = @s1.$root::@s3.top::@s4.top::@s6.clk, semantic_type = !obelisk.integral<1, false, true, 0 : 0, logic>} {
                }
              }
              obelisk.sv.assertion.binary attributes {node_id = 20 : i64, operator_kind = 11 : i32} {
                obelisk.sv.assertion.simple attributes {has_repetition = false, is_null = false, node_id = 21 : i64, repetition_is_unbounded = false} {
                  obelisk.sv.expression.named_value attributes {is_signed = false, node_id = 22 : i64, referenced_path = "top.a", referenced_symbol = @s1.$root::@s3.top::@s4.top::@s8.a, semantic_type = !obelisk.integral<1, false, true, 0 : 0, logic>} {
                  }
                }
                obelisk.sv.assertion.binary attributes {node_id = 23 : i64, operator_kind = 6 : i32} {
                  obelisk.sv.assertion.simple attributes {has_repetition = false, is_null = false, node_id = 24 : i64, repetition_is_unbounded = false} {
                    obelisk.sv.expression.named_value attributes {is_signed = false, node_id = 25 : i64, referenced_path = "top.a", referenced_symbol = @s1.$root::@s3.top::@s4.top::@s8.a, semantic_type = !obelisk.integral<1, false, true, 0 : 0, logic>} {
                    }
                  }
                  obelisk.sv.assertion.simple attributes {has_repetition = false, is_null = false, node_id = 26 : i64, repetition_is_unbounded = false} {
                    obelisk.sv.expression.named_value attributes {is_signed = false, node_id = 27 : i64, referenced_path = "top.b", referenced_symbol = @s1.$root::@s3.top::@s4.top::@s10.b, semantic_type = !obelisk.integral<1, false, true, 0 : 0, logic>} {
                    }
                  }
                }
              }
            }
            obelisk.sv.statement.expression_statement attributes {node_id = 28 : i64} {
              obelisk.sv.expression.call attributes {argument_count = 1 : i64, callee_name = "$info", constraint_restrictions = [], defaulted_arguments = array<i64: 0>, has_inline_constraints = false, has_iterator_expression = false, has_output_arguments = false, has_this_class = false, is_signed = false, is_super_class = false, is_system_call = true, node_id = 29 : i64, semantic_type = !obelisk.void, subroutine_kind = 0 : i32, system_library_cell = "work.top", system_scope_path = "top", system_scope_symbol = @s1.$root::@s3.top::@s4.top} {
                obelisk.sv.expression.string_literal attributes {constant_value = "u0-pass", is_signed = false, node_id = 30 : i64, semantic_type = !obelisk.ranged_packed_array<55 : 0 x !obelisk.integral<1, false, false, 0 : 0, bit>>} {
                }
              }
            }
            obelisk.sv.statement.expression_statement attributes {node_id = 31 : i64} {
              obelisk.sv.expression.call attributes {argument_count = 1 : i64, callee_name = "$info", constraint_restrictions = [], defaulted_arguments = array<i64: 0>, has_inline_constraints = false, has_iterator_expression = false, has_output_arguments = false, has_this_class = false, is_signed = false, is_super_class = false, is_system_call = true, node_id = 32 : i64, semantic_type = !obelisk.void, subroutine_kind = 0 : i32, system_library_cell = "work.top", system_scope_path = "top", system_scope_symbol = @s1.$root::@s3.top::@s4.top} {
                obelisk.sv.expression.string_literal attributes {constant_value = "u0-fail", is_signed = false, node_id = 33 : i64, semantic_type = !obelisk.ranged_packed_array<55 : 0 x !obelisk.integral<1, false, false, 0 : 0, bit>>} {
                }
              }
            }
          }
        }
        obelisk.sv.symbol.procedural_block attributes {hierarchical_name = "top", node_id = 34 : i64, procedure_kind = 2 : i32, sym_name = "s16", time_precision_fs = 1000000 : i64, time_unit_fs = 1000000 : i64} {
          obelisk.sv.statement.concurrent_assertion attributes {assertion_kind = 0 : i32, has_default_disable = false, has_fail_action = true, has_pass_action = true, node_id = 35 : i64} {
            obelisk.sv.assertion.clocking attributes {node_id = 36 : i64} {
              obelisk.sv.timing.signal_event attributes {edge_kind = 1 : i32, has_iff = false, node_id = 37 : i64} {
                obelisk.sv.expression.named_value attributes {is_signed = false, node_id = 38 : i64, referenced_path = "top.clk", referenced_symbol = @s1.$root::@s3.top::@s4.top::@s6.clk, semantic_type = !obelisk.integral<1, false, true, 0 : 0, logic>} {
                }
              }
              obelisk.sv.assertion.binary attributes {node_id = 39 : i64, operator_kind = 12 : i32} {
                obelisk.sv.assertion.simple attributes {has_repetition = false, is_null = false, node_id = 40 : i64, repetition_is_unbounded = false} {
                  obelisk.sv.expression.named_value attributes {is_signed = false, node_id = 41 : i64, referenced_path = "top.a", referenced_symbol = @s1.$root::@s3.top::@s4.top::@s8.a, semantic_type = !obelisk.integral<1, false, true, 0 : 0, logic>} {
                  }
                }
                obelisk.sv.assertion.binary attributes {node_id = 42 : i64, operator_kind = 7 : i32} {
                  obelisk.sv.assertion.simple attributes {has_repetition = false, is_null = false, node_id = 43 : i64, repetition_is_unbounded = false} {
                    obelisk.sv.expression.named_value attributes {is_signed = false, node_id = 44 : i64, referenced_path = "top.b", referenced_symbol = @s1.$root::@s3.top::@s4.top::@s10.b, semantic_type = !obelisk.integral<1, false, true, 0 : 0, logic>} {
                    }
                  }
                  obelisk.sv.assertion.simple attributes {has_repetition = false, is_null = false, node_id = 45 : i64, repetition_is_unbounded = false} {
                    obelisk.sv.expression.named_value attributes {is_signed = false, node_id = 46 : i64, referenced_path = "top.c", referenced_symbol = @s1.$root::@s3.top::@s4.top::@s12.c, semantic_type = !obelisk.integral<1, false, true, 0 : 0, logic>} {
                    }
                  }
                }
              }
            }
            obelisk.sv.statement.expression_statement attributes {node_id = 47 : i64} {
              obelisk.sv.expression.call attributes {argument_count = 1 : i64, callee_name = "$info", constraint_restrictions = [], defaulted_arguments = array<i64: 0>, has_inline_constraints = false, has_iterator_expression = false, has_output_arguments = false, has_this_class = false, is_signed = false, is_super_class = false, is_system_call = true, node_id = 48 : i64, semantic_type = !obelisk.void, subroutine_kind = 0 : i32, system_library_cell = "work.top", system_scope_path = "top", system_scope_symbol = @s1.$root::@s3.top::@s4.top} {
                obelisk.sv.expression.string_literal attributes {constant_value = "u1-pass", is_signed = false, node_id = 49 : i64, semantic_type = !obelisk.ranged_packed_array<55 : 0 x !obelisk.integral<1, false, false, 0 : 0, bit>>} {
                }
              }
            }
            obelisk.sv.statement.expression_statement attributes {node_id = 50 : i64} {
              obelisk.sv.expression.call attributes {argument_count = 1 : i64, callee_name = "$info", constraint_restrictions = [], defaulted_arguments = array<i64: 0>, has_inline_constraints = false, has_iterator_expression = false, has_output_arguments = false, has_this_class = false, is_signed = false, is_super_class = false, is_system_call = true, node_id = 51 : i64, semantic_type = !obelisk.void, subroutine_kind = 0 : i32, system_library_cell = "work.top", system_scope_path = "top", system_scope_symbol = @s1.$root::@s3.top::@s4.top} {
                obelisk.sv.expression.string_literal attributes {constant_value = "u1-fail", is_signed = false, node_id = 52 : i64, semantic_type = !obelisk.ranged_packed_array<55 : 0 x !obelisk.integral<1, false, false, 0 : 0, bit>>} {
                }
              }
            }
          }
        }
        obelisk.sv.symbol.procedural_block attributes {hierarchical_name = "top", node_id = 53 : i64, procedure_kind = 2 : i32, sym_name = "s17", time_precision_fs = 1000000 : i64, time_unit_fs = 1000000 : i64} {
          obelisk.sv.statement.concurrent_assertion attributes {assertion_kind = 0 : i32, has_default_disable = false, has_fail_action = true, has_pass_action = true, node_id = 54 : i64} {
            obelisk.sv.assertion.clocking attributes {node_id = 55 : i64} {
              obelisk.sv.timing.signal_event attributes {edge_kind = 1 : i32, has_iff = false, node_id = 56 : i64} {
                obelisk.sv.expression.named_value attributes {is_signed = false, node_id = 57 : i64, referenced_path = "top.clk", referenced_symbol = @s1.$root::@s3.top::@s4.top::@s6.clk, semantic_type = !obelisk.integral<1, false, true, 0 : 0, logic>} {
                }
              }
              obelisk.sv.assertion.binary attributes {node_id = 58 : i64, operator_kind = 13 : i32} {
                obelisk.sv.assertion.simple attributes {has_repetition = false, is_null = false, node_id = 59 : i64, repetition_is_unbounded = false} {
                  obelisk.sv.expression.named_value attributes {is_signed = false, node_id = 60 : i64, referenced_path = "top.a", referenced_symbol = @s1.$root::@s3.top::@s4.top::@s8.a, semantic_type = !obelisk.integral<1, false, true, 0 : 0, logic>} {
                  }
                }
                obelisk.sv.assertion.binary attributes {node_id = 61 : i64, operator_kind = 8 : i32} {
                  obelisk.sv.assertion.simple attributes {has_repetition = false, is_null = false, node_id = 62 : i64, repetition_is_unbounded = false} {
                    obelisk.sv.expression.named_value attributes {is_signed = false, node_id = 63 : i64, referenced_path = "top.b", referenced_symbol = @s1.$root::@s3.top::@s4.top::@s10.b, semantic_type = !obelisk.integral<1, false, true, 0 : 0, logic>} {
                    }
                  }
                  obelisk.sv.assertion.simple attributes {has_repetition = false, is_null = false, node_id = 64 : i64, repetition_is_unbounded = false} {
                    obelisk.sv.expression.named_value attributes {is_signed = false, node_id = 65 : i64, referenced_path = "top.c", referenced_symbol = @s1.$root::@s3.top::@s4.top::@s12.c, semantic_type = !obelisk.integral<1, false, true, 0 : 0, logic>} {
                    }
                  }
                }
              }
            }
            obelisk.sv.statement.expression_statement attributes {node_id = 66 : i64} {
              obelisk.sv.expression.call attributes {argument_count = 1 : i64, callee_name = "$info", constraint_restrictions = [], defaulted_arguments = array<i64: 0>, has_inline_constraints = false, has_iterator_expression = false, has_output_arguments = false, has_this_class = false, is_signed = false, is_super_class = false, is_system_call = true, node_id = 67 : i64, semantic_type = !obelisk.void, subroutine_kind = 0 : i32, system_library_cell = "work.top", system_scope_path = "top", system_scope_symbol = @s1.$root::@s3.top::@s4.top} {
                obelisk.sv.expression.string_literal attributes {constant_value = "u2-pass", is_signed = false, node_id = 68 : i64, semantic_type = !obelisk.ranged_packed_array<55 : 0 x !obelisk.integral<1, false, false, 0 : 0, bit>>} {
                }
              }
            }
            obelisk.sv.statement.expression_statement attributes {node_id = 69 : i64} {
              obelisk.sv.expression.call attributes {argument_count = 1 : i64, callee_name = "$info", constraint_restrictions = [], defaulted_arguments = array<i64: 0>, has_inline_constraints = false, has_iterator_expression = false, has_output_arguments = false, has_this_class = false, is_signed = false, is_super_class = false, is_system_call = true, node_id = 70 : i64, semantic_type = !obelisk.void, subroutine_kind = 0 : i32, system_library_cell = "work.top", system_scope_path = "top", system_scope_symbol = @s1.$root::@s3.top::@s4.top} {
                obelisk.sv.expression.string_literal attributes {constant_value = "u2-fail", is_signed = false, node_id = 71 : i64, semantic_type = !obelisk.ranged_packed_array<55 : 0 x !obelisk.integral<1, false, false, 0 : 0, bit>>} {
                }
              }
            }
          }
        }
        obelisk.sv.symbol.procedural_block attributes {hierarchical_name = "top", node_id = 72 : i64, procedure_kind = 2 : i32, sym_name = "s18", time_precision_fs = 1000000 : i64, time_unit_fs = 1000000 : i64} {
          obelisk.sv.statement.concurrent_assertion attributes {assertion_kind = 2 : i32, has_default_disable = false, has_fail_action = false, has_pass_action = true, node_id = 73 : i64} {
            obelisk.sv.assertion.clocking attributes {node_id = 74 : i64} {
              obelisk.sv.timing.signal_event attributes {edge_kind = 1 : i32, has_iff = false, node_id = 75 : i64} {
                obelisk.sv.expression.named_value attributes {is_signed = false, node_id = 76 : i64, referenced_path = "top.clk", referenced_symbol = @s1.$root::@s3.top::@s4.top::@s6.clk, semantic_type = !obelisk.integral<1, false, true, 0 : 0, logic>} {
                }
              }
              obelisk.sv.assertion.unary attributes {has_range = false, node_id = 77 : i64, operator_kind = 0 : i32, range_is_unbounded = false} {
                obelisk.sv.assertion.binary attributes {node_id = 78 : i64, operator_kind = 14 : i32} {
                  obelisk.sv.assertion.simple attributes {has_repetition = false, is_null = false, node_id = 79 : i64, repetition_is_unbounded = false} {
                    obelisk.sv.expression.named_value attributes {is_signed = false, node_id = 80 : i64, referenced_path = "top.a", referenced_symbol = @s1.$root::@s3.top::@s4.top::@s8.a, semantic_type = !obelisk.integral<1, false, true, 0 : 0, logic>} {
                    }
                  }
                  obelisk.sv.assertion.binary attributes {node_id = 81 : i64, operator_kind = 9 : i32} {
                    obelisk.sv.assertion.simple attributes {has_repetition = false, is_null = false, node_id = 82 : i64, repetition_is_unbounded = false} {
                      obelisk.sv.expression.named_value attributes {is_signed = false, node_id = 83 : i64, referenced_path = "top.b", referenced_symbol = @s1.$root::@s3.top::@s4.top::@s10.b, semantic_type = !obelisk.integral<1, false, true, 0 : 0, logic>} {
                      }
                    }
                    obelisk.sv.assertion.simple attributes {has_repetition = false, is_null = false, node_id = 84 : i64, repetition_is_unbounded = false} {
                      obelisk.sv.expression.named_value attributes {is_signed = false, node_id = 85 : i64, referenced_path = "top.c", referenced_symbol = @s1.$root::@s3.top::@s4.top::@s12.c, semantic_type = !obelisk.integral<1, false, true, 0 : 0, logic>} {
                      }
                    }
                  }
                }
              }
            }
            obelisk.sv.statement.expression_statement attributes {node_id = 86 : i64} {
              obelisk.sv.expression.call attributes {argument_count = 1 : i64, callee_name = "$info", constraint_restrictions = [], defaulted_arguments = array<i64: 0>, has_inline_constraints = false, has_iterator_expression = false, has_output_arguments = false, has_this_class = false, is_signed = false, is_super_class = false, is_system_call = true, node_id = 87 : i64, semantic_type = !obelisk.void, subroutine_kind = 0 : i32, system_library_cell = "work.top", system_scope_path = "top", system_scope_symbol = @s1.$root::@s3.top::@s4.top} {
                obelisk.sv.expression.string_literal attributes {constant_value = "u3-hit", is_signed = false, node_id = 88 : i64, semantic_type = !obelisk.ranged_packed_array<47 : 0 x !obelisk.integral<1, false, false, 0 : 0, bit>>} {
                }
              }
            }
          }
        }
        obelisk.sv.symbol.procedural_block attributes {hierarchical_name = "top", node_id = 89 : i64, procedure_kind = 2 : i32, sym_name = "s19", time_precision_fs = 1000000 : i64, time_unit_fs = 1000000 : i64} {
          obelisk.sv.statement.concurrent_assertion attributes {assertion_kind = 0 : i32, has_default_disable = false, has_fail_action = true, has_pass_action = true, node_id = 90 : i64} {
            obelisk.sv.assertion.clocking attributes {node_id = 91 : i64} {
              obelisk.sv.timing.signal_event attributes {edge_kind = 1 : i32, has_iff = false, node_id = 92 : i64} {
                obelisk.sv.expression.named_value attributes {is_signed = false, node_id = 93 : i64, referenced_path = "top.clk", referenced_symbol = @s1.$root::@s3.top::@s4.top::@s6.clk, semantic_type = !obelisk.integral<1, false, true, 0 : 0, logic>} {
                }
              }
              obelisk.sv.assertion.disable_iff attributes {node_id = 94 : i64} {
                obelisk.sv.expression.named_value attributes {is_signed = false, node_id = 95 : i64, referenced_path = "top.d", referenced_symbol = @s1.$root::@s3.top::@s4.top::@s14.d, semantic_type = !obelisk.integral<1, false, true, 0 : 0, logic>} {
                }
                obelisk.sv.assertion.binary attributes {node_id = 96 : i64, operator_kind = 12 : i32} {
                  obelisk.sv.assertion.simple attributes {has_repetition = false, is_null = false, node_id = 97 : i64, repetition_is_unbounded = false} {
                    obelisk.sv.expression.named_value attributes {is_signed = false, node_id = 98 : i64, referenced_path = "top.a", referenced_symbol = @s1.$root::@s3.top::@s4.top::@s8.a, semantic_type = !obelisk.integral<1, false, true, 0 : 0, logic>} {
                    }
                  }
                  obelisk.sv.assertion.binary attributes {node_id = 99 : i64, operator_kind = 8 : i32} {
                    obelisk.sv.assertion.simple attributes {has_repetition = false, is_null = false, node_id = 100 : i64, repetition_is_unbounded = false} {
                      obelisk.sv.expression.named_value attributes {is_signed = false, node_id = 101 : i64, referenced_path = "top.a", referenced_symbol = @s1.$root::@s3.top::@s4.top::@s8.a, semantic_type = !obelisk.integral<1, false, true, 0 : 0, logic>} {
                      }
                    }
                    obelisk.sv.assertion.simple attributes {has_repetition = false, is_null = false, node_id = 102 : i64, repetition_is_unbounded = false} {
                      obelisk.sv.expression.named_value attributes {is_signed = false, node_id = 103 : i64, referenced_path = "top.b", referenced_symbol = @s1.$root::@s3.top::@s4.top::@s10.b, semantic_type = !obelisk.integral<1, false, true, 0 : 0, logic>} {
                      }
                    }
                  }
                }
              }
            }
            obelisk.sv.statement.expression_statement attributes {node_id = 104 : i64} {
              obelisk.sv.expression.call attributes {argument_count = 1 : i64, callee_name = "$info", constraint_restrictions = [], defaulted_arguments = array<i64: 0>, has_inline_constraints = false, has_iterator_expression = false, has_output_arguments = false, has_this_class = false, is_signed = false, is_super_class = false, is_system_call = true, node_id = 105 : i64, semantic_type = !obelisk.void, subroutine_kind = 0 : i32, system_library_cell = "work.top", system_scope_path = "top", system_scope_symbol = @s1.$root::@s3.top::@s4.top} {
                obelisk.sv.expression.string_literal attributes {constant_value = "u4-pass", is_signed = false, node_id = 106 : i64, semantic_type = !obelisk.ranged_packed_array<55 : 0 x !obelisk.integral<1, false, false, 0 : 0, bit>>} {
                }
              }
            }
            obelisk.sv.statement.expression_statement attributes {node_id = 107 : i64} {
              obelisk.sv.expression.call attributes {argument_count = 1 : i64, callee_name = "$info", constraint_restrictions = [], defaulted_arguments = array<i64: 0>, has_inline_constraints = false, has_iterator_expression = false, has_output_arguments = false, has_this_class = false, is_signed = false, is_super_class = false, is_system_call = true, node_id = 108 : i64, semantic_type = !obelisk.void, subroutine_kind = 0 : i32, system_library_cell = "work.top", system_scope_path = "top", system_scope_symbol = @s1.$root::@s3.top::@s4.top} {
                obelisk.sv.expression.string_literal attributes {constant_value = "u4-fail", is_signed = false, node_id = 109 : i64, semantic_type = !obelisk.ranged_packed_array<55 : 0 x !obelisk.integral<1, false, false, 0 : 0, bit>>} {
                }
              }
            }
          }
        }
      }
    }
  }
}

// Overlapped implication activates weak `until` on the antecedent clock.
// Reusing `a` as the antecedent and left operand requires only two sampled
// reads. False antecedent and right success pass; left failure fails.
// CHECK-LABEL: obelisk_sim.func private @unit_0.$concurrent_eos_count.
// CHECK: obelisk_sim.spawn @unit_0.fork.{{[0-9]+}}.0.0
// CHECK-LABEL: obelisk_sim.func private @unit_0(
// CHECK-SAME: obelisk_sim.persistent_until_implication
// CHECK-SAME: obelisk_sim.persistent_until_kind = "until"
// CHECK-NOT: obelisk_sim.persistent_until_inclusive
// CHECK-NOT: obelisk_sim.persistent_until_nonoverlapped
// CHECK: obelisk_sim.spawn @unit_0.$concurrent_eos_count.
// CHECK: obelisk_sim.assert.sampled_read
// CHECK: obelisk_sim.spawn @unit_0.fork.{{[0-9]+}}.0.0
// CHECK: obelisk_sim.assert.sampled_read
// CHECK-NOT: obelisk_sim.assert.sampled_read
// CHECK: obelisk_sim.spawn @unit_0.fork.{{[0-9]+}}.0.0
// CHECK: obelisk_sim.spawn @unit_0.fork.{{[0-9]+}}.1.1
// CHECK-NOT: obelisk_sim.spawn @unit_0.fork

// Nonoverlapped strong until owns one explicit handoff bit. The EOS
// coordinator counts live and handoff attempts and dispatches strong failure.
// CHECK-LABEL: obelisk_sim.func private @unit_1.$concurrent_eos_count.
// CHECK-COUNT-2: obelisk_sim.ref.load
// CHECK-NOT: obelisk_sim.ref.load
// CHECK: obelisk_sim.spawn @unit_1.fork.{{[0-9]+}}.1.1
// CHECK-LABEL: obelisk_sim.func private @unit_1(
// CHECK-SAME: obelisk_sim.persistent_until_kind = "s_until"
// CHECK-SAME: obelisk_sim.persistent_until_nonoverlapped
// CHECK-SAME: obelisk_sim.persistent_until_strong
// CHECK: obelisk_sim.ref.alloc %{{.*}} : i64 -> !obelisk_sim.ref<i64>
// CHECK-NOT: obelisk_sim.ref.alloc %{{.*}} : i64 -> !obelisk_sim.ref<i64>
// CHECK: [[HANDOFF:%.*]] = obelisk_sim.ref.alloc %{{.*}} {obelisk_sim.persistent_implication_handoff}
// CHECK: obelisk_sim.spawn @unit_1.$concurrent_eos_count.{{.*}}({{.*}}[[HANDOFF]]
// CHECK: obelisk_sim.assert.sampled_read
// CHECK: obelisk_sim.spawn @unit_1.fork.{{[0-9]+}}.0.0
// CHECK: obelisk_sim.ref.store
// CHECK-COUNT-2: obelisk_sim.assert.sampled_read
// CHECK-NOT: obelisk_sim.assert.sampled_read
// CHECK: obelisk_sim.spawn @unit_1.fork.{{[0-9]+}}.0.0
// CHECK: obelisk_sim.spawn @unit_1.fork.{{[0-9]+}}.1.1
// CHECK-NOT: obelisk_sim.spawn @unit_1.fork

// Inclusive weak until requires the left term on its right-success clock.
// Followed-by maps a false antecedent and a failed operand to failure.
// CHECK-LABEL: obelisk_sim.func private @unit_2.$concurrent_eos_count.
// CHECK: obelisk_sim.spawn @unit_2.fork.{{[0-9]+}}.0.0
// CHECK-LABEL: obelisk_sim.func private @unit_2(
// CHECK-SAME: obelisk_sim.followed_by_monitor
// CHECK-SAME: obelisk_sim.persistent_until_inclusive
// CHECK-SAME: obelisk_sim.persistent_until_kind = "until_with"
// CHECK-NOT: obelisk_sim.persistent_until_nonoverlapped
// CHECK: obelisk_sim.assert.sampled_read
// CHECK: obelisk_sim.spawn @unit_2.fork.{{[0-9]+}}.1.1
// CHECK-COUNT-2: obelisk_sim.assert.sampled_read
// CHECK-NOT: obelisk_sim.assert.sampled_read
// CHECK: obelisk_sim.spawn @unit_2.fork.{{[0-9]+}}.0.0
// CHECK: obelisk_sim.spawn @unit_2.fork.{{[0-9]+}}.1.1
// CHECK-NOT: obelisk_sim.spawn @unit_2.fork

// Temporal `not` switches strong s_until_with to weak outer completion and
// inverts the coalesced followed-by result once. False antecedent and live or
// EOS operand failure become cover hits.
// CHECK-LABEL: obelisk_sim.func private @unit_3.$concurrent_eos_count.
// CHECK: obelisk_sim.spawn @unit_3.fork.{{[0-9]+}}.0.0
// CHECK-LABEL: obelisk_sim.func private @unit_3(
// CHECK-SAME: obelisk_sim.end_of_simulation_strength = "weak"
// CHECK-SAME: obelisk_sim.negated_operand_end_of_simulation_strength = "strong"
// CHECK-SAME: obelisk_sim.persistent_until_inclusive
// CHECK-SAME: obelisk_sim.persistent_until_kind = "s_until_with"
// CHECK-SAME: obelisk_sim.persistent_until_nonoverlapped
// CHECK-SAME: obelisk_sim.temporal_property_negation
// CHECK: obelisk_sim.assert.sampled_read
// CHECK: obelisk_sim.spawn @unit_3.fork.{{[0-9]+}}.0.0
// CHECK: obelisk_sim.ref.store
// CHECK-COUNT-2: obelisk_sim.assert.sampled_read
// CHECK-NOT: obelisk_sim.assert.sampled_read
// CHECK: obelisk_sim.spawn @unit_3.fork.{{[0-9]+}}.0.0
// CHECK-NOT: obelisk_sim.spawn @unit_3.fork

// Disable clears the live and handoff cells and advances the callback epoch.
// The repeated direct `a` reference still shares one sampled truth value.
// CHECK-LABEL: obelisk_sim.func private @unit_4.$concurrent_cancel.
// CHECK: obelisk_sim.ref.store {{.*}} to %arg4
// CHECK: obelisk_sim.ref.store {{.*}} to %arg5
// CHECK: [[EPOCH:%.*]] = obelisk_sim.ref.load %arg6
// CHECK: [[NEXT_EPOCH:%.*]] = arith.addi [[EPOCH]],
// CHECK: obelisk_sim.ref.store [[NEXT_EPOCH]] to %arg6
// CHECK-LABEL: obelisk_sim.func private @unit_4.$concurrent_eos_count.
// CHECK-COUNT-3: obelisk_sim.ref.load
// CHECK-NOT: obelisk_sim.ref.load
// CHECK-LABEL: obelisk_sim.func private @unit_4(
// CHECK-SAME: obelisk_sim.persistent_until_inclusive
// CHECK-SAME: obelisk_sim.persistent_until_nonoverlapped
// CHECK-COUNT-2: obelisk_sim.ref.alloc %{{.*}} : i64 -> !obelisk_sim.ref<i64>
// CHECK-NOT: obelisk_sim.ref.alloc %{{.*}} : i64 -> !obelisk_sim.ref<i64>
// CHECK: [[DISABLE_HANDOFF:%.*]] = obelisk_sim.ref.alloc %{{.*}} {obelisk_sim.persistent_implication_handoff}
// CHECK: obelisk_sim.spawn @unit_4.$concurrent_cancel.{{.*}}({{.*}}[[DISABLE_HANDOFF]]
// CHECK: obelisk_sim.spawn @unit_4.$concurrent_eos_count.{{.*}}({{.*}}[[DISABLE_HANDOFF]]
// CHECK-COUNT-2: obelisk_sim.assert.sampled_read
// CHECK-NOT: obelisk_sim.assert.sampled_read
