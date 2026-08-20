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
              obelisk.sv.assertion.unary attributes {has_range = false, node_id = 20 : i64, operator_kind = 0 : i32, range_is_unbounded = false} {
                obelisk.sv.assertion.binary attributes {node_id = 21 : i64, operator_kind = 11 : i32} {
                  obelisk.sv.assertion.simple attributes {has_repetition = false, is_null = false, node_id = 22 : i64, repetition_is_unbounded = false} {
                    obelisk.sv.expression.named_value attributes {is_signed = false, node_id = 23 : i64, referenced_path = "top.a", referenced_symbol = @s1.$root::@s3.top::@s4.top::@s8.a, semantic_type = !obelisk.integral<1, false, true, 0 : 0, logic>} {
                    }
                  }
                  obelisk.sv.assertion.simple attributes {has_repetition = false, is_null = false, node_id = 24 : i64, repetition_is_unbounded = false} {
                    obelisk.sv.expression.named_value attributes {is_signed = false, node_id = 25 : i64, referenced_path = "top.b", referenced_symbol = @s1.$root::@s3.top::@s4.top::@s10.b, semantic_type = !obelisk.integral<1, false, true, 0 : 0, logic>} {
                    }
                  }
                }
              }
            }
            obelisk.sv.statement.expression_statement attributes {node_id = 26 : i64} {
              obelisk.sv.expression.call attributes {argument_count = 1 : i64, callee_name = "$info", constraint_restrictions = [], defaulted_arguments = array<i64: 0>, has_inline_constraints = false, has_iterator_expression = false, has_output_arguments = false, has_this_class = false, is_signed = false, is_super_class = false, is_system_call = true, node_id = 27 : i64, semantic_type = !obelisk.void, subroutine_kind = 0 : i32, system_library_cell = "work.top", system_scope_path = "top", system_scope_symbol = @s1.$root::@s3.top::@s4.top} {
                obelisk.sv.expression.string_literal attributes {constant_value = "u0-pass", is_signed = false, node_id = 28 : i64, semantic_type = !obelisk.ranged_packed_array<55 : 0 x !obelisk.integral<1, false, false, 0 : 0, bit>>} {
                }
              }
            }
            obelisk.sv.statement.expression_statement attributes {node_id = 29 : i64} {
              obelisk.sv.expression.call attributes {argument_count = 1 : i64, callee_name = "$info", constraint_restrictions = [], defaulted_arguments = array<i64: 0>, has_inline_constraints = false, has_iterator_expression = false, has_output_arguments = false, has_this_class = false, is_signed = false, is_super_class = false, is_system_call = true, node_id = 30 : i64, semantic_type = !obelisk.void, subroutine_kind = 0 : i32, system_library_cell = "work.top", system_scope_path = "top", system_scope_symbol = @s1.$root::@s3.top::@s4.top} {
                obelisk.sv.expression.string_literal attributes {constant_value = "u0-fail", is_signed = false, node_id = 31 : i64, semantic_type = !obelisk.ranged_packed_array<55 : 0 x !obelisk.integral<1, false, false, 0 : 0, bit>>} {
                }
              }
            }
          }
        }
        obelisk.sv.symbol.procedural_block attributes {hierarchical_name = "top", node_id = 32 : i64, procedure_kind = 2 : i32, sym_name = "s16", time_precision_fs = 1000000 : i64, time_unit_fs = 1000000 : i64} {
          obelisk.sv.statement.concurrent_assertion attributes {assertion_kind = 2 : i32, has_default_disable = false, has_fail_action = false, has_pass_action = true, node_id = 33 : i64} {
            obelisk.sv.assertion.clocking attributes {node_id = 34 : i64} {
              obelisk.sv.timing.signal_event attributes {edge_kind = 1 : i32, has_iff = false, node_id = 35 : i64} {
                obelisk.sv.expression.named_value attributes {is_signed = false, node_id = 36 : i64, referenced_path = "top.clk", referenced_symbol = @s1.$root::@s3.top::@s4.top::@s6.clk, semantic_type = !obelisk.integral<1, false, true, 0 : 0, logic>} {
                }
              }
              obelisk.sv.assertion.unary attributes {has_range = false, node_id = 37 : i64, operator_kind = 0 : i32, range_is_unbounded = false} {
                obelisk.sv.assertion.binary attributes {node_id = 38 : i64, operator_kind = 11 : i32} {
                  obelisk.sv.assertion.simple attributes {has_repetition = false, is_null = false, node_id = 39 : i64, repetition_is_unbounded = false} {
                    obelisk.sv.expression.named_value attributes {is_signed = false, node_id = 40 : i64, referenced_path = "top.a", referenced_symbol = @s1.$root::@s3.top::@s4.top::@s8.a, semantic_type = !obelisk.integral<1, false, true, 0 : 0, logic>} {
                    }
                  }
                  obelisk.sv.assertion.simple attributes {has_repetition = false, is_null = false, node_id = 41 : i64, repetition_is_unbounded = false} {
                    obelisk.sv.expression.named_value attributes {is_signed = false, node_id = 42 : i64, referenced_path = "top.b", referenced_symbol = @s1.$root::@s3.top::@s4.top::@s10.b, semantic_type = !obelisk.integral<1, false, true, 0 : 0, logic>} {
                    }
                  }
                }
              }
            }
            obelisk.sv.statement.expression_statement attributes {node_id = 43 : i64} {
              obelisk.sv.expression.call attributes {argument_count = 1 : i64, callee_name = "$info", constraint_restrictions = [], defaulted_arguments = array<i64: 0>, has_inline_constraints = false, has_iterator_expression = false, has_output_arguments = false, has_this_class = false, is_signed = false, is_super_class = false, is_system_call = true, node_id = 44 : i64, semantic_type = !obelisk.void, subroutine_kind = 0 : i32, system_library_cell = "work.top", system_scope_path = "top", system_scope_symbol = @s1.$root::@s3.top::@s4.top} {
                obelisk.sv.expression.string_literal attributes {constant_value = "u1-hit", is_signed = false, node_id = 45 : i64, semantic_type = !obelisk.ranged_packed_array<47 : 0 x !obelisk.integral<1, false, false, 0 : 0, bit>>} {
                }
              }
            }
          }
        }
        obelisk.sv.symbol.procedural_block attributes {hierarchical_name = "top", node_id = 46 : i64, procedure_kind = 2 : i32, sym_name = "s17", time_precision_fs = 1000000 : i64, time_unit_fs = 1000000 : i64} {
          obelisk.sv.statement.concurrent_assertion attributes {assertion_kind = 0 : i32, has_default_disable = false, has_fail_action = true, has_pass_action = true, node_id = 47 : i64} {
            obelisk.sv.assertion.clocking attributes {node_id = 48 : i64} {
              obelisk.sv.timing.signal_event attributes {edge_kind = 1 : i32, has_iff = false, node_id = 49 : i64} {
                obelisk.sv.expression.named_value attributes {is_signed = false, node_id = 50 : i64, referenced_path = "top.clk", referenced_symbol = @s1.$root::@s3.top::@s4.top::@s6.clk, semantic_type = !obelisk.integral<1, false, true, 0 : 0, logic>} {
                }
              }
              obelisk.sv.assertion.unary attributes {has_range = false, node_id = 51 : i64, operator_kind = 0 : i32, range_is_unbounded = false} {
                obelisk.sv.assertion.binary attributes {node_id = 52 : i64, operator_kind = 14 : i32} {
                  obelisk.sv.assertion.simple attributes {has_repetition = false, is_null = false, node_id = 53 : i64, repetition_is_unbounded = false} {
                    obelisk.sv.expression.named_value attributes {is_signed = false, node_id = 54 : i64, referenced_path = "top.a", referenced_symbol = @s1.$root::@s3.top::@s4.top::@s8.a, semantic_type = !obelisk.integral<1, false, true, 0 : 0, logic>} {
                    }
                  }
                  obelisk.sv.assertion.sequence_concat attributes {delays = [{is_unbounded = false, max = 0 : i64, min = 0 : i64}, {is_unbounded = false, max = 1 : i64, min = 1 : i64}], node_id = 55 : i64} {
                    obelisk.sv.assertion.simple attributes {has_repetition = false, is_null = false, node_id = 56 : i64, repetition_is_unbounded = false} {
                      obelisk.sv.expression.named_value attributes {is_signed = false, node_id = 57 : i64, referenced_path = "top.b", referenced_symbol = @s1.$root::@s3.top::@s4.top::@s10.b, semantic_type = !obelisk.integral<1, false, true, 0 : 0, logic>} {
                      }
                    }
                    obelisk.sv.assertion.simple attributes {has_repetition = false, is_null = false, node_id = 58 : i64, repetition_is_unbounded = false} {
                      obelisk.sv.expression.named_value attributes {is_signed = false, node_id = 59 : i64, referenced_path = "top.c", referenced_symbol = @s1.$root::@s3.top::@s4.top::@s12.c, semantic_type = !obelisk.integral<1, false, true, 0 : 0, logic>} {
                      }
                    }
                  }
                }
              }
            }
            obelisk.sv.statement.expression_statement attributes {node_id = 60 : i64} {
              obelisk.sv.expression.call attributes {argument_count = 1 : i64, callee_name = "$info", constraint_restrictions = [], defaulted_arguments = array<i64: 0>, has_inline_constraints = false, has_iterator_expression = false, has_output_arguments = false, has_this_class = false, is_signed = false, is_super_class = false, is_system_call = true, node_id = 61 : i64, semantic_type = !obelisk.void, subroutine_kind = 0 : i32, system_library_cell = "work.top", system_scope_path = "top", system_scope_symbol = @s1.$root::@s3.top::@s4.top} {
                obelisk.sv.expression.string_literal attributes {constant_value = "u2-pass", is_signed = false, node_id = 62 : i64, semantic_type = !obelisk.ranged_packed_array<55 : 0 x !obelisk.integral<1, false, false, 0 : 0, bit>>} {
                }
              }
            }
            obelisk.sv.statement.expression_statement attributes {node_id = 63 : i64} {
              obelisk.sv.expression.call attributes {argument_count = 1 : i64, callee_name = "$info", constraint_restrictions = [], defaulted_arguments = array<i64: 0>, has_inline_constraints = false, has_iterator_expression = false, has_output_arguments = false, has_this_class = false, is_signed = false, is_super_class = false, is_system_call = true, node_id = 64 : i64, semantic_type = !obelisk.void, subroutine_kind = 0 : i32, system_library_cell = "work.top", system_scope_path = "top", system_scope_symbol = @s1.$root::@s3.top::@s4.top} {
                obelisk.sv.expression.string_literal attributes {constant_value = "u2-fail", is_signed = false, node_id = 65 : i64, semantic_type = !obelisk.ranged_packed_array<55 : 0 x !obelisk.integral<1, false, false, 0 : 0, bit>>} {
                }
              }
            }
          }
        }
        obelisk.sv.symbol.procedural_block attributes {hierarchical_name = "top", node_id = 66 : i64, procedure_kind = 2 : i32, sym_name = "s18", time_precision_fs = 1000000 : i64, time_unit_fs = 1000000 : i64} {
          obelisk.sv.statement.concurrent_assertion attributes {assertion_kind = 2 : i32, has_default_disable = false, has_fail_action = false, has_pass_action = true, node_id = 67 : i64} {
            obelisk.sv.assertion.clocking attributes {node_id = 68 : i64} {
              obelisk.sv.timing.signal_event attributes {edge_kind = 1 : i32, has_iff = false, node_id = 69 : i64} {
                obelisk.sv.expression.named_value attributes {is_signed = false, node_id = 70 : i64, referenced_path = "top.clk", referenced_symbol = @s1.$root::@s3.top::@s4.top::@s6.clk, semantic_type = !obelisk.integral<1, false, true, 0 : 0, logic>} {
                }
              }
              obelisk.sv.assertion.unary attributes {has_range = false, node_id = 71 : i64, operator_kind = 0 : i32, range_is_unbounded = false} {
                obelisk.sv.assertion.binary attributes {node_id = 72 : i64, operator_kind = 12 : i32} {
                  obelisk.sv.assertion.simple attributes {has_repetition = false, is_null = false, node_id = 73 : i64, repetition_is_unbounded = false} {
                    obelisk.sv.expression.named_value attributes {is_signed = false, node_id = 74 : i64, referenced_path = "top.a", referenced_symbol = @s1.$root::@s3.top::@s4.top::@s8.a, semantic_type = !obelisk.integral<1, false, true, 0 : 0, logic>} {
                    }
                  }
                  obelisk.sv.assertion.sequence_concat attributes {delays = [{is_unbounded = false, max = 0 : i64, min = 0 : i64}, {is_unbounded = false, max = 1 : i64, min = 1 : i64}], node_id = 75 : i64} {
                    obelisk.sv.assertion.simple attributes {has_repetition = false, is_null = false, node_id = 76 : i64, repetition_is_unbounded = false} {
                      obelisk.sv.expression.named_value attributes {is_signed = false, node_id = 77 : i64, referenced_path = "top.b", referenced_symbol = @s1.$root::@s3.top::@s4.top::@s10.b, semantic_type = !obelisk.integral<1, false, true, 0 : 0, logic>} {
                      }
                    }
                    obelisk.sv.assertion.simple attributes {has_repetition = false, is_null = false, node_id = 78 : i64, repetition_is_unbounded = false} {
                      obelisk.sv.expression.named_value attributes {is_signed = false, node_id = 79 : i64, referenced_path = "top.c", referenced_symbol = @s1.$root::@s3.top::@s4.top::@s12.c, semantic_type = !obelisk.integral<1, false, true, 0 : 0, logic>} {
                      }
                    }
                  }
                }
              }
            }
            obelisk.sv.statement.expression_statement attributes {node_id = 80 : i64} {
              obelisk.sv.expression.call attributes {argument_count = 1 : i64, callee_name = "$info", constraint_restrictions = [], defaulted_arguments = array<i64: 0>, has_inline_constraints = false, has_iterator_expression = false, has_output_arguments = false, has_this_class = false, is_signed = false, is_super_class = false, is_system_call = true, node_id = 81 : i64, semantic_type = !obelisk.void, subroutine_kind = 0 : i32, system_library_cell = "work.top", system_scope_path = "top", system_scope_symbol = @s1.$root::@s3.top::@s4.top} {
                obelisk.sv.expression.string_literal attributes {constant_value = "u3-hit", is_signed = false, node_id = 82 : i64, semantic_type = !obelisk.ranged_packed_array<47 : 0 x !obelisk.integral<1, false, false, 0 : 0, bit>>} {
                }
              }
            }
          }
        }
        obelisk.sv.symbol.procedural_block attributes {hierarchical_name = "top", node_id = 83 : i64, procedure_kind = 2 : i32, sym_name = "s19", time_precision_fs = 1000000 : i64, time_unit_fs = 1000000 : i64} {
          obelisk.sv.statement.concurrent_assertion attributes {assertion_kind = 0 : i32, has_default_disable = false, has_fail_action = true, has_pass_action = true, node_id = 84 : i64} {
            obelisk.sv.assertion.clocking attributes {node_id = 85 : i64} {
              obelisk.sv.timing.signal_event attributes {edge_kind = 1 : i32, has_iff = false, node_id = 86 : i64} {
                obelisk.sv.expression.named_value attributes {is_signed = false, node_id = 87 : i64, referenced_path = "top.clk", referenced_symbol = @s1.$root::@s3.top::@s4.top::@s6.clk, semantic_type = !obelisk.integral<1, false, true, 0 : 0, logic>} {
                }
              }
              obelisk.sv.assertion.unary attributes {has_range = false, node_id = 88 : i64, operator_kind = 0 : i32, range_is_unbounded = false} {
                obelisk.sv.assertion.binary attributes {node_id = 89 : i64, operator_kind = 11 : i32} {
                  obelisk.sv.assertion.simple attributes {has_repetition = false, is_null = false, node_id = 90 : i64, repetition_is_unbounded = false} {
                    obelisk.sv.expression.named_value attributes {is_signed = false, node_id = 91 : i64, referenced_path = "top.a", referenced_symbol = @s1.$root::@s3.top::@s4.top::@s8.a, semantic_type = !obelisk.integral<1, false, true, 0 : 0, logic>} {
                    }
                  }
                  obelisk.sv.assertion.binary attributes {node_id = 92 : i64, operator_kind = 1 : i32} {
                    obelisk.sv.assertion.sequence_concat attributes {delays = [{is_unbounded = false, max = 0 : i64, min = 0 : i64}, {is_unbounded = false, max = 1 : i64, min = 1 : i64}], node_id = 93 : i64} {
                      obelisk.sv.assertion.simple attributes {has_repetition = false, is_null = false, node_id = 94 : i64, repetition_is_unbounded = false} {
                        obelisk.sv.expression.named_value attributes {is_signed = false, node_id = 95 : i64, referenced_path = "top.b", referenced_symbol = @s1.$root::@s3.top::@s4.top::@s10.b, semantic_type = !obelisk.integral<1, false, true, 0 : 0, logic>} {
                        }
                      }
                      obelisk.sv.assertion.simple attributes {has_repetition = false, is_null = false, node_id = 96 : i64, repetition_is_unbounded = false} {
                        obelisk.sv.expression.named_value attributes {is_signed = false, node_id = 97 : i64, referenced_path = "top.c", referenced_symbol = @s1.$root::@s3.top::@s4.top::@s12.c, semantic_type = !obelisk.integral<1, false, true, 0 : 0, logic>} {
                        }
                      }
                    }
                    obelisk.sv.assertion.sequence_concat attributes {delays = [{is_unbounded = false, max = 0 : i64, min = 0 : i64}, {is_unbounded = false, max = 2 : i64, min = 2 : i64}], node_id = 98 : i64} {
                      obelisk.sv.assertion.simple attributes {has_repetition = false, is_null = false, node_id = 99 : i64, repetition_is_unbounded = false} {
                        obelisk.sv.expression.named_value attributes {is_signed = false, node_id = 100 : i64, referenced_path = "top.c", referenced_symbol = @s1.$root::@s3.top::@s4.top::@s12.c, semantic_type = !obelisk.integral<1, false, true, 0 : 0, logic>} {
                        }
                      }
                      obelisk.sv.assertion.simple attributes {has_repetition = false, is_null = false, node_id = 101 : i64, repetition_is_unbounded = false} {
                        obelisk.sv.expression.named_value attributes {is_signed = false, node_id = 102 : i64, referenced_path = "top.d", referenced_symbol = @s1.$root::@s3.top::@s4.top::@s14.d, semantic_type = !obelisk.integral<1, false, true, 0 : 0, logic>} {
                        }
                      }
                    }
                  }
                }
              }
            }
            obelisk.sv.statement.expression_statement attributes {node_id = 103 : i64} {
              obelisk.sv.expression.call attributes {argument_count = 1 : i64, callee_name = "$info", constraint_restrictions = [], defaulted_arguments = array<i64: 0>, has_inline_constraints = false, has_iterator_expression = false, has_output_arguments = false, has_this_class = false, is_signed = false, is_super_class = false, is_system_call = true, node_id = 104 : i64, semantic_type = !obelisk.void, subroutine_kind = 0 : i32, system_library_cell = "work.top", system_scope_path = "top", system_scope_symbol = @s1.$root::@s3.top::@s4.top} {
                obelisk.sv.expression.string_literal attributes {constant_value = "u4-pass", is_signed = false, node_id = 105 : i64, semantic_type = !obelisk.ranged_packed_array<55 : 0 x !obelisk.integral<1, false, false, 0 : 0, bit>>} {
                }
              }
            }
            obelisk.sv.statement.expression_statement attributes {node_id = 106 : i64} {
              obelisk.sv.expression.call attributes {argument_count = 1 : i64, callee_name = "$info", constraint_restrictions = [], defaulted_arguments = array<i64: 0>, has_inline_constraints = false, has_iterator_expression = false, has_output_arguments = false, has_this_class = false, is_signed = false, is_super_class = false, is_system_call = true, node_id = 107 : i64, semantic_type = !obelisk.void, subroutine_kind = 0 : i32, system_library_cell = "work.top", system_scope_path = "top", system_scope_symbol = @s1.$root::@s3.top::@s4.top} {
                obelisk.sv.expression.string_literal attributes {constant_value = "u4-fail", is_signed = false, node_id = 108 : i64, semantic_type = !obelisk.ranged_packed_array<55 : 0 x !obelisk.integral<1, false, false, 0 : 0, bit>>} {
                }
              }
            }
          }
        }
        obelisk.sv.symbol.procedural_block attributes {hierarchical_name = "top", node_id = 109 : i64, procedure_kind = 2 : i32, sym_name = "s20", time_precision_fs = 1000000 : i64, time_unit_fs = 1000000 : i64} {
          obelisk.sv.statement.concurrent_assertion attributes {assertion_kind = 2 : i32, has_default_disable = false, has_fail_action = false, has_pass_action = true, node_id = 110 : i64} {
            obelisk.sv.assertion.clocking attributes {node_id = 111 : i64} {
              obelisk.sv.timing.signal_event attributes {edge_kind = 1 : i32, has_iff = false, node_id = 112 : i64} {
                obelisk.sv.expression.named_value attributes {is_signed = false, node_id = 113 : i64, referenced_path = "top.clk", referenced_symbol = @s1.$root::@s3.top::@s4.top::@s6.clk, semantic_type = !obelisk.integral<1, false, true, 0 : 0, logic>} {
                }
              }
              obelisk.sv.assertion.unary attributes {has_range = false, node_id = 114 : i64, operator_kind = 0 : i32, range_is_unbounded = false} {
                obelisk.sv.assertion.binary attributes {node_id = 115 : i64, operator_kind = 11 : i32} {
                  obelisk.sv.assertion.simple attributes {has_repetition = false, is_null = false, node_id = 116 : i64, repetition_is_unbounded = false} {
                    obelisk.sv.expression.named_value attributes {is_signed = false, node_id = 117 : i64, referenced_path = "top.a", referenced_symbol = @s1.$root::@s3.top::@s4.top::@s8.a, semantic_type = !obelisk.integral<1, false, true, 0 : 0, logic>} {
                    }
                  }
                  obelisk.sv.assertion.sequence_concat attributes {delays = [{is_unbounded = false, max = 0 : i64, min = 0 : i64}, {is_unbounded = false, max = 1 : i64, min = 1 : i64}, {is_unbounded = true, min = 1 : i64}], node_id = 118 : i64} {
                    obelisk.sv.assertion.simple attributes {has_repetition = false, is_null = false, node_id = 119 : i64, repetition_is_unbounded = false} {
                      obelisk.sv.expression.named_value attributes {is_signed = false, node_id = 120 : i64, referenced_path = "top.b", referenced_symbol = @s1.$root::@s3.top::@s4.top::@s10.b, semantic_type = !obelisk.integral<1, false, true, 0 : 0, logic>} {
                      }
                    }
                    obelisk.sv.assertion.simple attributes {has_repetition = false, is_null = false, node_id = 121 : i64, repetition_is_unbounded = false} {
                      obelisk.sv.expression.named_value attributes {is_signed = false, node_id = 122 : i64, referenced_path = "top.c", referenced_symbol = @s1.$root::@s3.top::@s4.top::@s12.c, semantic_type = !obelisk.integral<1, false, true, 0 : 0, logic>} {
                      }
                    }
                    obelisk.sv.assertion.simple attributes {has_repetition = false, is_null = false, node_id = 123 : i64, repetition_is_unbounded = false} {
                      obelisk.sv.expression.named_value attributes {is_signed = false, node_id = 124 : i64, referenced_path = "top.d", referenced_symbol = @s1.$root::@s3.top::@s4.top::@s14.d, semantic_type = !obelisk.integral<1, false, true, 0 : 0, logic>} {
                      }
                    }
                  }
                }
              }
            }
            obelisk.sv.statement.expression_statement attributes {node_id = 125 : i64} {
              obelisk.sv.expression.call attributes {argument_count = 1 : i64, callee_name = "$info", constraint_restrictions = [], defaulted_arguments = array<i64: 0>, has_inline_constraints = false, has_iterator_expression = false, has_output_arguments = false, has_this_class = false, is_signed = false, is_super_class = false, is_system_call = true, node_id = 126 : i64, semantic_type = !obelisk.void, subroutine_kind = 0 : i32, system_library_cell = "work.top", system_scope_path = "top", system_scope_symbol = @s1.$root::@s3.top::@s4.top} {
                obelisk.sv.expression.string_literal attributes {constant_value = "u5-hit", is_signed = false, node_id = 127 : i64, semantic_type = !obelisk.ranged_packed_array<47 : 0 x !obelisk.integral<1, false, false, 0 : 0, bit>>} {
                }
              }
            }
          }
        }
        obelisk.sv.symbol.procedural_block attributes {hierarchical_name = "top", node_id = 128 : i64, procedure_kind = 2 : i32, sym_name = "s21", time_precision_fs = 1000000 : i64, time_unit_fs = 1000000 : i64} {
          obelisk.sv.statement.concurrent_assertion attributes {assertion_kind = 0 : i32, has_default_disable = false, has_fail_action = true, has_pass_action = true, node_id = 129 : i64} {
            obelisk.sv.assertion.clocking attributes {node_id = 130 : i64} {
              obelisk.sv.timing.signal_event attributes {edge_kind = 1 : i32, has_iff = false, node_id = 131 : i64} {
                obelisk.sv.expression.named_value attributes {is_signed = false, node_id = 132 : i64, referenced_path = "top.clk", referenced_symbol = @s1.$root::@s3.top::@s4.top::@s6.clk, semantic_type = !obelisk.integral<1, false, true, 0 : 0, logic>} {
                }
              }
              obelisk.sv.assertion.disable_iff attributes {node_id = 133 : i64} {
                obelisk.sv.expression.named_value attributes {is_signed = false, node_id = 134 : i64, referenced_path = "top.d", referenced_symbol = @s1.$root::@s3.top::@s4.top::@s14.d, semantic_type = !obelisk.integral<1, false, true, 0 : 0, logic>} {
                }
                obelisk.sv.assertion.unary attributes {has_range = false, node_id = 135 : i64, operator_kind = 0 : i32, range_is_unbounded = false} {
                  obelisk.sv.assertion.binary attributes {node_id = 136 : i64, operator_kind = 12 : i32} {
                    obelisk.sv.assertion.simple attributes {has_repetition = false, is_null = false, node_id = 137 : i64, repetition_is_unbounded = false} {
                      obelisk.sv.expression.named_value attributes {is_signed = false, node_id = 138 : i64, referenced_path = "top.a", referenced_symbol = @s1.$root::@s3.top::@s4.top::@s8.a, semantic_type = !obelisk.integral<1, false, true, 0 : 0, logic>} {
                      }
                    }
                    obelisk.sv.assertion.sequence_concat attributes {delays = [{is_unbounded = false, max = 0 : i64, min = 0 : i64}, {is_unbounded = false, max = 1 : i64, min = 1 : i64}], node_id = 139 : i64} {
                      obelisk.sv.assertion.simple attributes {has_repetition = false, is_null = false, node_id = 140 : i64, repetition_is_unbounded = false} {
                        obelisk.sv.expression.named_value attributes {is_signed = false, node_id = 141 : i64, referenced_path = "top.b", referenced_symbol = @s1.$root::@s3.top::@s4.top::@s10.b, semantic_type = !obelisk.integral<1, false, true, 0 : 0, logic>} {
                        }
                      }
                      obelisk.sv.assertion.simple attributes {has_repetition = false, is_null = false, node_id = 142 : i64, repetition_is_unbounded = false} {
                        obelisk.sv.expression.named_value attributes {is_signed = false, node_id = 143 : i64, referenced_path = "top.c", referenced_symbol = @s1.$root::@s3.top::@s4.top::@s12.c, semantic_type = !obelisk.integral<1, false, true, 0 : 0, logic>} {
                        }
                      }
                    }
                  }
                }
              }
            }
            obelisk.sv.statement.expression_statement attributes {node_id = 144 : i64} {
              obelisk.sv.expression.call attributes {argument_count = 1 : i64, callee_name = "$info", constraint_restrictions = [], defaulted_arguments = array<i64: 0>, has_inline_constraints = false, has_iterator_expression = false, has_output_arguments = false, has_this_class = false, is_signed = false, is_super_class = false, is_system_call = true, node_id = 145 : i64, semantic_type = !obelisk.void, subroutine_kind = 0 : i32, system_library_cell = "work.top", system_scope_path = "top", system_scope_symbol = @s1.$root::@s3.top::@s4.top} {
                obelisk.sv.expression.string_literal attributes {constant_value = "u6-pass", is_signed = false, node_id = 146 : i64, semantic_type = !obelisk.ranged_packed_array<55 : 0 x !obelisk.integral<1, false, false, 0 : 0, bit>>} {
                }
              }
            }
            obelisk.sv.statement.expression_statement attributes {node_id = 147 : i64} {
              obelisk.sv.expression.call attributes {argument_count = 1 : i64, callee_name = "$info", constraint_restrictions = [], defaulted_arguments = array<i64: 0>, has_inline_constraints = false, has_iterator_expression = false, has_output_arguments = false, has_this_class = false, is_signed = false, is_super_class = false, is_system_call = true, node_id = 148 : i64, semantic_type = !obelisk.void, subroutine_kind = 0 : i32, system_library_cell = "work.top", system_scope_path = "top", system_scope_symbol = @s1.$root::@s3.top::@s4.top} {
                obelisk.sv.expression.string_literal attributes {constant_value = "u6-fail", is_signed = false, node_id = 149 : i64, semantic_type = !obelisk.ranged_packed_array<55 : 0 x !obelisk.integral<1, false, false, 0 : 0, bit>>} {
                }
              }
            }
          }
        }
        obelisk.sv.symbol.procedural_block attributes {hierarchical_name = "top", node_id = 150 : i64, procedure_kind = 2 : i32, sym_name = "s22", time_precision_fs = 1000000 : i64, time_unit_fs = 1000000 : i64} {
          obelisk.sv.statement.concurrent_assertion attributes {assertion_kind = 2 : i32, has_default_disable = false, has_fail_action = false, has_pass_action = true, node_id = 151 : i64} {
            obelisk.sv.assertion.clocking attributes {node_id = 152 : i64} {
              obelisk.sv.timing.signal_event attributes {edge_kind = 1 : i32, has_iff = false, node_id = 153 : i64} {
                obelisk.sv.expression.named_value attributes {is_signed = false, node_id = 154 : i64, referenced_path = "top.clk", referenced_symbol = @s1.$root::@s3.top::@s4.top::@s6.clk, semantic_type = !obelisk.integral<1, false, true, 0 : 0, logic>} {
                }
              }
              obelisk.sv.assertion.unary attributes {has_range = false, node_id = 155 : i64, operator_kind = 0 : i32, range_is_unbounded = false} {
                obelisk.sv.assertion.binary attributes {node_id = 156 : i64, operator_kind = 14 : i32} {
                  obelisk.sv.assertion.simple attributes {has_repetition = false, is_null = false, node_id = 157 : i64, repetition_is_unbounded = false} {
                    obelisk.sv.expression.named_value attributes {is_signed = false, node_id = 158 : i64, referenced_path = "top.a", referenced_symbol = @s1.$root::@s3.top::@s4.top::@s8.a, semantic_type = !obelisk.integral<1, false, true, 0 : 0, logic>} {
                    }
                  }
                  obelisk.sv.assertion.simple attributes {has_repetition = false, is_null = false, node_id = 159 : i64, repetition_is_unbounded = false} {
                    obelisk.sv.expression.named_value attributes {is_signed = false, node_id = 160 : i64, referenced_path = "top.b", referenced_symbol = @s1.$root::@s3.top::@s4.top::@s10.b, semantic_type = !obelisk.integral<1, false, true, 0 : 0, logic>} {
                    }
                  }
                }
              }
            }
            obelisk.sv.statement.expression_statement attributes {node_id = 161 : i64} {
              obelisk.sv.expression.call attributes {argument_count = 1 : i64, callee_name = "$info", constraint_restrictions = [], defaulted_arguments = array<i64: 0>, has_inline_constraints = false, has_iterator_expression = false, has_output_arguments = false, has_this_class = false, is_signed = false, is_super_class = false, is_system_call = true, node_id = 162 : i64, semantic_type = !obelisk.void, subroutine_kind = 0 : i32, system_library_cell = "work.top", system_scope_path = "top", system_scope_symbol = @s1.$root::@s3.top::@s4.top} {
                obelisk.sv.expression.string_literal attributes {constant_value = "u7-hit", is_signed = false, node_id = 163 : i64, semantic_type = !obelisk.ranged_packed_array<47 : 0 x !obelisk.integral<1, false, false, 0 : 0, bit>>} {
                }
              }
            }
          }
        }
        obelisk.sv.symbol.procedural_block attributes {hierarchical_name = "top", node_id = 164 : i64, procedure_kind = 2 : i32, sym_name = "s23", time_precision_fs = 1000000 : i64, time_unit_fs = 1000000 : i64} {
          obelisk.sv.statement.concurrent_assertion attributes {assertion_kind = 2 : i32, has_default_disable = false, has_fail_action = false, has_pass_action = true, node_id = 165 : i64} {
            obelisk.sv.assertion.clocking attributes {node_id = 166 : i64} {
              obelisk.sv.timing.signal_event attributes {edge_kind = 1 : i32, has_iff = false, node_id = 167 : i64} {
                obelisk.sv.expression.named_value attributes {is_signed = false, node_id = 168 : i64, referenced_path = "top.clk", referenced_symbol = @s1.$root::@s3.top::@s4.top::@s6.clk, semantic_type = !obelisk.integral<1, false, true, 0 : 0, logic>} {
                }
              }
              obelisk.sv.assertion.unary attributes {has_range = false, node_id = 169 : i64, operator_kind = 0 : i32, range_is_unbounded = false} {
                obelisk.sv.assertion.binary attributes {node_id = 170 : i64, operator_kind = 13 : i32} {
                  obelisk.sv.assertion.simple attributes {has_repetition = false, is_null = false, node_id = 171 : i64, repetition_is_unbounded = false} {
                    obelisk.sv.expression.named_value attributes {is_signed = false, node_id = 172 : i64, referenced_path = "top.a", referenced_symbol = @s1.$root::@s3.top::@s4.top::@s8.a, semantic_type = !obelisk.integral<1, false, true, 0 : 0, logic>} {
                    }
                  }
                  obelisk.sv.assertion.binary attributes {node_id = 173 : i64, operator_kind = 1 : i32} {
                    obelisk.sv.assertion.sequence_concat attributes {delays = [{is_unbounded = false, max = 0 : i64, min = 0 : i64}, {is_unbounded = false, max = 1 : i64, min = 1 : i64}], node_id = 174 : i64} {
                      obelisk.sv.assertion.simple attributes {has_repetition = false, is_null = false, node_id = 175 : i64, repetition_is_unbounded = false} {
                        obelisk.sv.expression.named_value attributes {is_signed = false, node_id = 176 : i64, referenced_path = "top.b", referenced_symbol = @s1.$root::@s3.top::@s4.top::@s10.b, semantic_type = !obelisk.integral<1, false, true, 0 : 0, logic>} {
                        }
                      }
                      obelisk.sv.assertion.simple attributes {has_repetition = false, is_null = false, node_id = 177 : i64, repetition_is_unbounded = false} {
                        obelisk.sv.expression.named_value attributes {is_signed = false, node_id = 178 : i64, referenced_path = "top.c", referenced_symbol = @s1.$root::@s3.top::@s4.top::@s12.c, semantic_type = !obelisk.integral<1, false, true, 0 : 0, logic>} {
                        }
                      }
                    }
                    obelisk.sv.assertion.sequence_concat attributes {delays = [{is_unbounded = false, max = 0 : i64, min = 0 : i64}, {is_unbounded = false, max = 2 : i64, min = 2 : i64}], node_id = 179 : i64} {
                      obelisk.sv.assertion.simple attributes {has_repetition = false, is_null = false, node_id = 180 : i64, repetition_is_unbounded = false} {
                        obelisk.sv.expression.named_value attributes {is_signed = false, node_id = 181 : i64, referenced_path = "top.c", referenced_symbol = @s1.$root::@s3.top::@s4.top::@s12.c, semantic_type = !obelisk.integral<1, false, true, 0 : 0, logic>} {
                        }
                      }
                      obelisk.sv.assertion.simple attributes {has_repetition = false, is_null = false, node_id = 182 : i64, repetition_is_unbounded = false} {
                        obelisk.sv.expression.named_value attributes {is_signed = false, node_id = 183 : i64, referenced_path = "top.d", referenced_symbol = @s1.$root::@s3.top::@s4.top::@s14.d, semantic_type = !obelisk.integral<1, false, true, 0 : 0, logic>} {
                        }
                      }
                    }
                  }
                }
              }
            }
            obelisk.sv.statement.expression_statement attributes {node_id = 184 : i64} {
              obelisk.sv.expression.call attributes {argument_count = 1 : i64, callee_name = "$info", constraint_restrictions = [], defaulted_arguments = array<i64: 0>, has_inline_constraints = false, has_iterator_expression = false, has_output_arguments = false, has_this_class = false, is_signed = false, is_super_class = false, is_system_call = true, node_id = 185 : i64, semantic_type = !obelisk.void, subroutine_kind = 0 : i32, system_library_cell = "work.top", system_scope_path = "top", system_scope_symbol = @s1.$root::@s3.top::@s4.top} {
                obelisk.sv.expression.string_literal attributes {constant_value = "u8-hit", is_signed = false, node_id = 186 : i64, semantic_type = !obelisk.ranged_packed_array<47 : 0 x !obelisk.integral<1, false, false, 0 : 0, bit>>} {
                }
              }
            }
          }
        }
        obelisk.sv.symbol.procedural_block attributes {hierarchical_name = "top", node_id = 187 : i64, procedure_kind = 2 : i32, sym_name = "s24", time_precision_fs = 1000000 : i64, time_unit_fs = 1000000 : i64} {
          obelisk.sv.statement.concurrent_assertion attributes {assertion_kind = 2 : i32, has_default_disable = false, has_fail_action = false, has_pass_action = true, node_id = 188 : i64} {
            obelisk.sv.assertion.clocking attributes {node_id = 189 : i64} {
              obelisk.sv.timing.signal_event attributes {edge_kind = 1 : i32, has_iff = false, node_id = 190 : i64} {
                obelisk.sv.expression.named_value attributes {is_signed = false, node_id = 191 : i64, referenced_path = "top.clk", referenced_symbol = @s1.$root::@s3.top::@s4.top::@s6.clk, semantic_type = !obelisk.integral<1, false, true, 0 : 0, logic>} {
                }
              }
              obelisk.sv.assertion.unary attributes {has_range = false, node_id = 192 : i64, operator_kind = 0 : i32, range_is_unbounded = false} {
                obelisk.sv.assertion.binary attributes {node_id = 193 : i64, operator_kind = 14 : i32} {
                  obelisk.sv.assertion.simple attributes {has_repetition = false, is_null = false, node_id = 194 : i64, repetition_is_unbounded = false} {
                    obelisk.sv.expression.named_value attributes {is_signed = false, node_id = 195 : i64, referenced_path = "top.a", referenced_symbol = @s1.$root::@s3.top::@s4.top::@s8.a, semantic_type = !obelisk.integral<1, false, true, 0 : 0, logic>} {
                    }
                  }
                  obelisk.sv.assertion.sequence_concat attributes {delays = [{is_unbounded = false, max = 0 : i64, min = 0 : i64}, {is_unbounded = false, max = 1 : i64, min = 1 : i64}, {is_unbounded = true, min = 1 : i64}], node_id = 196 : i64} {
                    obelisk.sv.assertion.simple attributes {has_repetition = false, is_null = false, node_id = 197 : i64, repetition_is_unbounded = false} {
                      obelisk.sv.expression.named_value attributes {is_signed = false, node_id = 198 : i64, referenced_path = "top.b", referenced_symbol = @s1.$root::@s3.top::@s4.top::@s10.b, semantic_type = !obelisk.integral<1, false, true, 0 : 0, logic>} {
                      }
                    }
                    obelisk.sv.assertion.simple attributes {has_repetition = false, is_null = false, node_id = 199 : i64, repetition_is_unbounded = false} {
                      obelisk.sv.expression.named_value attributes {is_signed = false, node_id = 200 : i64, referenced_path = "top.c", referenced_symbol = @s1.$root::@s3.top::@s4.top::@s12.c, semantic_type = !obelisk.integral<1, false, true, 0 : 0, logic>} {
                      }
                    }
                    obelisk.sv.assertion.simple attributes {has_repetition = false, is_null = false, node_id = 201 : i64, repetition_is_unbounded = false} {
                      obelisk.sv.expression.named_value attributes {is_signed = false, node_id = 202 : i64, referenced_path = "top.d", referenced_symbol = @s1.$root::@s3.top::@s4.top::@s14.d, semantic_type = !obelisk.integral<1, false, true, 0 : 0, logic>} {
                      }
                    }
                  }
                }
              }
            }
            obelisk.sv.statement.expression_statement attributes {node_id = 203 : i64} {
              obelisk.sv.expression.call attributes {argument_count = 1 : i64, callee_name = "$info", constraint_restrictions = [], defaulted_arguments = array<i64: 0>, has_inline_constraints = false, has_iterator_expression = false, has_output_arguments = false, has_this_class = false, is_signed = false, is_super_class = false, is_system_call = true, node_id = 204 : i64, semantic_type = !obelisk.void, subroutine_kind = 0 : i32, system_library_cell = "work.top", system_scope_path = "top", system_scope_symbol = @s1.$root::@s3.top::@s4.top} {
                obelisk.sv.expression.string_literal attributes {constant_value = "u9-hit", is_signed = false, node_id = 205 : i64, semantic_type = !obelisk.ranged_packed_array<47 : 0 x !obelisk.integral<1, false, false, 0 : 0, bit>>} {
                }
              }
            }
          }
        }
      }
    }
  }
}

