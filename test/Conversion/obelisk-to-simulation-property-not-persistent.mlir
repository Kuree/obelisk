// RUN: obelisk-opt %s '--lower-obelisk-to-sim=opt-level=0' | FileCheck %s
// RUN: obelisk-opt %s '--lower-obelisk-to-sim=opt-level=3' -o /dev/null
// RUN: obelisk-opt %s '--lower-obelisk-to-sim=opt-level=3' '--encode-obelisk-sim-to-bytecode=vpi=off' -o /dev/null
// RUN: obelisk-opt %s '--lower-obelisk-to-sim=opt-level=0' -o %t.threaded
// RUN: obelisk-opt %s '--lower-obelisk-to-sim=opt-level=0' --mlir-disable-threading -o %t.serial
// RUN: diff %t.threaded %t.serial

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
        obelisk.sv.symbol.procedural_block attributes {hierarchical_name = "top", node_id = 13 : i64, procedure_kind = 2 : i32, sym_name = "s13", time_precision_fs = 1000000 : i64, time_unit_fs = 1000000 : i64} {
          obelisk.sv.statement.concurrent_assertion attributes {assertion_kind = 0 : i32, has_default_disable = false, has_fail_action = true, has_pass_action = true, node_id = 14 : i64} {
            obelisk.sv.assertion.clocking attributes {node_id = 15 : i64} {
              obelisk.sv.timing.signal_event attributes {edge_kind = 1 : i32, has_iff = false, node_id = 16 : i64} {
                obelisk.sv.expression.named_value attributes {is_signed = false, node_id = 17 : i64, referenced_path = "top.clk", referenced_symbol = @s1.$root::@s3.top::@s4.top::@s6.clk, semantic_type = !obelisk.integral<1, false, true, 0 : 0, logic>} {
                }
              }
              obelisk.sv.assertion.unary attributes {has_range = false, node_id = 18 : i64, operator_kind = 0 : i32, range_is_unbounded = false} {
                obelisk.sv.assertion.unary attributes {has_range = false, node_id = 19 : i64, operator_kind = 3 : i32, range_is_unbounded = false} {
                  obelisk.sv.assertion.simple attributes {has_repetition = false, is_null = false, node_id = 20 : i64, repetition_is_unbounded = false} {
                    obelisk.sv.expression.named_value attributes {is_signed = false, node_id = 21 : i64, referenced_path = "top.a", referenced_symbol = @s1.$root::@s3.top::@s4.top::@s8.a, semantic_type = !obelisk.integral<1, false, true, 0 : 0, logic>} {
                    }
                  }
                }
              }
            }
            obelisk.sv.statement.expression_statement attributes {node_id = 22 : i64} {
              obelisk.sv.expression.call attributes {argument_count = 1 : i64, callee_name = "$info", constraint_restrictions = [], defaulted_arguments = array<i64: 0>, has_inline_constraints = false, has_iterator_expression = false, has_output_arguments = false, has_this_class = false, is_signed = false, is_super_class = false, is_system_call = true, node_id = 23 : i64, semantic_type = !obelisk.void, subroutine_kind = 0 : i32, system_library_cell = "work.top", system_scope_path = "top", system_scope_symbol = @s1.$root::@s3.top::@s4.top} {
                obelisk.sv.expression.string_literal attributes {constant_value = "u0-pass", is_signed = false, node_id = 24 : i64, semantic_type = !obelisk.ranged_packed_array<55 : 0 x !obelisk.integral<1, false, false, 0 : 0, bit>>} {
                }
              }
            }
            obelisk.sv.statement.expression_statement attributes {node_id = 25 : i64} {
              obelisk.sv.expression.call attributes {argument_count = 1 : i64, callee_name = "$info", constraint_restrictions = [], defaulted_arguments = array<i64: 0>, has_inline_constraints = false, has_iterator_expression = false, has_output_arguments = false, has_this_class = false, is_signed = false, is_super_class = false, is_system_call = true, node_id = 26 : i64, semantic_type = !obelisk.void, subroutine_kind = 0 : i32, system_library_cell = "work.top", system_scope_path = "top", system_scope_symbol = @s1.$root::@s3.top::@s4.top} {
                obelisk.sv.expression.string_literal attributes {constant_value = "u0-fail", is_signed = false, node_id = 27 : i64, semantic_type = !obelisk.ranged_packed_array<55 : 0 x !obelisk.integral<1, false, false, 0 : 0, bit>>} {
                }
              }
            }
          }
        }
        obelisk.sv.symbol.procedural_block attributes {hierarchical_name = "top", node_id = 28 : i64, procedure_kind = 2 : i32, sym_name = "s14", time_precision_fs = 1000000 : i64, time_unit_fs = 1000000 : i64} {
          obelisk.sv.statement.concurrent_assertion attributes {assertion_kind = 2 : i32, has_default_disable = false, has_fail_action = false, has_pass_action = true, node_id = 29 : i64} {
            obelisk.sv.assertion.clocking attributes {node_id = 30 : i64} {
              obelisk.sv.timing.signal_event attributes {edge_kind = 1 : i32, has_iff = false, node_id = 31 : i64} {
                obelisk.sv.expression.named_value attributes {is_signed = false, node_id = 32 : i64, referenced_path = "top.clk", referenced_symbol = @s1.$root::@s3.top::@s4.top::@s6.clk, semantic_type = !obelisk.integral<1, false, true, 0 : 0, logic>} {
                }
              }
              obelisk.sv.assertion.unary attributes {has_range = false, node_id = 33 : i64, operator_kind = 0 : i32, range_is_unbounded = false} {
                obelisk.sv.assertion.unary attributes {has_range = false, node_id = 34 : i64, operator_kind = 6 : i32, range_is_unbounded = false} {
                  obelisk.sv.assertion.simple attributes {has_repetition = false, is_null = false, node_id = 35 : i64, repetition_is_unbounded = false} {
                    obelisk.sv.expression.named_value attributes {is_signed = false, node_id = 36 : i64, referenced_path = "top.a", referenced_symbol = @s1.$root::@s3.top::@s4.top::@s8.a, semantic_type = !obelisk.integral<1, false, true, 0 : 0, logic>} {
                    }
                  }
                }
              }
            }
            obelisk.sv.statement.expression_statement attributes {node_id = 37 : i64} {
              obelisk.sv.expression.call attributes {argument_count = 1 : i64, callee_name = "$info", constraint_restrictions = [], defaulted_arguments = array<i64: 0>, has_inline_constraints = false, has_iterator_expression = false, has_output_arguments = false, has_this_class = false, is_signed = false, is_super_class = false, is_system_call = true, node_id = 38 : i64, semantic_type = !obelisk.void, subroutine_kind = 0 : i32, system_library_cell = "work.top", system_scope_path = "top", system_scope_symbol = @s1.$root::@s3.top::@s4.top} {
                obelisk.sv.expression.string_literal attributes {constant_value = "u1-hit", is_signed = false, node_id = 39 : i64, semantic_type = !obelisk.ranged_packed_array<47 : 0 x !obelisk.integral<1, false, false, 0 : 0, bit>>} {
                }
              }
            }
          }
        }
        obelisk.sv.symbol.procedural_block attributes {hierarchical_name = "top", node_id = 40 : i64, procedure_kind = 2 : i32, sym_name = "s15", time_precision_fs = 1000000 : i64, time_unit_fs = 1000000 : i64} {
          obelisk.sv.statement.concurrent_assertion attributes {assertion_kind = 0 : i32, has_default_disable = false, has_fail_action = true, has_pass_action = true, node_id = 41 : i64} {
            obelisk.sv.assertion.clocking attributes {node_id = 42 : i64} {
              obelisk.sv.timing.signal_event attributes {edge_kind = 1 : i32, has_iff = false, node_id = 43 : i64} {
                obelisk.sv.expression.named_value attributes {is_signed = false, node_id = 44 : i64, referenced_path = "top.clk", referenced_symbol = @s1.$root::@s3.top::@s4.top::@s6.clk, semantic_type = !obelisk.integral<1, false, true, 0 : 0, logic>} {
                }
              }
              obelisk.sv.assertion.unary attributes {has_range = false, node_id = 45 : i64, operator_kind = 0 : i32, range_is_unbounded = false} {
                obelisk.sv.assertion.binary attributes {node_id = 46 : i64, operator_kind = 6 : i32} {
                  obelisk.sv.assertion.simple attributes {has_repetition = false, is_null = false, node_id = 47 : i64, repetition_is_unbounded = false} {
                    obelisk.sv.expression.named_value attributes {is_signed = false, node_id = 48 : i64, referenced_path = "top.a", referenced_symbol = @s1.$root::@s3.top::@s4.top::@s8.a, semantic_type = !obelisk.integral<1, false, true, 0 : 0, logic>} {
                    }
                  }
                  obelisk.sv.assertion.simple attributes {has_repetition = false, is_null = false, node_id = 49 : i64, repetition_is_unbounded = false} {
                    obelisk.sv.expression.named_value attributes {is_signed = false, node_id = 50 : i64, referenced_path = "top.b", referenced_symbol = @s1.$root::@s3.top::@s4.top::@s10.b, semantic_type = !obelisk.integral<1, false, true, 0 : 0, logic>} {
                    }
                  }
                }
              }
            }
            obelisk.sv.statement.expression_statement attributes {node_id = 51 : i64} {
              obelisk.sv.expression.call attributes {argument_count = 1 : i64, callee_name = "$info", constraint_restrictions = [], defaulted_arguments = array<i64: 0>, has_inline_constraints = false, has_iterator_expression = false, has_output_arguments = false, has_this_class = false, is_signed = false, is_super_class = false, is_system_call = true, node_id = 52 : i64, semantic_type = !obelisk.void, subroutine_kind = 0 : i32, system_library_cell = "work.top", system_scope_path = "top", system_scope_symbol = @s1.$root::@s3.top::@s4.top} {
                obelisk.sv.expression.string_literal attributes {constant_value = "u2-pass", is_signed = false, node_id = 53 : i64, semantic_type = !obelisk.ranged_packed_array<55 : 0 x !obelisk.integral<1, false, false, 0 : 0, bit>>} {
                }
              }
            }
            obelisk.sv.statement.expression_statement attributes {node_id = 54 : i64} {
              obelisk.sv.expression.call attributes {argument_count = 1 : i64, callee_name = "$info", constraint_restrictions = [], defaulted_arguments = array<i64: 0>, has_inline_constraints = false, has_iterator_expression = false, has_output_arguments = false, has_this_class = false, is_signed = false, is_super_class = false, is_system_call = true, node_id = 55 : i64, semantic_type = !obelisk.void, subroutine_kind = 0 : i32, system_library_cell = "work.top", system_scope_path = "top", system_scope_symbol = @s1.$root::@s3.top::@s4.top} {
                obelisk.sv.expression.string_literal attributes {constant_value = "u2-fail", is_signed = false, node_id = 56 : i64, semantic_type = !obelisk.ranged_packed_array<55 : 0 x !obelisk.integral<1, false, false, 0 : 0, bit>>} {
                }
              }
            }
          }
        }
        obelisk.sv.symbol.procedural_block attributes {hierarchical_name = "top", node_id = 57 : i64, procedure_kind = 2 : i32, sym_name = "s16", time_precision_fs = 1000000 : i64, time_unit_fs = 1000000 : i64} {
          obelisk.sv.statement.concurrent_assertion attributes {assertion_kind = 2 : i32, has_default_disable = false, has_fail_action = false, has_pass_action = true, node_id = 58 : i64} {
            obelisk.sv.assertion.clocking attributes {node_id = 59 : i64} {
              obelisk.sv.timing.signal_event attributes {edge_kind = 1 : i32, has_iff = false, node_id = 60 : i64} {
                obelisk.sv.expression.named_value attributes {is_signed = false, node_id = 61 : i64, referenced_path = "top.clk", referenced_symbol = @s1.$root::@s3.top::@s4.top::@s6.clk, semantic_type = !obelisk.integral<1, false, true, 0 : 0, logic>} {
                }
              }
              obelisk.sv.assertion.unary attributes {has_range = false, node_id = 62 : i64, operator_kind = 0 : i32, range_is_unbounded = false} {
                obelisk.sv.assertion.binary attributes {node_id = 63 : i64, operator_kind = 7 : i32} {
                  obelisk.sv.assertion.simple attributes {has_repetition = false, is_null = false, node_id = 64 : i64, repetition_is_unbounded = false} {
                    obelisk.sv.expression.named_value attributes {is_signed = false, node_id = 65 : i64, referenced_path = "top.a", referenced_symbol = @s1.$root::@s3.top::@s4.top::@s8.a, semantic_type = !obelisk.integral<1, false, true, 0 : 0, logic>} {
                    }
                  }
                  obelisk.sv.assertion.simple attributes {has_repetition = false, is_null = false, node_id = 66 : i64, repetition_is_unbounded = false} {
                    obelisk.sv.expression.named_value attributes {is_signed = false, node_id = 67 : i64, referenced_path = "top.b", referenced_symbol = @s1.$root::@s3.top::@s4.top::@s10.b, semantic_type = !obelisk.integral<1, false, true, 0 : 0, logic>} {
                    }
                  }
                }
              }
            }
            obelisk.sv.statement.expression_statement attributes {node_id = 68 : i64} {
              obelisk.sv.expression.call attributes {argument_count = 1 : i64, callee_name = "$info", constraint_restrictions = [], defaulted_arguments = array<i64: 0>, has_inline_constraints = false, has_iterator_expression = false, has_output_arguments = false, has_this_class = false, is_signed = false, is_super_class = false, is_system_call = true, node_id = 69 : i64, semantic_type = !obelisk.void, subroutine_kind = 0 : i32, system_library_cell = "work.top", system_scope_path = "top", system_scope_symbol = @s1.$root::@s3.top::@s4.top} {
                obelisk.sv.expression.string_literal attributes {constant_value = "u3-hit", is_signed = false, node_id = 70 : i64, semantic_type = !obelisk.ranged_packed_array<47 : 0 x !obelisk.integral<1, false, false, 0 : 0, bit>>} {
                }
              }
            }
          }
        }
        obelisk.sv.symbol.procedural_block attributes {hierarchical_name = "top", node_id = 71 : i64, procedure_kind = 2 : i32, sym_name = "s17", time_precision_fs = 1000000 : i64, time_unit_fs = 1000000 : i64} {
          obelisk.sv.statement.concurrent_assertion attributes {assertion_kind = 0 : i32, has_default_disable = false, has_fail_action = true, has_pass_action = true, node_id = 72 : i64} {
            obelisk.sv.assertion.clocking attributes {node_id = 73 : i64} {
              obelisk.sv.timing.signal_event attributes {edge_kind = 1 : i32, has_iff = false, node_id = 74 : i64} {
                obelisk.sv.expression.named_value attributes {is_signed = false, node_id = 75 : i64, referenced_path = "top.clk", referenced_symbol = @s1.$root::@s3.top::@s4.top::@s6.clk, semantic_type = !obelisk.integral<1, false, true, 0 : 0, logic>} {
                }
              }
              obelisk.sv.assertion.unary attributes {has_range = false, node_id = 76 : i64, operator_kind = 0 : i32, range_is_unbounded = false} {
                obelisk.sv.assertion.sequence_concat attributes {delays = [{is_unbounded = true, min = 1 : i64}], node_id = 77 : i64} {
                  obelisk.sv.assertion.simple attributes {has_repetition = false, is_null = false, node_id = 78 : i64, repetition_is_unbounded = false} {
                    obelisk.sv.expression.named_value attributes {is_signed = false, node_id = 79 : i64, referenced_path = "top.a", referenced_symbol = @s1.$root::@s3.top::@s4.top::@s8.a, semantic_type = !obelisk.integral<1, false, true, 0 : 0, logic>} {
                    }
                  }
                }
              }
            }
            obelisk.sv.statement.expression_statement attributes {node_id = 80 : i64} {
              obelisk.sv.expression.call attributes {argument_count = 1 : i64, callee_name = "$info", constraint_restrictions = [], defaulted_arguments = array<i64: 0>, has_inline_constraints = false, has_iterator_expression = false, has_output_arguments = false, has_this_class = false, is_signed = false, is_super_class = false, is_system_call = true, node_id = 81 : i64, semantic_type = !obelisk.void, subroutine_kind = 0 : i32, system_library_cell = "work.top", system_scope_path = "top", system_scope_symbol = @s1.$root::@s3.top::@s4.top} {
                obelisk.sv.expression.string_literal attributes {constant_value = "u4-pass", is_signed = false, node_id = 82 : i64, semantic_type = !obelisk.ranged_packed_array<55 : 0 x !obelisk.integral<1, false, false, 0 : 0, bit>>} {
                }
              }
            }
            obelisk.sv.statement.expression_statement attributes {node_id = 83 : i64} {
              obelisk.sv.expression.call attributes {argument_count = 1 : i64, callee_name = "$info", constraint_restrictions = [], defaulted_arguments = array<i64: 0>, has_inline_constraints = false, has_iterator_expression = false, has_output_arguments = false, has_this_class = false, is_signed = false, is_super_class = false, is_system_call = true, node_id = 84 : i64, semantic_type = !obelisk.void, subroutine_kind = 0 : i32, system_library_cell = "work.top", system_scope_path = "top", system_scope_symbol = @s1.$root::@s3.top::@s4.top} {
                obelisk.sv.expression.string_literal attributes {constant_value = "u4-fail", is_signed = false, node_id = 85 : i64, semantic_type = !obelisk.ranged_packed_array<55 : 0 x !obelisk.integral<1, false, false, 0 : 0, bit>>} {
                }
              }
            }
          }
        }
        obelisk.sv.symbol.procedural_block attributes {hierarchical_name = "top", node_id = 86 : i64, procedure_kind = 2 : i32, sym_name = "s18", time_precision_fs = 1000000 : i64, time_unit_fs = 1000000 : i64} {
          obelisk.sv.statement.concurrent_assertion attributes {assertion_kind = 2 : i32, has_default_disable = false, has_fail_action = false, has_pass_action = true, node_id = 87 : i64} {
            obelisk.sv.assertion.clocking attributes {node_id = 88 : i64} {
              obelisk.sv.timing.signal_event attributes {edge_kind = 1 : i32, has_iff = false, node_id = 89 : i64} {
                obelisk.sv.expression.named_value attributes {is_signed = false, node_id = 90 : i64, referenced_path = "top.clk", referenced_symbol = @s1.$root::@s3.top::@s4.top::@s6.clk, semantic_type = !obelisk.integral<1, false, true, 0 : 0, logic>} {
                }
              }
              obelisk.sv.assertion.unary attributes {has_range = false, node_id = 91 : i64, operator_kind = 0 : i32, range_is_unbounded = false} {
                obelisk.sv.assertion.sequence_concat attributes {delays = [{is_unbounded = false, max = 0 : i64, min = 0 : i64}, {is_unbounded = false, max = 1 : i64, min = 1 : i64}, {is_unbounded = true, min = 1 : i64}], node_id = 92 : i64} {
                  obelisk.sv.assertion.simple attributes {has_repetition = false, is_null = false, node_id = 93 : i64, repetition_is_unbounded = false} {
                    obelisk.sv.expression.named_value attributes {is_signed = false, node_id = 94 : i64, referenced_path = "top.a", referenced_symbol = @s1.$root::@s3.top::@s4.top::@s8.a, semantic_type = !obelisk.integral<1, false, true, 0 : 0, logic>} {
                    }
                  }
                  obelisk.sv.assertion.simple attributes {has_repetition = false, is_null = false, node_id = 95 : i64, repetition_is_unbounded = false} {
                    obelisk.sv.expression.named_value attributes {is_signed = false, node_id = 96 : i64, referenced_path = "top.b", referenced_symbol = @s1.$root::@s3.top::@s4.top::@s10.b, semantic_type = !obelisk.integral<1, false, true, 0 : 0, logic>} {
                    }
                  }
                  obelisk.sv.assertion.simple attributes {has_repetition = false, is_null = false, node_id = 97 : i64, repetition_is_unbounded = false} {
                    obelisk.sv.expression.named_value attributes {is_signed = false, node_id = 98 : i64, referenced_path = "top.c", referenced_symbol = @s1.$root::@s3.top::@s4.top::@s12.c, semantic_type = !obelisk.integral<1, false, true, 0 : 0, logic>} {
                    }
                  }
                }
              }
            }
            obelisk.sv.statement.expression_statement attributes {node_id = 99 : i64} {
              obelisk.sv.expression.call attributes {argument_count = 1 : i64, callee_name = "$info", constraint_restrictions = [], defaulted_arguments = array<i64: 0>, has_inline_constraints = false, has_iterator_expression = false, has_output_arguments = false, has_this_class = false, is_signed = false, is_super_class = false, is_system_call = true, node_id = 100 : i64, semantic_type = !obelisk.void, subroutine_kind = 0 : i32, system_library_cell = "work.top", system_scope_path = "top", system_scope_symbol = @s1.$root::@s3.top::@s4.top} {
                obelisk.sv.expression.string_literal attributes {constant_value = "u5-hit", is_signed = false, node_id = 101 : i64, semantic_type = !obelisk.ranged_packed_array<47 : 0 x !obelisk.integral<1, false, false, 0 : 0, bit>>} {
                }
              }
            }
          }
        }
        obelisk.sv.symbol.procedural_block attributes {hierarchical_name = "top", node_id = 102 : i64, procedure_kind = 2 : i32, sym_name = "s19", time_precision_fs = 1000000 : i64, time_unit_fs = 1000000 : i64} {
          obelisk.sv.statement.concurrent_assertion attributes {assertion_kind = 0 : i32, has_default_disable = false, has_fail_action = true, has_pass_action = true, node_id = 103 : i64} {
            obelisk.sv.assertion.clocking attributes {node_id = 104 : i64} {
              obelisk.sv.timing.signal_event attributes {edge_kind = 1 : i32, has_iff = false, node_id = 105 : i64} {
                obelisk.sv.expression.named_value attributes {is_signed = false, node_id = 106 : i64, referenced_path = "top.clk", referenced_symbol = @s1.$root::@s3.top::@s4.top::@s6.clk, semantic_type = !obelisk.integral<1, false, true, 0 : 0, logic>} {
                }
              }
              obelisk.sv.assertion.unary attributes {has_range = false, node_id = 107 : i64, operator_kind = 0 : i32, range_is_unbounded = false} {
                obelisk.sv.assertion.simple attributes {has_repetition = true, is_null = false, node_id = 108 : i64, repetition_is_unbounded = true, repetition_kind = 0 : i32, repetition_min = 1 : i64} {
                  obelisk.sv.expression.named_value attributes {is_signed = false, node_id = 109 : i64, referenced_path = "top.a", referenced_symbol = @s1.$root::@s3.top::@s4.top::@s8.a, semantic_type = !obelisk.integral<1, false, true, 0 : 0, logic>} {
                  }
                }
              }
            }
            obelisk.sv.statement.expression_statement attributes {node_id = 110 : i64} {
              obelisk.sv.expression.call attributes {argument_count = 1 : i64, callee_name = "$info", constraint_restrictions = [], defaulted_arguments = array<i64: 0>, has_inline_constraints = false, has_iterator_expression = false, has_output_arguments = false, has_this_class = false, is_signed = false, is_super_class = false, is_system_call = true, node_id = 111 : i64, semantic_type = !obelisk.void, subroutine_kind = 0 : i32, system_library_cell = "work.top", system_scope_path = "top", system_scope_symbol = @s1.$root::@s3.top::@s4.top} {
                obelisk.sv.expression.string_literal attributes {constant_value = "u6-pass", is_signed = false, node_id = 112 : i64, semantic_type = !obelisk.ranged_packed_array<55 : 0 x !obelisk.integral<1, false, false, 0 : 0, bit>>} {
                }
              }
            }
            obelisk.sv.statement.expression_statement attributes {node_id = 113 : i64} {
              obelisk.sv.expression.call attributes {argument_count = 1 : i64, callee_name = "$info", constraint_restrictions = [], defaulted_arguments = array<i64: 0>, has_inline_constraints = false, has_iterator_expression = false, has_output_arguments = false, has_this_class = false, is_signed = false, is_super_class = false, is_system_call = true, node_id = 114 : i64, semantic_type = !obelisk.void, subroutine_kind = 0 : i32, system_library_cell = "work.top", system_scope_path = "top", system_scope_symbol = @s1.$root::@s3.top::@s4.top} {
                obelisk.sv.expression.string_literal attributes {constant_value = "u6-fail", is_signed = false, node_id = 115 : i64, semantic_type = !obelisk.ranged_packed_array<55 : 0 x !obelisk.integral<1, false, false, 0 : 0, bit>>} {
                }
              }
            }
          }
        }
        obelisk.sv.symbol.procedural_block attributes {hierarchical_name = "top", node_id = 116 : i64, procedure_kind = 2 : i32, sym_name = "s20", time_precision_fs = 1000000 : i64, time_unit_fs = 1000000 : i64} {
          obelisk.sv.statement.concurrent_assertion attributes {assertion_kind = 0 : i32, has_default_disable = false, has_fail_action = true, has_pass_action = true, node_id = 117 : i64} {
            obelisk.sv.assertion.clocking attributes {node_id = 118 : i64} {
              obelisk.sv.timing.signal_event attributes {edge_kind = 1 : i32, has_iff = false, node_id = 119 : i64} {
                obelisk.sv.expression.named_value attributes {is_signed = false, node_id = 120 : i64, referenced_path = "top.clk", referenced_symbol = @s1.$root::@s3.top::@s4.top::@s6.clk, semantic_type = !obelisk.integral<1, false, true, 0 : 0, logic>} {
                }
              }
              obelisk.sv.assertion.unary attributes {has_range = false, node_id = 121 : i64, operator_kind = 0 : i32, range_is_unbounded = false} {
                obelisk.sv.assertion.strong_weak attributes {node_id = 122 : i64, strength = 0 : i32} {
                  obelisk.sv.assertion.simple attributes {has_repetition = true, is_null = false, node_id = 123 : i64, repetition_is_unbounded = true, repetition_kind = 0 : i32, repetition_min = 1 : i64} {
                    obelisk.sv.expression.named_value attributes {is_signed = false, node_id = 124 : i64, referenced_path = "top.a", referenced_symbol = @s1.$root::@s3.top::@s4.top::@s8.a, semantic_type = !obelisk.integral<1, false, true, 0 : 0, logic>} {
                    }
                  }
                }
              }
            }
            obelisk.sv.statement.expression_statement attributes {node_id = 125 : i64} {
              obelisk.sv.expression.call attributes {argument_count = 1 : i64, callee_name = "$info", constraint_restrictions = [], defaulted_arguments = array<i64: 0>, has_inline_constraints = false, has_iterator_expression = false, has_output_arguments = false, has_this_class = false, is_signed = false, is_super_class = false, is_system_call = true, node_id = 126 : i64, semantic_type = !obelisk.void, subroutine_kind = 0 : i32, system_library_cell = "work.top", system_scope_path = "top", system_scope_symbol = @s1.$root::@s3.top::@s4.top} {
                obelisk.sv.expression.string_literal attributes {constant_value = "u7-pass", is_signed = false, node_id = 127 : i64, semantic_type = !obelisk.ranged_packed_array<55 : 0 x !obelisk.integral<1, false, false, 0 : 0, bit>>} {
                }
              }
            }
            obelisk.sv.statement.expression_statement attributes {node_id = 128 : i64} {
              obelisk.sv.expression.call attributes {argument_count = 1 : i64, callee_name = "$info", constraint_restrictions = [], defaulted_arguments = array<i64: 0>, has_inline_constraints = false, has_iterator_expression = false, has_output_arguments = false, has_this_class = false, is_signed = false, is_super_class = false, is_system_call = true, node_id = 129 : i64, semantic_type = !obelisk.void, subroutine_kind = 0 : i32, system_library_cell = "work.top", system_scope_path = "top", system_scope_symbol = @s1.$root::@s3.top::@s4.top} {
                obelisk.sv.expression.string_literal attributes {constant_value = "u7-fail", is_signed = false, node_id = 130 : i64, semantic_type = !obelisk.ranged_packed_array<55 : 0 x !obelisk.integral<1, false, false, 0 : 0, bit>>} {
                }
              }
            }
          }
        }
        obelisk.sv.symbol.procedural_block attributes {hierarchical_name = "top", node_id = 131 : i64, procedure_kind = 2 : i32, sym_name = "s21", time_precision_fs = 1000000 : i64, time_unit_fs = 1000000 : i64} {
          obelisk.sv.statement.concurrent_assertion attributes {assertion_kind = 0 : i32, has_default_disable = false, has_fail_action = true, has_pass_action = true, node_id = 132 : i64} {
            obelisk.sv.assertion.clocking attributes {node_id = 133 : i64} {
              obelisk.sv.timing.signal_event attributes {edge_kind = 1 : i32, has_iff = false, node_id = 134 : i64} {
                obelisk.sv.expression.named_value attributes {is_signed = false, node_id = 135 : i64, referenced_path = "top.clk", referenced_symbol = @s1.$root::@s3.top::@s4.top::@s6.clk, semantic_type = !obelisk.integral<1, false, true, 0 : 0, logic>} {
                }
              }
              obelisk.sv.assertion.disable_iff attributes {node_id = 136 : i64} {
                obelisk.sv.expression.named_value attributes {is_signed = false, node_id = 137 : i64, referenced_path = "top.c", referenced_symbol = @s1.$root::@s3.top::@s4.top::@s12.c, semantic_type = !obelisk.integral<1, false, true, 0 : 0, logic>} {
                }
                obelisk.sv.assertion.unary attributes {has_range = false, node_id = 138 : i64, operator_kind = 0 : i32, range_is_unbounded = false} {
                  obelisk.sv.assertion.unary attributes {has_range = false, node_id = 139 : i64, operator_kind = 6 : i32, range_is_unbounded = false} {
                    obelisk.sv.assertion.simple attributes {has_repetition = false, is_null = false, node_id = 140 : i64, repetition_is_unbounded = false} {
                      obelisk.sv.expression.named_value attributes {is_signed = false, node_id = 141 : i64, referenced_path = "top.a", referenced_symbol = @s1.$root::@s3.top::@s4.top::@s8.a, semantic_type = !obelisk.integral<1, false, true, 0 : 0, logic>} {
                      }
                    }
                  }
                }
              }
            }
            obelisk.sv.statement.expression_statement attributes {node_id = 142 : i64} {
              obelisk.sv.expression.call attributes {argument_count = 1 : i64, callee_name = "$info", constraint_restrictions = [], defaulted_arguments = array<i64: 0>, has_inline_constraints = false, has_iterator_expression = false, has_output_arguments = false, has_this_class = false, is_signed = false, is_super_class = false, is_system_call = true, node_id = 143 : i64, semantic_type = !obelisk.void, subroutine_kind = 0 : i32, system_library_cell = "work.top", system_scope_path = "top", system_scope_symbol = @s1.$root::@s3.top::@s4.top} {
                obelisk.sv.expression.string_literal attributes {constant_value = "u8-pass", is_signed = false, node_id = 144 : i64, semantic_type = !obelisk.ranged_packed_array<55 : 0 x !obelisk.integral<1, false, false, 0 : 0, bit>>} {
                }
              }
            }
            obelisk.sv.statement.expression_statement attributes {node_id = 145 : i64} {
              obelisk.sv.expression.call attributes {argument_count = 1 : i64, callee_name = "$info", constraint_restrictions = [], defaulted_arguments = array<i64: 0>, has_inline_constraints = false, has_iterator_expression = false, has_output_arguments = false, has_this_class = false, is_signed = false, is_super_class = false, is_system_call = true, node_id = 146 : i64, semantic_type = !obelisk.void, subroutine_kind = 0 : i32, system_library_cell = "work.top", system_scope_path = "top", system_scope_symbol = @s1.$root::@s3.top::@s4.top} {
                obelisk.sv.expression.string_literal attributes {constant_value = "u8-fail", is_signed = false, node_id = 147 : i64, semantic_type = !obelisk.ranged_packed_array<55 : 0 x !obelisk.integral<1, false, false, 0 : 0, bit>>} {
                }
              }
            }
          }
        }
        obelisk.sv.symbol.procedural_block attributes {hierarchical_name = "top", node_id = 148 : i64, procedure_kind = 2 : i32, sym_name = "s22", time_precision_fs = 1000000 : i64, time_unit_fs = 1000000 : i64} {
          obelisk.sv.statement.concurrent_assertion attributes {assertion_kind = 2 : i32, has_default_disable = false, has_fail_action = false, has_pass_action = true, node_id = 149 : i64} {
            obelisk.sv.assertion.clocking attributes {node_id = 150 : i64} {
              obelisk.sv.timing.signal_event attributes {edge_kind = 1 : i32, has_iff = false, node_id = 151 : i64} {
                obelisk.sv.expression.named_value attributes {is_signed = false, node_id = 152 : i64, referenced_path = "top.clk", referenced_symbol = @s1.$root::@s3.top::@s4.top::@s6.clk, semantic_type = !obelisk.integral<1, false, true, 0 : 0, logic>} {
                }
              }
              obelisk.sv.assertion.unary attributes {has_range = false, node_id = 153 : i64, operator_kind = 0 : i32, range_is_unbounded = false} {
                obelisk.sv.assertion.unary attributes {has_range = true, node_id = 154 : i64, operator_kind = 6 : i32, range_is_unbounded = true, range_min = 2 : i64} {
                  obelisk.sv.assertion.simple attributes {has_repetition = false, is_null = false, node_id = 155 : i64, repetition_is_unbounded = false} {
                    obelisk.sv.expression.named_value attributes {is_signed = false, node_id = 156 : i64, referenced_path = "top.a", referenced_symbol = @s1.$root::@s3.top::@s4.top::@s8.a, semantic_type = !obelisk.integral<1, false, true, 0 : 0, logic>} {
                    }
                  }
                }
              }
            }
            obelisk.sv.statement.expression_statement attributes {node_id = 157 : i64} {
              obelisk.sv.expression.call attributes {argument_count = 1 : i64, callee_name = "$info", constraint_restrictions = [], defaulted_arguments = array<i64: 0>, has_inline_constraints = false, has_iterator_expression = false, has_output_arguments = false, has_this_class = false, is_signed = false, is_super_class = false, is_system_call = true, node_id = 158 : i64, semantic_type = !obelisk.void, subroutine_kind = 0 : i32, system_library_cell = "work.top", system_scope_path = "top", system_scope_symbol = @s1.$root::@s3.top::@s4.top} {
                obelisk.sv.expression.string_literal attributes {constant_value = "u9-hit", is_signed = false, node_id = 159 : i64, semantic_type = !obelisk.ranged_packed_array<47 : 0 x !obelisk.integral<1, false, false, 0 : 0, bit>>} {
                }
              }
            }
          }
        }
        obelisk.sv.symbol.procedural_block attributes {hierarchical_name = "top", node_id = 160 : i64, procedure_kind = 2 : i32, sym_name = "s23", time_precision_fs = 1000000 : i64, time_unit_fs = 1000000 : i64} {
          obelisk.sv.statement.concurrent_assertion attributes {assertion_kind = 4 : i32, has_default_disable = false, has_fail_action = false, has_pass_action = true, node_id = 161 : i64} {
            obelisk.sv.assertion.clocking attributes {node_id = 162 : i64} {
              obelisk.sv.timing.signal_event attributes {edge_kind = 1 : i32, has_iff = false, node_id = 163 : i64} {
                obelisk.sv.expression.named_value attributes {is_signed = false, node_id = 164 : i64, referenced_path = "top.clk", referenced_symbol = @s1.$root::@s3.top::@s4.top::@s6.clk, semantic_type = !obelisk.integral<1, false, true, 0 : 0, logic>} {
                }
              }
              obelisk.sv.assertion.unary attributes {has_range = false, node_id = 165 : i64, operator_kind = 0 : i32, range_is_unbounded = false} {
                obelisk.sv.assertion.sequence_concat attributes {delays = [{is_unbounded = true, min = 1 : i64}], node_id = 166 : i64} {
                  obelisk.sv.assertion.simple attributes {has_repetition = false, is_null = false, node_id = 167 : i64, repetition_is_unbounded = false} {
                    obelisk.sv.expression.named_value attributes {is_signed = false, node_id = 168 : i64, referenced_path = "top.a", referenced_symbol = @s1.$root::@s3.top::@s4.top::@s8.a, semantic_type = !obelisk.integral<1, false, true, 0 : 0, logic>} {
                    }
                  }
                }
              }
            }
            obelisk.sv.statement.empty attributes {node_id = 169 : i64} {
            }
          }
        }
      }
    }
  }
}