// One-cycle overlapped implication has no live state. A false antecedent is an
// operand-vacuous success, hence an outer failure; consequent failure becomes
// the sole outer pass, and consequent success becomes an outer failure.
// CHECK-NOT: @unit_0.$concurrent_eos
// CHECK-LABEL: obelisk_sim.func private @unit_0(
// CHECK-SAME: obelisk_sim.end_of_simulation_strength = "strong"
// CHECK-SAME: obelisk_sim.negated_operand_end_of_simulation_strength = "weak"
// CHECK-SAME: obelisk_sim.temporal_property_negation
// CHECK-NOT: obelisk_sim.ref.alloc
// CHECK: obelisk_sim.assert.sampled_read
// CHECK: obelisk_sim.spawn @unit_0.fork.16.1.1
// CHECK: obelisk_sim.assert.sampled_read
// CHECK: obelisk_sim.spawn @unit_0.fork.16.0.0
// CHECK: obelisk_sim.spawn @unit_0.fork.16.1.1
// CHECK-NOT: obelisk_sim.spawn @unit_0.fork

// Cover sees only the nonvacuous inverted consequent failure. Neither a false
// implication antecedent nor a successful consequent creates a hit.
// CHECK-NOT: @unit_1.$concurrent_eos
// CHECK-LABEL: obelisk_sim.func private @unit_1(
// CHECK-SAME: obelisk_sim.end_of_simulation_strength = "weak"
// CHECK-SAME: obelisk_sim.negated_operand_end_of_simulation_strength = "strong"
// CHECK-SAME: obelisk_sim.temporal_property_negation
// CHECK-NOT: obelisk_sim.ref.alloc
// CHECK: obelisk_sim.spawn @unit_1.fork.33.0.0
// CHECK-NOT: obelisk_sim.spawn @unit_1.fork

// Nonoverlapped followed-by retains the matched-antecedent handoff in bit zero.
// Both that handoff and the older pending consequent are finalized as outer
// strong failures at EOS. Live prefix/consequent failures invert to passes,
// consequent success to failure, and a false followed-by antecedent to pass.
// CHECK-LABEL: obelisk_sim.func private @unit_2.$concurrent_eos_report.47.strong(
// CHECK: obelisk_sim.bytes.constant "u2-fail"
// CHECK-LABEL: obelisk_sim.func private @unit_2.$concurrent_eos.47.strong(
// CHECK-COUNT-1: obelisk_sim.ref.load
// CHECK-NOT: obelisk_sim.ref.load
// CHECK-COUNT-2: obelisk_sim.spawn @unit_2.$concurrent_eos_report.47.strong
// CHECK-NOT: obelisk_sim.spawn @unit_2.$concurrent_eos_report.47.strong
// CHECK-LABEL: obelisk_sim.func private @unit_2(
// CHECK-SAME: obelisk_sim.end_of_simulation_strength = "strong"
// CHECK-SAME: obelisk_sim.followed_by_monitor
// CHECK-SAME: obelisk_sim.negated_operand_end_of_simulation_strength = "weak"
// CHECK-SAME: obelisk_sim.temporal_property_negation
// CHECK-COUNT-1: obelisk_sim.ref.alloc
// CHECK-NOT: obelisk_sim.ref.alloc
// CHECK: obelisk_sim.spawn @unit_2.$concurrent_eos.47.strong
// CHECK: obelisk_sim.spawn @unit_2.fork.47.0.0
// CHECK: obelisk_sim.spawn @unit_2.fork.47.0.0
// CHECK: obelisk_sim.spawn @unit_2.fork.47.1.1
// CHECK: obelisk_sim.spawn @unit_2.fork.47.0.0
// CHECK-NOT: obelisk_sim.spawn @unit_2.fork