// Weak always is intrinsically weak. Negation makes the outer property strong:
// live false results hit the pass action, while weak EOS successes become
// failures once per outstanding aggregate token.
// CHECK-LABEL: obelisk_sim.func private @unit_0.$concurrent_eos_count.14.always(
// CHECK: obelisk_sim.spawn @unit_0.fork.14.1.1
// CHECK-LABEL: obelisk_sim.func private @unit_0(
// CHECK-SAME: obelisk_sim.end_of_simulation_strength = "strong"
// CHECK-SAME: obelisk_sim.negated_operand_end_of_simulation_strength = "weak"
// CHECK-SAME: obelisk_sim.persistent_unary_kind = "always"
// CHECK-SAME: obelisk_sim.temporal_property_negation
// CHECK: obelisk_sim.spawn @unit_0.$concurrent_eos_count.14.always
// CHECK: obelisk_sim.assert.sampled_read
// CHECK: obelisk_sim.spawn @unit_0.fork.14.0.0

// Strong eventuality becomes weak. Its live successes invert to cover failures
// and therefore dispatch nothing, while every strong EOS failure is a
// nonvacuous outer success and creates one cover hit.
// CHECK-LABEL: obelisk_sim.func private @unit_1.$concurrent_eos_count.29.s_eventually(
// CHECK: obelisk_sim.spawn @unit_1.fork.29.0.0
// CHECK-LABEL: obelisk_sim.func private @unit_1(
// CHECK-SAME: obelisk_sim.end_of_simulation_strength = "weak"
// CHECK-SAME: obelisk_sim.negated_operand_end_of_simulation_strength = "strong"
// CHECK-SAME: obelisk_sim.persistent_unary_kind = "s_eventually"
// CHECK-SAME: obelisk_sim.temporal_property_negation
// CHECK: obelisk_sim.spawn @unit_1.$concurrent_eos_count.29.s_eventually
// CHECK-NOT: obelisk_sim.spawn @unit_1.fork