// The same nonoverlap handoff is a pending strong operand consequence for
// cover. Its EOS failure and both live consequent failures are nonvacuous hits;
// a false implication antecedent inverts to an outer failure and schedules no
// cover-property pass action.
// CHECK-LABEL: obelisk_sim.func private @unit_3.$concurrent_eos_report.67.weak(
// CHECK: obelisk_sim.bytes.constant "u3-hit"
// CHECK-LABEL: obelisk_sim.func private @unit_3.$concurrent_eos.67.weak(
// CHECK-COUNT-1: obelisk_sim.ref.load
// CHECK-NOT: obelisk_sim.ref.load
// CHECK-COUNT-2: obelisk_sim.spawn @unit_3.$concurrent_eos_report.67.weak
// CHECK-NOT: obelisk_sim.spawn @unit_3.$concurrent_eos_report.67.weak
// CHECK-LABEL: obelisk_sim.func private @unit_3(
// CHECK-SAME: obelisk_sim.end_of_simulation_strength = "weak"
// CHECK-SAME: obelisk_sim.negated_operand_end_of_simulation_strength = "strong"
// CHECK-SAME: obelisk_sim.temporal_property_negation
// CHECK-COUNT-1: obelisk_sim.ref.alloc
// CHECK-NOT: obelisk_sim.ref.alloc
// CHECK: obelisk_sim.spawn @unit_3.$concurrent_eos.67.weak
// CHECK: obelisk_sim.spawn @unit_3.fork.67.0.0
// CHECK: obelisk_sim.spawn @unit_3.fork.67.0.0
// CHECK-NOT: obelisk_sim.spawn @unit_3.fork