// Weak until has both live results: an operand success becomes outer failure,
// and an operand failure becomes outer success. Its vacuous weak EOS success
// also becomes outer failure.
// CHECK-LABEL: obelisk_sim.func private @unit_2.$concurrent_eos_count.41.until_weak(
// CHECK: obelisk_sim.spawn @unit_2.fork.41.1.1
// CHECK-LABEL: obelisk_sim.func private @unit_2(
// CHECK-SAME: obelisk_sim.end_of_simulation_strength = "strong"
// CHECK-SAME: obelisk_sim.negated_operand_end_of_simulation_strength = "weak"
// CHECK-SAME: obelisk_sim.persistent_until_kind = "until"
// CHECK-SAME: obelisk_sim.temporal_property_negation
// CHECK: obelisk_sim.spawn @unit_2.$concurrent_eos_count.41.until_weak
// CHECK: obelisk_sim.spawn @unit_2.fork.41.1.1
// CHECK: obelisk_sim.spawn @unit_2.fork.41.0.0

// Strong until under cover proves the counted-dispatch preselection uses the
// outer result: both live operand failures and strong EOS failures become
// nonvacuous cover hits; operand successes remain silent.
// CHECK-LABEL: obelisk_sim.func private @unit_3.$concurrent_eos_count.58.until_strong(
// CHECK: obelisk_sim.spawn @unit_3.fork.58.0.0
// CHECK-LABEL: obelisk_sim.func private @unit_3(
// CHECK-SAME: obelisk_sim.end_of_simulation_strength = "weak"
// CHECK-SAME: obelisk_sim.negated_operand_end_of_simulation_strength = "strong"
// CHECK-SAME: obelisk_sim.persistent_until_kind = "s_until"
// CHECK-SAME: obelisk_sim.temporal_property_negation
// CHECK: obelisk_sim.spawn @unit_3.$concurrent_eos_count.58.until_strong
// CHECK: obelisk_sim.spawn @unit_3.fork.58.0.0
// CHECK-NOT: obelisk_sim.spawn @unit_3.fork