// Branching consequent words are unioned before EOS reporting. This preserves
// one property result for each start age instead of one result per alternative.
// Live failure/success sites still select the inverted pass/fail callbacks.
// CHECK-LABEL: obelisk_sim.func private @unit_4.$concurrent_eos_report.84.strong(
// CHECK: obelisk_sim.bytes.constant "u4-fail"
// CHECK-LABEL: obelisk_sim.func private @unit_4.$concurrent_eos.84.strong(
// CHECK: obelisk_sim.ref.load
// CHECK: obelisk_sim.ref.load
// CHECK-NOT: obelisk_sim.ref.load
// CHECK: arith.ori
// CHECK-COUNT-2: obelisk_sim.spawn @unit_4.$concurrent_eos_report.84.strong
// CHECK-NOT: obelisk_sim.spawn @unit_4.$concurrent_eos_report.84.strong
// CHECK-LABEL: obelisk_sim.func private @unit_4(
// CHECK-SAME: obelisk_sim.branching_consequent_alternatives = 2 : i64
// CHECK-SAME: obelisk_sim.branching_consequent_monitor
// CHECK-SAME: obelisk_sim.end_of_simulation_strength = "strong"
// CHECK-SAME: obelisk_sim.negated_operand_end_of_simulation_strength = "weak"
// CHECK-SAME: obelisk_sim.temporal_property_negation
// CHECK-COUNT-2: obelisk_sim.ref.alloc
// CHECK-NOT: obelisk_sim.ref.alloc
// CHECK: obelisk_sim.spawn @unit_4.$concurrent_eos.84.strong
// CHECK: obelisk_sim.spawn @unit_4.fork.84.1.1
// CHECK: obelisk_sim.spawn @unit_4.fork.84.1.1
// CHECK: obelisk_sim.spawn @unit_4.fork.84.0.0
// CHECK: obelisk_sim.spawn @unit_4.fork.84.1.1
// CHECK: obelisk_sim.spawn @unit_4.fork.84.0.0
// CHECK: obelisk_sim.spawn @unit_4.fork.84.0.0
// CHECK-NOT: obelisk_sim.spawn @unit_4.fork