// A default assert sequence is weak. The aggregate unbounded-delay state is
// retained; terminal successes and weak EOS successes both invert to failure.
// CHECK-LABEL: obelisk_sim.func private @unit_4.$concurrent_eos_count.72.delay_weak(
// CHECK: obelisk_sim.spawn @unit_4.fork.72.1.1
// CHECK-LABEL: obelisk_sim.func private @unit_4(
// CHECK-SAME: obelisk_sim.end_of_simulation_strength = "strong"
// CHECK-SAME: obelisk_sim.negated_operand_end_of_simulation_strength = "weak"
// CHECK-SAME: obelisk_sim.persistent_delay_monitor
// CHECK-SAME: obelisk_sim.temporal_property_negation
// CHECK: obelisk_sim.spawn @unit_4.$concurrent_eos_count.72.delay_weak
// CHECK: obelisk_sim.spawn @unit_4.fork.72.1.1

// A cover-property sequence is strong by default. Prefix failures and strong
// EOS failures both invert into hits, while terminal successes are silent.
// The deterministic two-age prefix remains compact state.
// CHECK-LABEL: obelisk_sim.func private @unit_5.$concurrent_eos_count.87.delay_strong(
// CHECK: obelisk_sim.spawn @unit_5.fork.87.0.0
// CHECK-LABEL: obelisk_sim.func private @unit_5(
// CHECK-SAME: obelisk_sim.end_of_simulation_strength = "weak"
// CHECK-SAME: obelisk_sim.negated_operand_end_of_simulation_strength = "strong"
// CHECK-SAME: obelisk_sim.persistent_delay_prefix_horizon = 2 : i64
// CHECK-SAME: obelisk_sim.temporal_property_negation
// CHECK: obelisk_sim.spawn @unit_5.$concurrent_eos_count.87.delay_strong
// CHECK: obelisk_sim.spawn @unit_5.fork.87.0.0
// CHECK-NOT: obelisk_sim.spawn @unit_5.fork