// Final unbounded-delay implication keeps prefix, warm-up, and eligible state.
// Counted strong operand failures invert into cover hits at EOS and prefix
// failure clocks, while terminal success and false antecedent stay silent.
// CHECK-LABEL: obelisk_sim.func private @unit_5.$concurrent_eos_count.110.delay_strong(
// CHECK-COUNT-3: obelisk_sim.ref.load
// CHECK-NOT: obelisk_sim.ref.load
// CHECK: obelisk_sim.spawn @unit_5.fork.110.0.0
// CHECK-LABEL: obelisk_sim.func private @unit_5(
// CHECK-SAME: obelisk_sim.end_of_simulation_strength = "weak"
// CHECK-SAME: obelisk_sim.negated_operand_end_of_simulation_strength = "strong"
// CHECK-SAME: obelisk_sim.persistent_delay_implication
// CHECK-SAME: obelisk_sim.persistent_delay_minimum = 1 : i64
// CHECK-SAME: obelisk_sim.persistent_delay_prefix_horizon = 2 : i64
// CHECK-SAME: obelisk_sim.temporal_property_negation
// CHECK-COUNT-3: obelisk_sim.ref.alloc
// CHECK-NOT: obelisk_sim.ref.alloc
// CHECK: obelisk_sim.spawn @unit_5.$concurrent_eos_count.110.delay_strong
// CHECK: obelisk_sim.spawn @unit_5.fork.110.0.0
// CHECK-NOT: obelisk_sim.spawn @unit_5.fork

// Disable clears the nonoverlap handoff and pending-consequent bits and bumps
// the epoch. Both live and EOS callbacks reload that epoch, so already queued
// inverted reports become stale instead of escaping cancellation.
// CHECK-LABEL: obelisk_sim.func private @unit_6.$concurrent_cancel.129(
// CHECK: obelisk_sim.ref.store {{.*}} to %arg4
// CHECK: obelisk_sim.ref.load %arg5
// CHECK: arith.addi
// CHECK: obelisk_sim.ref.store {{.*}} to %arg5
// CHECK-LABEL: obelisk_sim.func private @unit_6.$concurrent_eos_report.129.strong(
// CHECK: obelisk_sim.ref.load %arg1
// CHECK: arith.cmpi eq, {{.*}}, %arg2
// CHECK-LABEL: obelisk_sim.func private @unit_6.$concurrent_eos.129.strong(
// CHECK: obelisk_sim.ref.load %arg1
// CHECK: obelisk_sim.ref.load %arg2
// CHECK: obelisk_sim.spawn @unit_6.$concurrent_eos_report.129.strong
// CHECK: obelisk_sim.ref.load %arg2
// CHECK: obelisk_sim.spawn @unit_6.$concurrent_eos_report.129.strong
// CHECK-NOT: obelisk_sim.ref.load
// CHECK-NOT: obelisk_sim.spawn @unit_6.$concurrent_eos_report.129.strong
// CHECK-LABEL: obelisk_sim.func private @unit_6(
// CHECK-SAME: obelisk_sim.end_of_simulation_strength = "strong"
// CHECK-SAME: obelisk_sim.negated_operand_end_of_simulation_strength = "weak"
// CHECK-SAME: obelisk_sim.temporal_property_negation
// CHECK-COUNT-2: obelisk_sim.ref.alloc
// CHECK-NOT: obelisk_sim.ref.alloc
// CHECK: obelisk_sim.spawn @unit_6.$concurrent_cancel.129
// CHECK: obelisk_sim.spawn @unit_6.$concurrent_eos.129.strong
// CHECK: obelisk_sim.ref.load
// CHECK: obelisk_sim.spawn @unit_6.fork.129.0.0
// CHECK: obelisk_sim.ref.load
// CHECK: obelisk_sim.spawn @unit_6.fork.129.0.0
// CHECK: obelisk_sim.ref.load
// CHECK: obelisk_sim.spawn @unit_6.fork.129.1.1
// CHECK: obelisk_sim.ref.load
// CHECK: obelisk_sim.spawn @unit_6.fork.129.1.1