// Persistent repetition reuses its aggregate DFA. The default weak form
// inverts EOS to failure and swaps both live completion callbacks.
// CHECK-LABEL: obelisk_sim.func private @unit_6.$concurrent_eos_count.103.repetition_weak(
// CHECK: obelisk_sim.spawn @unit_6.fork.103.1.1
// CHECK-LABEL: obelisk_sim.func private @unit_6(
// CHECK-SAME: obelisk_sim.end_of_simulation_strength = "strong"
// CHECK-SAME: obelisk_sim.negated_operand_end_of_simulation_strength = "weak"
// CHECK-SAME: obelisk_sim.persistent_repetition_dfa
// CHECK-SAME: obelisk_sim.temporal_property_negation
// CHECK: obelisk_sim.spawn @unit_6.$concurrent_eos_count.103.repetition_weak
// CHECK: obelisk_sim.spawn @unit_6.fork.103.1.1
// CHECK: obelisk_sim.spawn @unit_6.fork.103.0.0

// Explicit strong qualification remains inside not. Its EOS failure becomes a
// nonvacuous outer success, and live repetition outcomes are still swapped.
// CHECK-LABEL: obelisk_sim.func private @unit_7.$concurrent_eos_count.117.repetition_strong(
// CHECK: obelisk_sim.spawn @unit_7.fork.117.0.0
// CHECK-LABEL: obelisk_sim.func private @unit_7(
// CHECK-SAME: obelisk_sim.end_of_simulation_strength = "weak"
// CHECK-SAME: obelisk_sim.negated_operand_end_of_simulation_strength = "strong"
// CHECK-SAME: obelisk_sim.persistent_repetition_dfa
// CHECK-SAME: obelisk_sim.temporal_property_negation
// CHECK: obelisk_sim.spawn @unit_7.$concurrent_eos_count.117.repetition_strong
// CHECK: obelisk_sim.spawn @unit_7.fork.117.1.1
// CHECK: obelisk_sim.spawn @unit_7.fork.117.0.0

// Disable clears the aggregate count and advances the epoch. The negated EOS
// callback reloads that epoch so a stale queued success cannot execute.
// CHECK-LABEL: obelisk_sim.func private @unit_8.$concurrent_cancel.
// CHECK: obelisk_sim.ref.store {{.*}} to %arg4
// CHECK: obelisk_sim.ref.load %arg5
// CHECK: arith.addi
// CHECK: obelisk_sim.ref.store {{.*}} to %arg5
// CHECK-LABEL: obelisk_sim.func private @unit_8.$concurrent_eos_count.
// CHECK: [[EPOCH:%.*]] = obelisk_sim.ref.load %arg2
// CHECK: obelisk_sim.spawn @unit_8.fork.{{[0-9]+}}.0.0(%arg0, %arg2, [[EPOCH]])
// CHECK-LABEL: obelisk_sim.func private @unit_8(
// CHECK-SAME: obelisk_sim.end_of_simulation_strength = "weak"
// CHECK-SAME: obelisk_sim.negated_operand_end_of_simulation_strength = "strong"
// CHECK-SAME: obelisk_sim.persistent_unary_kind = "s_eventually"
// CHECK-SAME: obelisk_sim.temporal_property_negation
// CHECK: obelisk_sim.spawn @unit_8.$concurrent_cancel.
// CHECK: obelisk_sim.spawn @unit_8.$concurrent_eos_count.