// A false followed-by antecedent makes not succeed vacuously. The cover pass
// action still executes even though future vacuity counters must classify it
// separately. Consequent failure and pending strong failure at EOS also pass.
// CHECK-LABEL: obelisk_sim.func private @unit_7.$concurrent_eos_report.151.weak(
// CHECK: obelisk_sim.bytes.constant "u7-hit"
// CHECK-LABEL: obelisk_sim.func private @unit_7.$concurrent_eos.151.weak(
// CHECK-COUNT-1: obelisk_sim.ref.load
// CHECK-NOT: obelisk_sim.ref.load
// CHECK-COUNT-1: obelisk_sim.spawn @unit_7.$concurrent_eos_report.151.weak
// CHECK-NOT: obelisk_sim.spawn @unit_7.$concurrent_eos_report.151.weak
// CHECK-LABEL: obelisk_sim.func private @unit_7(
// CHECK-SAME: obelisk_sim.end_of_simulation_strength = "weak"
// CHECK-SAME: obelisk_sim.followed_by_monitor
// CHECK-SAME: obelisk_sim.negated_operand_end_of_simulation_strength = "strong"
// CHECK-SAME: obelisk_sim.temporal_property_negation
// CHECK-COUNT-1: obelisk_sim.ref.alloc
// CHECK-NOT: obelisk_sim.ref.alloc
// CHECK: obelisk_sim.spawn @unit_7.$concurrent_eos.151.weak
// CHECK-COUNT-2: obelisk_sim.spawn @unit_7.fork.151.0.0
// CHECK-NOT: obelisk_sim.spawn @unit_7.fork

// Direct overlapped #-# composes the same vacuous pass with two branching
// consequent words. Their pending ages are unioned before the two nonvacuous
// strong-failure EOS reports, while four live failure sites share one action.
// CHECK-LABEL: obelisk_sim.func private @unit_8.$concurrent_eos_report.165.weak(
// CHECK: obelisk_sim.bytes.constant "u8-hit"
// CHECK-LABEL: obelisk_sim.func private @unit_8.$concurrent_eos.165.weak(
// CHECK-COUNT-2: obelisk_sim.ref.load
// CHECK-NOT: obelisk_sim.ref.load
// CHECK: arith.ori
// CHECK-COUNT-2: obelisk_sim.spawn @unit_8.$concurrent_eos_report.165.weak
// CHECK-NOT: obelisk_sim.spawn @unit_8.$concurrent_eos_report.165.weak
// CHECK-LABEL: obelisk_sim.func private @unit_8(
// CHECK-SAME: obelisk_sim.branching_consequent_alternatives = 2 : i64
// CHECK-SAME: obelisk_sim.branching_consequent_monitor
// CHECK-SAME: obelisk_sim.end_of_simulation_strength = "weak"
// CHECK-SAME: obelisk_sim.followed_by_monitor
// CHECK-SAME: obelisk_sim.negated_operand_end_of_simulation_strength = "strong"
// CHECK-SAME: obelisk_sim.temporal_property_negation
// CHECK-COUNT-2: obelisk_sim.ref.alloc
// CHECK-NOT: obelisk_sim.ref.alloc
// CHECK: obelisk_sim.spawn @unit_8.$concurrent_eos.165.weak
// CHECK-COUNT-4: obelisk_sim.spawn @unit_8.fork.165.0.0
// CHECK-NOT: obelisk_sim.spawn @unit_8.fork

// Nonoverlapped #=# retains its handoff in a fourth aggregate cell. The false
// antecedent and prefix failures enter the counted live pass dispatcher, while
// finalization loads exactly the prefix, handoff/warm-up, and eligible inventory.
// CHECK-NOT: @unit_9.$concurrent_eos_report.188
// CHECK-NOT: @unit_9.$concurrent_eos.188
// CHECK-LABEL: obelisk_sim.func private @unit_9.$concurrent_eos_count.188.delay_strong(
// CHECK-COUNT-4: obelisk_sim.ref.load
// CHECK-NOT: obelisk_sim.ref.load
// CHECK: obelisk_sim.spawn @unit_9.fork.188.0.0
// CHECK-LABEL: obelisk_sim.func private @unit_9(
// CHECK-SAME: obelisk_sim.end_of_simulation_strength = "weak"
// CHECK-SAME: obelisk_sim.followed_by_monitor
// CHECK-SAME: obelisk_sim.negated_operand_end_of_simulation_strength = "strong"
// CHECK-SAME: obelisk_sim.persistent_delay_implication
// CHECK-SAME: obelisk_sim.persistent_delay_minimum = 1 : i64
// CHECK-SAME: obelisk_sim.persistent_delay_nonoverlapped
// CHECK-SAME: obelisk_sim.persistent_delay_prefix_horizon = 2 : i64
// CHECK-SAME: obelisk_sim.temporal_property_negation
// CHECK-COUNT-4: obelisk_sim.ref.alloc
// CHECK-NOT: obelisk_sim.ref.alloc
// CHECK: obelisk_sim.spawn @unit_9.$concurrent_eos_count.188.delay_strong
// CHECK-COUNT-2: obelisk_sim.spawn @unit_9.fork.188.0.0
// CHECK-NOT: obelisk_sim.spawn @unit_9.fork