// An immature ranged-strong-eventuality attempt remains vacuous through not,
// but its successful cover-property evaluation still executes the pass
// action. The eligible count and immature age bitset preserve classification
// for future vacuity counters while one counted callback handles both. The
// bitset representation also permits assertion-control Off gaps without
// changing an attempt's M-clock age.
// CHECK-LABEL: obelisk_sim.func private @unit_9.$concurrent_eos_count.
// CHECK: [[ELIGIBLE:%.*]] = obelisk_sim.ref.load %arg1
// CHECK: [[IMMATURE:%.*]] = obelisk_sim.ref.load %arg2
// CHECK: cf.br [[BIT_LOOP:\^bb[0-9]+]]([[IMMATURE]], [[ELIGIBLE]] : i64, i64)
// CHECK: [[BIT_LOOP]]([[BITS:%.*]]: i64, [[COUNT:%.*]]: i64)
// CHECK: [[LESS_ONE:%.*]] = arith.subi [[BITS]],
// CHECK: [[NEXT_BITS:%.*]] = arith.andi [[BITS]], [[LESS_ONE]] : i64
// CHECK: [[NEXT_COUNT:%.*]] = arith.addi [[COUNT]],
// CHECK: cf.br [[BIT_LOOP]]([[NEXT_BITS]], [[NEXT_COUNT]] : i64, i64)
// CHECK: obelisk_sim.spawn @unit_9.fork.{{[0-9]+}}.0.0
// CHECK-LABEL: obelisk_sim.func private @unit_9(
// CHECK-SAME: obelisk_sim.end_of_simulation_strength = "weak"
// CHECK-SAME: obelisk_sim.negated_operand_end_of_simulation_strength = "strong"
// CHECK-SAME: obelisk_sim.persistent_unary_minimum = 2 : i64
// CHECK-SAME: obelisk_sim.temporal_property_negation
// Both classifications remain addressable for the EOS coordinator.
// CHECK-COUNT-2: obelisk_sim.ref.alloc
// CHECK: obelisk_sim.spawn @unit_9.$concurrent_eos_count.
// CHECK: cf.br {{.*}} : !obelisk_sim.ref<i64>, !obelisk_sim.ref<i64>
// CHECK-NOT: obelisk_sim.spawn @unit_9.fork

// Restrict is also a non-assert/assume directive, so a bare sequence operand is
// strong by default and the negated outer property is weak. It remains
// simulation-silent and therefore outlines neither reports nor EOS work.
// CHECK-NOT: @unit_10.$concurrent_eos_count
// CHECK-LABEL: obelisk_sim.func private @unit_10(
// CHECK-SAME: obelisk_sim.end_of_simulation_strength = "weak"
// CHECK-SAME: obelisk_sim.negated_operand_end_of_simulation_strength = "strong"
// CHECK-SAME: obelisk_sim.persistent_delay_monitor
// CHECK-SAME: obelisk_sim.temporal_property_negation
// CHECK-NOT: obelisk_sim.ref.alloc
// CHECK-NOT: obelisk_sim.spawn
