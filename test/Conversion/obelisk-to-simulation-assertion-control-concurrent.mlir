// RUN: obelisk-opt %s '--lower-obelisk-to-sim=opt-level=0' | FileCheck %s
// RUN: obelisk-opt %s '--lower-obelisk-to-sim=opt-level=0' | FileCheck %s --check-prefix=COUNT
// RUN: obelisk-opt %s '--lower-obelisk-to-sim=opt-level=3' -o /dev/null
// RUN: obelisk-opt %s '--lower-obelisk-to-sim=opt-level=3' --emit-bytecode -o /dev/null

// IEEE 1800-2017 20.12: Off prevents new concurrent assertion attempts, but
// attempts already executing and their actions continue. Kill additionally
// cancels every live attempt and pending report. The fixture selects every
// assertion with Lock, Unlock, Off, and On; its first ten single-clock
// assertions also model Kill selection across deterministic, branching,
// aggregate-persistent, abort, and disable monitor families. The final
// multi-clock assertion remains an On/Off case because Kill is conservatively
// rejected for detached multi-clock attempts.

module {
  obelisk.sv.symbol.definition @s0.assertion_control_concurrent attributes {definition_kind = 0 : i32, hierarchical_name = "assertion_control_concurrent", name = "assertion_control_concurrent", node_id = 0 : i64} {
  }
  obelisk.sv.symbol.root @s1.$root attributes {hierarchical_name = "\\$root ", name = "$root", node_id = 1 : i64} {
    obelisk.sv.symbol.compilation_unit @s2 attributes {hierarchical_name = "$unit", node_id = 2 : i64} {
    }
    obelisk.sv.symbol.instance @s3.assertion_control_concurrent attributes {hierarchical_name = "assertion_control_concurrent", is_uninstantiated = false, name = "assertion_control_concurrent", node_id = 3 : i64, referenced_path = "assertion_control_concurrent", referenced_symbol = @s0.assertion_control_concurrent} {
      obelisk.sv.symbol.instance_body @s4.assertion_control_concurrent attributes {hierarchical_name = "assertion_control_concurrent", name = "assertion_control_concurrent", node_id = 4 : i64, time_precision_fs = 1000000 : i64, time_unit_fs = 1000000 : i64} {
        obelisk.sv.symbol.port @s5.clk attributes {direction = 0 : i32, hierarchical_name = "assertion_control_concurrent.clk", name = "clk", node_id = 5 : i64, semantic_type = !obelisk.integral<1, false, true, 0 : 0, logic>} {
        }
        obelisk.sv.symbol.variable @s6.clk attributes {hierarchical_name = "assertion_control_concurrent.clk", lifetime = 1 : i32, name = "clk", node_id = 6 : i64, semantic_type = !obelisk.integral<1, false, true, 0 : 0, logic>} {
        }
        obelisk.sv.symbol.port @s7.clk2 attributes {direction = 0 : i32, hierarchical_name = "assertion_control_concurrent.clk2", name = "clk2", node_id = 7 : i64, semantic_type = !obelisk.integral<1, false, true, 0 : 0, logic>} {
        }
        obelisk.sv.symbol.variable @s8.clk2 attributes {hierarchical_name = "assertion_control_concurrent.clk2", lifetime = 1 : i32, name = "clk2", node_id = 8 : i64, semantic_type = !obelisk.integral<1, false, true, 0 : 0, logic>} {
        }
        obelisk.sv.symbol.port @s9.a attributes {direction = 0 : i32, hierarchical_name = "assertion_control_concurrent.a", name = "a", node_id = 9 : i64, semantic_type = !obelisk.integral<1, false, true, 0 : 0, logic>} {
        }
        obelisk.sv.symbol.variable @s10.a attributes {hierarchical_name = "assertion_control_concurrent.a", lifetime = 1 : i32, name = "a", node_id = 10 : i64, semantic_type = !obelisk.integral<1, false, true, 0 : 0, logic>} {
        }
        obelisk.sv.symbol.port @s11.b attributes {direction = 0 : i32, hierarchical_name = "assertion_control_concurrent.b", name = "b", node_id = 11 : i64, semantic_type = !obelisk.integral<1, false, true, 0 : 0, logic>} {
        }
        obelisk.sv.symbol.variable @s12.b attributes {hierarchical_name = "assertion_control_concurrent.b", lifetime = 1 : i32, name = "b", node_id = 12 : i64, semantic_type = !obelisk.integral<1, false, true, 0 : 0, logic>} {
        }
        obelisk.sv.symbol.port @s13.c attributes {direction = 0 : i32, hierarchical_name = "assertion_control_concurrent.c", name = "c", node_id = 13 : i64, semantic_type = !obelisk.integral<1, false, true, 0 : 0, logic>} {
        }
        obelisk.sv.symbol.variable @s14.c attributes {hierarchical_name = "assertion_control_concurrent.c", lifetime = 1 : i32, name = "c", node_id = 14 : i64, semantic_type = !obelisk.integral<1, false, true, 0 : 0, logic>} {
        }
        obelisk.sv.symbol.port @s15.d attributes {direction = 0 : i32, hierarchical_name = "assertion_control_concurrent.d", name = "d", node_id = 15 : i64, semantic_type = !obelisk.integral<1, false, true, 0 : 0, logic>} {
        }
        obelisk.sv.symbol.variable @s16.d attributes {hierarchical_name = "assertion_control_concurrent.d", lifetime = 1 : i32, name = "d", node_id = 16 : i64, semantic_type = !obelisk.integral<1, false, true, 0 : 0, logic>} {
        }
        obelisk.sv.symbol.procedural_block @s17 attributes {hierarchical_name = "assertion_control_concurrent", node_id = 17 : i64, procedure_kind = 0 : i32, time_precision_fs = 1000000 : i64, time_unit_fs = 1000000 : i64} {
          obelisk.sv.statement.block attributes {node_id = 18 : i64} {
            obelisk.sv.statement.list attributes {node_id = 19 : i64} {
              obelisk.sv.statement.expression_statement attributes {node_id = 20 : i64} {
                obelisk.sv.expression.call attributes {argument_count = 4 : i64, callee_name = "$assertcontrol", constraint_restrictions = [], defaulted_arguments = array<i64: 0, 0, 0, 0>, has_inline_constraints = false, has_iterator_expression = false, has_output_arguments = false, has_this_class = false, is_signed = false, is_super_class = false, is_system_call = true, node_id = 21 : i64, semantic_type = !obelisk.void, subroutine_kind = 1 : i32, system_library_cell = "work.assertion_control_concurrent", system_scope_path = "assertion_control_concurrent", system_scope_symbol = @s1.$root::@s3.assertion_control_concurrent::@s4.assertion_control_concurrent} {
                  obelisk.sv.expression.integer_literal attributes {constant_value = "1", is_declared_unsized = true, is_signed = true, node_id = 22 : i64, semantic_type = !obelisk.integral<32, true, false, 31 : 0, int>} {
                  }
                  obelisk.sv.expression.integer_literal attributes {constant_value = "1", is_declared_unsized = true, is_signed = true, node_id = 23 : i64, semantic_type = !obelisk.integral<32, true, false, 31 : 0, int>} {
                  }
                  obelisk.sv.expression.integer_literal attributes {constant_value = "7", is_declared_unsized = true, is_signed = true, node_id = 24 : i64, semantic_type = !obelisk.integral<32, true, false, 31 : 0, int>} {
                  }
                  obelisk.sv.expression.integer_literal attributes {constant_value = "0", is_declared_unsized = true, is_signed = true, node_id = 25 : i64, semantic_type = !obelisk.integral<32, true, false, 31 : 0, int>} {
                  }
                }
              }
              obelisk.sv.statement.expression_statement attributes {node_id = 26 : i64} {
                obelisk.sv.expression.call attributes {argument_count = 4 : i64, callee_name = "$assertcontrol", constraint_restrictions = [], defaulted_arguments = array<i64: 0, 0, 0, 0>, has_inline_constraints = false, has_iterator_expression = false, has_output_arguments = false, has_this_class = false, is_signed = false, is_super_class = false, is_system_call = true, node_id = 27 : i64, semantic_type = !obelisk.void, subroutine_kind = 1 : i32, system_library_cell = "work.assertion_control_concurrent", system_scope_path = "assertion_control_concurrent", system_scope_symbol = @s1.$root::@s3.assertion_control_concurrent::@s4.assertion_control_concurrent} {
                  obelisk.sv.expression.integer_literal attributes {constant_value = "2", is_declared_unsized = true, is_signed = true, node_id = 28 : i64, semantic_type = !obelisk.integral<32, true, false, 31 : 0, int>} {
                  }
                  obelisk.sv.expression.integer_literal attributes {constant_value = "1", is_declared_unsized = true, is_signed = true, node_id = 29 : i64, semantic_type = !obelisk.integral<32, true, false, 31 : 0, int>} {
                  }
                  obelisk.sv.expression.integer_literal attributes {constant_value = "7", is_declared_unsized = true, is_signed = true, node_id = 30 : i64, semantic_type = !obelisk.integral<32, true, false, 31 : 0, int>} {
                  }
                  obelisk.sv.expression.integer_literal attributes {constant_value = "0", is_declared_unsized = true, is_signed = true, node_id = 31 : i64, semantic_type = !obelisk.integral<32, true, false, 31 : 0, int>} {
                  }
                }
              }
              obelisk.sv.statement.expression_statement attributes {node_id = 32 : i64} {
                obelisk.sv.expression.call attributes {argument_count = 1 : i64, callee_name = "$assertoff", constraint_restrictions = [], defaulted_arguments = array<i64: 0>, has_inline_constraints = false, has_iterator_expression = false, has_output_arguments = false, has_this_class = false, is_signed = false, is_super_class = false, is_system_call = true, node_id = 33 : i64, semantic_type = !obelisk.void, subroutine_kind = 1 : i32, system_library_cell = "work.assertion_control_concurrent", system_scope_path = "assertion_control_concurrent", system_scope_symbol = @s1.$root::@s3.assertion_control_concurrent::@s4.assertion_control_concurrent} {
                  obelisk.sv.expression.integer_literal attributes {constant_value = "0", is_declared_unsized = true, is_signed = true, node_id = 34 : i64, semantic_type = !obelisk.integral<32, true, false, 31 : 0, int>} {
                  }
                }
              }
              obelisk.sv.statement.expression_statement attributes {node_id = 35 : i64} {
                obelisk.sv.expression.call attributes {argument_count = 1 : i64, callee_name = "$asserton", constraint_restrictions = [], defaulted_arguments = array<i64: 0>, has_inline_constraints = false, has_iterator_expression = false, has_output_arguments = false, has_this_class = false, is_signed = false, is_super_class = false, is_system_call = true, node_id = 36 : i64, semantic_type = !obelisk.void, subroutine_kind = 1 : i32, system_library_cell = "work.assertion_control_concurrent", system_scope_path = "assertion_control_concurrent", system_scope_symbol = @s1.$root::@s3.assertion_control_concurrent::@s4.assertion_control_concurrent} {
                  obelisk.sv.expression.integer_literal attributes {constant_value = "0", is_declared_unsized = true, is_signed = true, node_id = 37 : i64, semantic_type = !obelisk.integral<32, true, false, 31 : 0, int>} {
                  }
                }
              }
            }
          }
        }
        obelisk.sv.symbol.statement_block @s18.det attributes {block_kind = 0 : i32, hierarchical_name = "assertion_control_concurrent.det", name = "det", node_id = 38 : i64} {
        }
        obelisk.sv.symbol.procedural_block @s19 attributes {hierarchical_name = "assertion_control_concurrent", node_id = 39 : i64, procedure_kind = 2 : i32, time_precision_fs = 1000000 : i64, time_unit_fs = 1000000 : i64} {
          obelisk.sv.statement.block attributes {block_path = "assertion_control_concurrent.det", block_symbol = @s1.$root::@s3.assertion_control_concurrent::@s4.assertion_control_concurrent::@s18.det, node_id = 40 : i64} {
            obelisk.sv.statement.concurrent_assertion attributes {simulation.assertion_kill_controlled, assertion_kind = 0 : i32, has_default_disable = false, has_fail_action = false, has_pass_action = true, node_id = 41 : i64} {
              obelisk.sv.assertion.clocking attributes {node_id = 42 : i64} {
                obelisk.sv.timing.signal_event attributes {edge_kind = 1 : i32, has_iff = false, node_id = 43 : i64} {
                  obelisk.sv.expression.named_value attributes {is_signed = false, node_id = 44 : i64, referenced_path = "assertion_control_concurrent.clk", referenced_symbol = @s1.$root::@s3.assertion_control_concurrent::@s4.assertion_control_concurrent::@s6.clk, semantic_type = !obelisk.integral<1, false, true, 0 : 0, logic>} {
                  }
                }
                obelisk.sv.assertion.sequence_concat attributes {delays = [{is_unbounded = false, max = 0 : i64, min = 0 : i64}, {is_unbounded = false, max = 2 : i64, min = 2 : i64}], node_id = 45 : i64} {
                  obelisk.sv.assertion.simple attributes {has_repetition = false, is_null = false, node_id = 46 : i64, repetition_is_unbounded = false} {
                    obelisk.sv.expression.named_value attributes {is_signed = false, node_id = 47 : i64, referenced_path = "assertion_control_concurrent.a", referenced_symbol = @s1.$root::@s3.assertion_control_concurrent::@s4.assertion_control_concurrent::@s10.a, semantic_type = !obelisk.integral<1, false, true, 0 : 0, logic>} {
                    }
                  }
                  obelisk.sv.assertion.simple attributes {has_repetition = false, is_null = false, node_id = 48 : i64, repetition_is_unbounded = false} {
                    obelisk.sv.expression.named_value attributes {is_signed = false, node_id = 49 : i64, referenced_path = "assertion_control_concurrent.b", referenced_symbol = @s1.$root::@s3.assertion_control_concurrent::@s4.assertion_control_concurrent::@s12.b, semantic_type = !obelisk.integral<1, false, true, 0 : 0, logic>} {
                    }
                  }
                }
              }
              obelisk.sv.statement.empty attributes {node_id = 50 : i64} {
              }
            }
          }
        }
        obelisk.sv.symbol.statement_block @s20.delay attributes {block_kind = 0 : i32, hierarchical_name = "assertion_control_concurrent.delay", name = "delay", node_id = 51 : i64} {
        }
        obelisk.sv.symbol.procedural_block @s21 attributes {hierarchical_name = "assertion_control_concurrent", node_id = 52 : i64, procedure_kind = 2 : i32, time_precision_fs = 1000000 : i64, time_unit_fs = 1000000 : i64} {
          obelisk.sv.statement.block attributes {block_path = "assertion_control_concurrent.delay", block_symbol = @s1.$root::@s3.assertion_control_concurrent::@s4.assertion_control_concurrent::@s20.delay, node_id = 53 : i64} {
            obelisk.sv.statement.concurrent_assertion attributes {simulation.assertion_kill_controlled, assertion_kind = 0 : i32, has_default_disable = false, has_fail_action = false, has_pass_action = true, node_id = 54 : i64} {
              obelisk.sv.assertion.clocking attributes {node_id = 55 : i64} {
                obelisk.sv.timing.signal_event attributes {edge_kind = 1 : i32, has_iff = false, node_id = 56 : i64} {
                  obelisk.sv.expression.named_value attributes {is_signed = false, node_id = 57 : i64, referenced_path = "assertion_control_concurrent.clk", referenced_symbol = @s1.$root::@s3.assertion_control_concurrent::@s4.assertion_control_concurrent::@s6.clk, semantic_type = !obelisk.integral<1, false, true, 0 : 0, logic>} {
                  }
                }
                obelisk.sv.assertion.sequence_concat attributes {delays = [{is_unbounded = false, max = 0 : i64, min = 0 : i64}, {is_unbounded = true, min = 2 : i64}], node_id = 58 : i64} {
                  obelisk.sv.assertion.simple attributes {has_repetition = false, is_null = false, node_id = 59 : i64, repetition_is_unbounded = false} {
                    obelisk.sv.expression.named_value attributes {is_signed = false, node_id = 60 : i64, referenced_path = "assertion_control_concurrent.a", referenced_symbol = @s1.$root::@s3.assertion_control_concurrent::@s4.assertion_control_concurrent::@s10.a, semantic_type = !obelisk.integral<1, false, true, 0 : 0, logic>} {
                    }
                  }
                  obelisk.sv.assertion.simple attributes {has_repetition = false, is_null = false, node_id = 61 : i64, repetition_is_unbounded = false} {
                    obelisk.sv.expression.named_value attributes {is_signed = false, node_id = 62 : i64, referenced_path = "assertion_control_concurrent.b", referenced_symbol = @s1.$root::@s3.assertion_control_concurrent::@s4.assertion_control_concurrent::@s12.b, semantic_type = !obelisk.integral<1, false, true, 0 : 0, logic>} {
                    }
                  }
                }
              }
              obelisk.sv.statement.empty attributes {node_id = 63 : i64} {
              }
            }
          }
        }
        obelisk.sv.symbol.statement_block @s22.unary attributes {block_kind = 0 : i32, hierarchical_name = "assertion_control_concurrent.unary", name = "unary", node_id = 64 : i64} {
        }
        obelisk.sv.symbol.procedural_block @s23 attributes {hierarchical_name = "assertion_control_concurrent", node_id = 65 : i64, procedure_kind = 2 : i32, time_precision_fs = 1000000 : i64, time_unit_fs = 1000000 : i64} {
          obelisk.sv.statement.block attributes {block_path = "assertion_control_concurrent.unary", block_symbol = @s1.$root::@s3.assertion_control_concurrent::@s4.assertion_control_concurrent::@s22.unary, node_id = 66 : i64} {
            obelisk.sv.statement.concurrent_assertion attributes {simulation.assertion_kill_controlled, assertion_kind = 0 : i32, has_default_disable = false, has_fail_action = false, has_pass_action = true, node_id = 67 : i64} {
              obelisk.sv.assertion.clocking attributes {node_id = 68 : i64} {
                obelisk.sv.timing.signal_event attributes {edge_kind = 1 : i32, has_iff = false, node_id = 69 : i64} {
                  obelisk.sv.expression.named_value attributes {is_signed = false, node_id = 70 : i64, referenced_path = "assertion_control_concurrent.clk", referenced_symbol = @s1.$root::@s3.assertion_control_concurrent::@s4.assertion_control_concurrent::@s6.clk, semantic_type = !obelisk.integral<1, false, true, 0 : 0, logic>} {
                  }
                }
                obelisk.sv.assertion.unary attributes {has_range = true, node_id = 71 : i64, operator_kind = 6 : i32, range_is_unbounded = true, range_min = 2 : i64} {
                  obelisk.sv.assertion.simple attributes {has_repetition = false, is_null = false, node_id = 72 : i64, repetition_is_unbounded = false} {
                    obelisk.sv.expression.named_value attributes {is_signed = false, node_id = 73 : i64, referenced_path = "assertion_control_concurrent.a", referenced_symbol = @s1.$root::@s3.assertion_control_concurrent::@s4.assertion_control_concurrent::@s10.a, semantic_type = !obelisk.integral<1, false, true, 0 : 0, logic>} {
                    }
                  }
                }
              }
              obelisk.sv.statement.empty attributes {node_id = 74 : i64} {
              }
            }
          }
        }
        obelisk.sv.symbol.statement_block @s24.until_p attributes {block_kind = 0 : i32, hierarchical_name = "assertion_control_concurrent.until_p", name = "until_p", node_id = 75 : i64} {
        }
        obelisk.sv.symbol.procedural_block @s25 attributes {hierarchical_name = "assertion_control_concurrent", node_id = 76 : i64, procedure_kind = 2 : i32, time_precision_fs = 1000000 : i64, time_unit_fs = 1000000 : i64} {
          obelisk.sv.statement.block attributes {block_path = "assertion_control_concurrent.until_p", block_symbol = @s1.$root::@s3.assertion_control_concurrent::@s4.assertion_control_concurrent::@s24.until_p, node_id = 77 : i64} {
            obelisk.sv.statement.concurrent_assertion attributes {simulation.assertion_kill_controlled, assertion_kind = 0 : i32, has_default_disable = false, has_fail_action = false, has_pass_action = true, node_id = 78 : i64} {
              obelisk.sv.assertion.clocking attributes {node_id = 79 : i64} {
                obelisk.sv.timing.signal_event attributes {edge_kind = 1 : i32, has_iff = false, node_id = 80 : i64} {
                  obelisk.sv.expression.named_value attributes {is_signed = false, node_id = 81 : i64, referenced_path = "assertion_control_concurrent.clk", referenced_symbol = @s1.$root::@s3.assertion_control_concurrent::@s4.assertion_control_concurrent::@s6.clk, semantic_type = !obelisk.integral<1, false, true, 0 : 0, logic>} {
                  }
                }
                obelisk.sv.assertion.binary attributes {node_id = 82 : i64, operator_kind = 6 : i32} {
                  obelisk.sv.assertion.simple attributes {has_repetition = false, is_null = false, node_id = 83 : i64, repetition_is_unbounded = false} {
                    obelisk.sv.expression.named_value attributes {is_signed = false, node_id = 84 : i64, referenced_path = "assertion_control_concurrent.a", referenced_symbol = @s1.$root::@s3.assertion_control_concurrent::@s4.assertion_control_concurrent::@s10.a, semantic_type = !obelisk.integral<1, false, true, 0 : 0, logic>} {
                    }
                  }
                  obelisk.sv.assertion.simple attributes {has_repetition = false, is_null = false, node_id = 85 : i64, repetition_is_unbounded = false} {
                    obelisk.sv.expression.named_value attributes {is_signed = false, node_id = 86 : i64, referenced_path = "assertion_control_concurrent.b", referenced_symbol = @s1.$root::@s3.assertion_control_concurrent::@s4.assertion_control_concurrent::@s12.b, semantic_type = !obelisk.integral<1, false, true, 0 : 0, logic>} {
                    }
                  }
                }
              }
              obelisk.sv.statement.empty attributes {node_id = 87 : i64} {
              }
            }
          }
        }
        obelisk.sv.symbol.statement_block @s26.repeat_p attributes {block_kind = 0 : i32, hierarchical_name = "assertion_control_concurrent.repeat_p", name = "repeat_p", node_id = 88 : i64} {
        }
        obelisk.sv.symbol.procedural_block @s27 attributes {hierarchical_name = "assertion_control_concurrent", node_id = 89 : i64, procedure_kind = 2 : i32, time_precision_fs = 1000000 : i64, time_unit_fs = 1000000 : i64} {
          obelisk.sv.statement.block attributes {block_path = "assertion_control_concurrent.repeat_p", block_symbol = @s1.$root::@s3.assertion_control_concurrent::@s4.assertion_control_concurrent::@s26.repeat_p, node_id = 90 : i64} {
            obelisk.sv.statement.concurrent_assertion attributes {simulation.assertion_kill_controlled, assertion_kind = 0 : i32, has_default_disable = false, has_fail_action = false, has_pass_action = true, node_id = 91 : i64} {
              obelisk.sv.assertion.clocking attributes {node_id = 92 : i64} {
                obelisk.sv.timing.signal_event attributes {edge_kind = 1 : i32, has_iff = false, node_id = 93 : i64} {
                  obelisk.sv.expression.named_value attributes {is_signed = false, node_id = 94 : i64, referenced_path = "assertion_control_concurrent.clk", referenced_symbol = @s1.$root::@s3.assertion_control_concurrent::@s4.assertion_control_concurrent::@s6.clk, semantic_type = !obelisk.integral<1, false, true, 0 : 0, logic>} {
                  }
                }
                obelisk.sv.assertion.simple attributes {has_repetition = true, is_null = false, node_id = 95 : i64, repetition_is_unbounded = true, repetition_kind = 0 : i32, repetition_min = 1 : i64} {
                  obelisk.sv.expression.named_value attributes {is_signed = false, node_id = 96 : i64, referenced_path = "assertion_control_concurrent.a", referenced_symbol = @s1.$root::@s3.assertion_control_concurrent::@s4.assertion_control_concurrent::@s10.a, semantic_type = !obelisk.integral<1, false, true, 0 : 0, logic>} {
                  }
                }
              }
              obelisk.sv.statement.empty attributes {node_id = 97 : i64} {
              }
            }
          }
        }
        obelisk.sv.symbol.statement_block @s28.branch attributes {block_kind = 0 : i32, hierarchical_name = "assertion_control_concurrent.branch", name = "branch", node_id = 98 : i64} {
        }
        obelisk.sv.symbol.procedural_block @s29 attributes {hierarchical_name = "assertion_control_concurrent", node_id = 99 : i64, procedure_kind = 2 : i32, time_precision_fs = 1000000 : i64, time_unit_fs = 1000000 : i64} {
          obelisk.sv.statement.block attributes {block_path = "assertion_control_concurrent.branch", block_symbol = @s1.$root::@s3.assertion_control_concurrent::@s4.assertion_control_concurrent::@s28.branch, node_id = 100 : i64} {
            obelisk.sv.statement.concurrent_assertion attributes {simulation.assertion_kill_controlled, assertion_kind = 0 : i32, has_default_disable = false, has_fail_action = false, has_pass_action = true, node_id = 101 : i64} {
              obelisk.sv.assertion.clocking attributes {node_id = 102 : i64} {
                obelisk.sv.timing.signal_event attributes {edge_kind = 1 : i32, has_iff = false, node_id = 103 : i64} {
                  obelisk.sv.expression.named_value attributes {is_signed = false, node_id = 104 : i64, referenced_path = "assertion_control_concurrent.clk", referenced_symbol = @s1.$root::@s3.assertion_control_concurrent::@s4.assertion_control_concurrent::@s6.clk, semantic_type = !obelisk.integral<1, false, true, 0 : 0, logic>} {
                  }
                }
                obelisk.sv.assertion.binary attributes {node_id = 105 : i64, operator_kind = 1 : i32} {
                  obelisk.sv.assertion.sequence_concat attributes {delays = [{is_unbounded = false, max = 0 : i64, min = 0 : i64}, {is_unbounded = false, max = 1 : i64, min = 1 : i64}], node_id = 106 : i64} {
                    obelisk.sv.assertion.simple attributes {has_repetition = false, is_null = false, node_id = 107 : i64, repetition_is_unbounded = false} {
                      obelisk.sv.expression.named_value attributes {is_signed = false, node_id = 108 : i64, referenced_path = "assertion_control_concurrent.a", referenced_symbol = @s1.$root::@s3.assertion_control_concurrent::@s4.assertion_control_concurrent::@s10.a, semantic_type = !obelisk.integral<1, false, true, 0 : 0, logic>} {
                      }
                    }
                    obelisk.sv.assertion.simple attributes {has_repetition = false, is_null = false, node_id = 109 : i64, repetition_is_unbounded = false} {
                      obelisk.sv.expression.named_value attributes {is_signed = false, node_id = 110 : i64, referenced_path = "assertion_control_concurrent.b", referenced_symbol = @s1.$root::@s3.assertion_control_concurrent::@s4.assertion_control_concurrent::@s12.b, semantic_type = !obelisk.integral<1, false, true, 0 : 0, logic>} {
                      }
                    }
                  }
                  obelisk.sv.assertion.sequence_concat attributes {delays = [{is_unbounded = false, max = 0 : i64, min = 0 : i64}, {is_unbounded = false, max = 2 : i64, min = 2 : i64}], node_id = 111 : i64} {
                    obelisk.sv.assertion.simple attributes {has_repetition = false, is_null = false, node_id = 112 : i64, repetition_is_unbounded = false} {
                      obelisk.sv.expression.named_value attributes {is_signed = false, node_id = 113 : i64, referenced_path = "assertion_control_concurrent.a", referenced_symbol = @s1.$root::@s3.assertion_control_concurrent::@s4.assertion_control_concurrent::@s10.a, semantic_type = !obelisk.integral<1, false, true, 0 : 0, logic>} {
                      }
                    }
                    obelisk.sv.assertion.simple attributes {has_repetition = false, is_null = false, node_id = 114 : i64, repetition_is_unbounded = false} {
                      obelisk.sv.expression.named_value attributes {is_signed = false, node_id = 115 : i64, referenced_path = "assertion_control_concurrent.c", referenced_symbol = @s1.$root::@s3.assertion_control_concurrent::@s4.assertion_control_concurrent::@s14.c, semantic_type = !obelisk.integral<1, false, true, 0 : 0, logic>} {
                      }
                    }
                  }
                }
              }
              obelisk.sv.statement.empty attributes {node_id = 116 : i64} {
              }
            }
          }
        }
        obelisk.sv.symbol.statement_block @s30.impl attributes {block_kind = 0 : i32, hierarchical_name = "assertion_control_concurrent.impl", name = "impl", node_id = 117 : i64} {
        }
        obelisk.sv.symbol.procedural_block @s31 attributes {hierarchical_name = "assertion_control_concurrent", node_id = 118 : i64, procedure_kind = 2 : i32, time_precision_fs = 1000000 : i64, time_unit_fs = 1000000 : i64} {
          obelisk.sv.statement.block attributes {block_path = "assertion_control_concurrent.impl", block_symbol = @s1.$root::@s3.assertion_control_concurrent::@s4.assertion_control_concurrent::@s30.impl, node_id = 119 : i64} {
            obelisk.sv.statement.concurrent_assertion attributes {simulation.assertion_kill_controlled, assertion_kind = 0 : i32, has_default_disable = false, has_fail_action = false, has_pass_action = true, node_id = 120 : i64} {
              obelisk.sv.assertion.clocking attributes {node_id = 121 : i64} {
                obelisk.sv.timing.signal_event attributes {edge_kind = 1 : i32, has_iff = false, node_id = 122 : i64} {
                  obelisk.sv.expression.named_value attributes {is_signed = false, node_id = 123 : i64, referenced_path = "assertion_control_concurrent.clk", referenced_symbol = @s1.$root::@s3.assertion_control_concurrent::@s4.assertion_control_concurrent::@s6.clk, semantic_type = !obelisk.integral<1, false, true, 0 : 0, logic>} {
                  }
                }
                obelisk.sv.assertion.binary attributes {node_id = 124 : i64, operator_kind = 12 : i32} {
                  obelisk.sv.assertion.simple attributes {has_repetition = false, is_null = false, node_id = 125 : i64, repetition_is_unbounded = false} {
                    obelisk.sv.expression.named_value attributes {is_signed = false, node_id = 126 : i64, referenced_path = "assertion_control_concurrent.a", referenced_symbol = @s1.$root::@s3.assertion_control_concurrent::@s4.assertion_control_concurrent::@s10.a, semantic_type = !obelisk.integral<1, false, true, 0 : 0, logic>} {
                    }
                  }
                  obelisk.sv.assertion.binary attributes {node_id = 127 : i64, operator_kind = 1 : i32} {
                    obelisk.sv.assertion.sequence_concat attributes {delays = [{is_unbounded = false, max = 0 : i64, min = 0 : i64}, {is_unbounded = false, max = 1 : i64, min = 1 : i64}], node_id = 128 : i64} {
                      obelisk.sv.assertion.simple attributes {has_repetition = false, is_null = false, node_id = 129 : i64, repetition_is_unbounded = false} {
                        obelisk.sv.expression.named_value attributes {is_signed = false, node_id = 130 : i64, referenced_path = "assertion_control_concurrent.b", referenced_symbol = @s1.$root::@s3.assertion_control_concurrent::@s4.assertion_control_concurrent::@s12.b, semantic_type = !obelisk.integral<1, false, true, 0 : 0, logic>} {
                        }
                      }
                      obelisk.sv.assertion.simple attributes {has_repetition = false, is_null = false, node_id = 131 : i64, repetition_is_unbounded = false} {
                        obelisk.sv.expression.named_value attributes {is_signed = false, node_id = 132 : i64, referenced_path = "assertion_control_concurrent.c", referenced_symbol = @s1.$root::@s3.assertion_control_concurrent::@s4.assertion_control_concurrent::@s14.c, semantic_type = !obelisk.integral<1, false, true, 0 : 0, logic>} {
                        }
                      }
                    }
                    obelisk.sv.assertion.sequence_concat attributes {delays = [{is_unbounded = false, max = 0 : i64, min = 0 : i64}, {is_unbounded = false, max = 2 : i64, min = 2 : i64}], node_id = 133 : i64} {
                      obelisk.sv.assertion.simple attributes {has_repetition = false, is_null = false, node_id = 134 : i64, repetition_is_unbounded = false} {
                        obelisk.sv.expression.named_value attributes {is_signed = false, node_id = 135 : i64, referenced_path = "assertion_control_concurrent.b", referenced_symbol = @s1.$root::@s3.assertion_control_concurrent::@s4.assertion_control_concurrent::@s12.b, semantic_type = !obelisk.integral<1, false, true, 0 : 0, logic>} {
                        }
                      }
                      obelisk.sv.assertion.simple attributes {has_repetition = false, is_null = false, node_id = 136 : i64, repetition_is_unbounded = false} {
                        obelisk.sv.expression.named_value attributes {is_signed = false, node_id = 137 : i64, referenced_path = "assertion_control_concurrent.d", referenced_symbol = @s1.$root::@s3.assertion_control_concurrent::@s4.assertion_control_concurrent::@s16.d, semantic_type = !obelisk.integral<1, false, true, 0 : 0, logic>} {
                        }
                      }
                    }
                  }
                }
              }
              obelisk.sv.statement.empty attributes {node_id = 138 : i64} {
              }
            }
          }
        }
        obelisk.sv.symbol.statement_block @s32.bante attributes {block_kind = 0 : i32, hierarchical_name = "assertion_control_concurrent.bante", name = "bante", node_id = 139 : i64} {
        }
        obelisk.sv.symbol.procedural_block @s33 attributes {hierarchical_name = "assertion_control_concurrent", node_id = 140 : i64, procedure_kind = 2 : i32, time_precision_fs = 1000000 : i64, time_unit_fs = 1000000 : i64} {
          obelisk.sv.statement.block attributes {block_path = "assertion_control_concurrent.bante", block_symbol = @s1.$root::@s3.assertion_control_concurrent::@s4.assertion_control_concurrent::@s32.bante, node_id = 141 : i64} {
            obelisk.sv.statement.concurrent_assertion attributes {simulation.assertion_kill_controlled, assertion_kind = 0 : i32, has_default_disable = false, has_fail_action = false, has_pass_action = true, node_id = 142 : i64} {
              obelisk.sv.assertion.clocking attributes {node_id = 143 : i64} {
                obelisk.sv.timing.signal_event attributes {edge_kind = 1 : i32, has_iff = false, node_id = 144 : i64} {
                  obelisk.sv.expression.named_value attributes {is_signed = false, node_id = 145 : i64, referenced_path = "assertion_control_concurrent.clk", referenced_symbol = @s1.$root::@s3.assertion_control_concurrent::@s4.assertion_control_concurrent::@s6.clk, semantic_type = !obelisk.integral<1, false, true, 0 : 0, logic>} {
                  }
                }
                obelisk.sv.assertion.binary attributes {node_id = 146 : i64, operator_kind = 11 : i32} {
                  obelisk.sv.assertion.binary attributes {node_id = 147 : i64, operator_kind = 1 : i32} {
                    obelisk.sv.assertion.simple attributes {has_repetition = false, is_null = false, node_id = 148 : i64, repetition_is_unbounded = false} {
                      obelisk.sv.expression.named_value attributes {is_signed = false, node_id = 149 : i64, referenced_path = "assertion_control_concurrent.a", referenced_symbol = @s1.$root::@s3.assertion_control_concurrent::@s4.assertion_control_concurrent::@s10.a, semantic_type = !obelisk.integral<1, false, true, 0 : 0, logic>} {
                      }
                    }
                    obelisk.sv.assertion.sequence_concat attributes {delays = [{is_unbounded = false, max = 0 : i64, min = 0 : i64}, {is_unbounded = false, max = 2 : i64, min = 2 : i64}], node_id = 153 : i64} {
                      obelisk.sv.assertion.simple attributes {has_repetition = false, is_null = false, node_id = 154 : i64, repetition_is_unbounded = false} {
                        obelisk.sv.expression.named_value attributes {is_signed = false, node_id = 155 : i64, referenced_path = "assertion_control_concurrent.a", referenced_symbol = @s1.$root::@s3.assertion_control_concurrent::@s4.assertion_control_concurrent::@s10.a, semantic_type = !obelisk.integral<1, false, true, 0 : 0, logic>} {
                        }
                      }
                      obelisk.sv.assertion.simple attributes {has_repetition = false, is_null = false, node_id = 156 : i64, repetition_is_unbounded = false} {
                        obelisk.sv.expression.named_value attributes {is_signed = false, node_id = 157 : i64, referenced_path = "assertion_control_concurrent.c", referenced_symbol = @s1.$root::@s3.assertion_control_concurrent::@s4.assertion_control_concurrent::@s14.c, semantic_type = !obelisk.integral<1, false, true, 0 : 0, logic>} {
                        }
                      }
                    }
                  }
                  obelisk.sv.assertion.simple attributes {has_repetition = false, is_null = false, node_id = 158 : i64, repetition_is_unbounded = false} {
                    obelisk.sv.expression.named_value attributes {is_signed = false, node_id = 159 : i64, referenced_path = "assertion_control_concurrent.d", referenced_symbol = @s1.$root::@s3.assertion_control_concurrent::@s4.assertion_control_concurrent::@s16.d, semantic_type = !obelisk.integral<1, false, true, 0 : 0, logic>} {
                    }
                  }
                }
              }
              obelisk.sv.statement.empty attributes {node_id = 160 : i64} {
              }
            }
          }
        }
        obelisk.sv.symbol.statement_block @s34.abort_p attributes {block_kind = 0 : i32, hierarchical_name = "assertion_control_concurrent.abort_p", name = "abort_p", node_id = 161 : i64} {
        }
        obelisk.sv.symbol.procedural_block @s35 attributes {hierarchical_name = "assertion_control_concurrent", node_id = 162 : i64, procedure_kind = 2 : i32, time_precision_fs = 1000000 : i64, time_unit_fs = 1000000 : i64} {
          obelisk.sv.statement.block attributes {block_path = "assertion_control_concurrent.abort_p", block_symbol = @s1.$root::@s3.assertion_control_concurrent::@s4.assertion_control_concurrent::@s34.abort_p, node_id = 163 : i64} {
            obelisk.sv.statement.concurrent_assertion attributes {simulation.assertion_kill_controlled, assertion_kind = 0 : i32, has_default_disable = false, has_fail_action = true, has_pass_action = true, node_id = 164 : i64} {
              obelisk.sv.assertion.clocking attributes {node_id = 165 : i64} {
                obelisk.sv.timing.signal_event attributes {edge_kind = 1 : i32, has_iff = false, node_id = 166 : i64} {
                  obelisk.sv.expression.named_value attributes {is_signed = false, node_id = 167 : i64, referenced_path = "assertion_control_concurrent.clk", referenced_symbol = @s1.$root::@s3.assertion_control_concurrent::@s4.assertion_control_concurrent::@s6.clk, semantic_type = !obelisk.integral<1, false, true, 0 : 0, logic>} {
                  }
                }
                obelisk.sv.assertion.abort attributes {action = 0 : i32, is_synchronous = false, node_id = 168 : i64} {
                  obelisk.sv.expression.named_value attributes {is_signed = false, node_id = 169 : i64, referenced_path = "assertion_control_concurrent.c", referenced_symbol = @s1.$root::@s3.assertion_control_concurrent::@s4.assertion_control_concurrent::@s14.c, semantic_type = !obelisk.integral<1, false, true, 0 : 0, logic>} {
                  }
                  obelisk.sv.assertion.sequence_concat attributes {delays = [{is_unbounded = false, max = 0 : i64, min = 0 : i64}, {is_unbounded = false, max = 2 : i64, min = 2 : i64}], node_id = 170 : i64} {
                    obelisk.sv.assertion.simple attributes {has_repetition = false, is_null = false, node_id = 171 : i64, repetition_is_unbounded = false} {
                      obelisk.sv.expression.named_value attributes {is_signed = false, node_id = 172 : i64, referenced_path = "assertion_control_concurrent.a", referenced_symbol = @s1.$root::@s3.assertion_control_concurrent::@s4.assertion_control_concurrent::@s10.a, semantic_type = !obelisk.integral<1, false, true, 0 : 0, logic>} {
                      }
                    }
                    obelisk.sv.assertion.simple attributes {has_repetition = false, is_null = false, node_id = 173 : i64, repetition_is_unbounded = false} {
                      obelisk.sv.expression.named_value attributes {is_signed = false, node_id = 174 : i64, referenced_path = "assertion_control_concurrent.b", referenced_symbol = @s1.$root::@s3.assertion_control_concurrent::@s4.assertion_control_concurrent::@s12.b, semantic_type = !obelisk.integral<1, false, true, 0 : 0, logic>} {
                      }
                    }
                  }
                }
              }
              obelisk.sv.statement.expression_statement attributes {node_id = 175 : i64} {
                obelisk.sv.expression.call attributes {argument_count = 1 : i64, callee_name = "$display", constraint_restrictions = [], defaulted_arguments = array<i64: 0>, has_inline_constraints = false, has_iterator_expression = false, has_output_arguments = false, has_this_class = false, is_signed = false, is_super_class = false, is_system_call = true, node_id = 176 : i64, semantic_type = !obelisk.void, subroutine_kind = 1 : i32, system_library_cell = "work.assertion_control_concurrent", system_scope_path = "assertion_control_concurrent.abort_p", system_scope_symbol = @s1.$root::@s3.assertion_control_concurrent::@s4.assertion_control_concurrent::@s34.abort_p} {
                  obelisk.sv.expression.string_literal attributes {constant_value = "abort pass", is_signed = false, node_id = 177 : i64, semantic_type = !obelisk.ranged_packed_array<79 : 0 x !obelisk.integral<1, false, false, 0 : 0, bit>>} {
                  }
                }
              }
              obelisk.sv.statement.expression_statement attributes {node_id = 178 : i64} {
                obelisk.sv.expression.call attributes {argument_count = 1 : i64, callee_name = "$display", constraint_restrictions = [], defaulted_arguments = array<i64: 0>, has_inline_constraints = false, has_iterator_expression = false, has_output_arguments = false, has_this_class = false, is_signed = false, is_super_class = false, is_system_call = true, node_id = 179 : i64, semantic_type = !obelisk.void, subroutine_kind = 1 : i32, system_library_cell = "work.assertion_control_concurrent", system_scope_path = "assertion_control_concurrent.abort_p", system_scope_symbol = @s1.$root::@s3.assertion_control_concurrent::@s4.assertion_control_concurrent::@s34.abort_p} {
                  obelisk.sv.expression.string_literal attributes {constant_value = "abort fail", is_signed = false, node_id = 180 : i64, semantic_type = !obelisk.ranged_packed_array<79 : 0 x !obelisk.integral<1, false, false, 0 : 0, bit>>} {
                  }
                }
              }
            }
          }
        }
        obelisk.sv.symbol.statement_block @s36.disable_p attributes {block_kind = 0 : i32, hierarchical_name = "assertion_control_concurrent.disable_p", name = "disable_p", node_id = 181 : i64} {
        }
        obelisk.sv.symbol.procedural_block @s37 attributes {hierarchical_name = "assertion_control_concurrent", node_id = 182 : i64, procedure_kind = 2 : i32, time_precision_fs = 1000000 : i64, time_unit_fs = 1000000 : i64} {
          obelisk.sv.statement.block attributes {block_path = "assertion_control_concurrent.disable_p", block_symbol = @s1.$root::@s3.assertion_control_concurrent::@s4.assertion_control_concurrent::@s36.disable_p, node_id = 183 : i64} {
            obelisk.sv.statement.concurrent_assertion attributes {simulation.assertion_kill_controlled, assertion_kind = 0 : i32, has_default_disable = false, has_fail_action = false, has_pass_action = true, node_id = 184 : i64} {
              obelisk.sv.assertion.clocking attributes {node_id = 185 : i64} {
                obelisk.sv.timing.signal_event attributes {edge_kind = 1 : i32, has_iff = false, node_id = 186 : i64} {
                  obelisk.sv.expression.named_value attributes {is_signed = false, node_id = 187 : i64, referenced_path = "assertion_control_concurrent.clk", referenced_symbol = @s1.$root::@s3.assertion_control_concurrent::@s4.assertion_control_concurrent::@s6.clk, semantic_type = !obelisk.integral<1, false, true, 0 : 0, logic>} {
                  }
                }
                obelisk.sv.assertion.disable_iff attributes {node_id = 188 : i64} {
                  obelisk.sv.expression.named_value attributes {is_signed = false, node_id = 189 : i64, referenced_path = "assertion_control_concurrent.c", referenced_symbol = @s1.$root::@s3.assertion_control_concurrent::@s4.assertion_control_concurrent::@s14.c, semantic_type = !obelisk.integral<1, false, true, 0 : 0, logic>} {
                  }
                  obelisk.sv.assertion.sequence_concat attributes {delays = [{is_unbounded = false, max = 0 : i64, min = 0 : i64}, {is_unbounded = false, max = 2 : i64, min = 2 : i64}], node_id = 190 : i64} {
                    obelisk.sv.assertion.simple attributes {has_repetition = false, is_null = false, node_id = 191 : i64, repetition_is_unbounded = false} {
                      obelisk.sv.expression.named_value attributes {is_signed = false, node_id = 192 : i64, referenced_path = "assertion_control_concurrent.a", referenced_symbol = @s1.$root::@s3.assertion_control_concurrent::@s4.assertion_control_concurrent::@s10.a, semantic_type = !obelisk.integral<1, false, true, 0 : 0, logic>} {
                      }
                    }
                    obelisk.sv.assertion.simple attributes {has_repetition = false, is_null = false, node_id = 193 : i64, repetition_is_unbounded = false} {
                      obelisk.sv.expression.named_value attributes {is_signed = false, node_id = 194 : i64, referenced_path = "assertion_control_concurrent.b", referenced_symbol = @s1.$root::@s3.assertion_control_concurrent::@s4.assertion_control_concurrent::@s12.b, semantic_type = !obelisk.integral<1, false, true, 0 : 0, logic>} {
                      }
                    }
                  }
                }
              }
              obelisk.sv.statement.empty attributes {node_id = 195 : i64} {
              }
            }
          }
        }
        obelisk.sv.symbol.statement_block @s38.multi attributes {block_kind = 0 : i32, hierarchical_name = "assertion_control_concurrent.multi", name = "multi", node_id = 196 : i64} {
        }
        obelisk.sv.symbol.procedural_block @s39 attributes {hierarchical_name = "assertion_control_concurrent", node_id = 197 : i64, procedure_kind = 2 : i32, time_precision_fs = 1000000 : i64, time_unit_fs = 1000000 : i64} {
          obelisk.sv.statement.block attributes {block_path = "assertion_control_concurrent.multi", block_symbol = @s1.$root::@s3.assertion_control_concurrent::@s4.assertion_control_concurrent::@s38.multi, node_id = 198 : i64} {
            obelisk.sv.statement.concurrent_assertion attributes {assertion_kind = 0 : i32, has_default_disable = false, has_fail_action = false, has_pass_action = true, node_id = 199 : i64} {
              obelisk.sv.assertion.clocking attributes {node_id = 200 : i64} {
                obelisk.sv.timing.signal_event attributes {edge_kind = 1 : i32, has_iff = false, node_id = 201 : i64} {
                  obelisk.sv.expression.named_value attributes {is_signed = false, node_id = 202 : i64, referenced_path = "assertion_control_concurrent.clk", referenced_symbol = @s1.$root::@s3.assertion_control_concurrent::@s4.assertion_control_concurrent::@s6.clk, semantic_type = !obelisk.integral<1, false, true, 0 : 0, logic>} {
                  }
                }
                obelisk.sv.assertion.sequence_concat attributes {delays = [{is_unbounded = false, max = 1 : i64, min = 1 : i64}, {is_unbounded = false, max = 1 : i64, min = 1 : i64}], node_id = 203 : i64} {
                  obelisk.sv.assertion.simple attributes {has_repetition = false, is_null = false, node_id = 204 : i64, repetition_is_unbounded = false} {
                    obelisk.sv.expression.named_value attributes {is_signed = false, node_id = 205 : i64, referenced_path = "assertion_control_concurrent.a", referenced_symbol = @s1.$root::@s3.assertion_control_concurrent::@s4.assertion_control_concurrent::@s10.a, semantic_type = !obelisk.integral<1, false, true, 0 : 0, logic>} {
                    }
                  }
                  obelisk.sv.assertion.clocking attributes {node_id = 206 : i64} {
                    obelisk.sv.timing.signal_event attributes {edge_kind = 1 : i32, has_iff = false, node_id = 207 : i64} {
                      obelisk.sv.expression.named_value attributes {is_signed = false, node_id = 208 : i64, referenced_path = "assertion_control_concurrent.clk2", referenced_symbol = @s1.$root::@s3.assertion_control_concurrent::@s4.assertion_control_concurrent::@s8.clk2, semantic_type = !obelisk.integral<1, false, true, 0 : 0, logic>} {
                      }
                    }
                    obelisk.sv.assertion.simple attributes {has_repetition = false, is_null = false, node_id = 209 : i64, repetition_is_unbounded = false} {
                      obelisk.sv.expression.named_value attributes {is_signed = false, node_id = 210 : i64, referenced_path = "assertion_control_concurrent.b", referenced_symbol = @s1.$root::@s3.assertion_control_concurrent::@s4.assertion_control_concurrent::@s12.b, semantic_type = !obelisk.integral<1, false, true, 0 : 0, logic>} {
                      }
                    }
                  }
                }
              }
              obelisk.sv.statement.empty attributes {node_id = 211 : i64} {
              }
            }
          }
        }
      }
    }
  }
}

// All eleven concurrent directives receive stable IDs. Lock/Unlock mutate
// only runtime control state; Off/On additionally cause exactly one enabled
// query in each monitor and no action-state query.
// COUNT-LABEL: simulation.func private @unit_0(
// COUNT-COUNT-11: simulation.assert.control {{.*}} action <lock> assertion
// COUNT-COUNT-11: simulation.assert.control {{.*}} action <unlock> assertion
// COUNT-COUNT-11: simulation.assert.control {{.*}} action <off> assertion
// COUNT-COUNT-11: simulation.assert.control {{.*}} action <on> assertion
// COUNT-NOT: simulation.assert.control
// COUNT-COUNT-11: simulation.assert.enabled
// COUNT-NOT: simulation.assert.enabled
// COUNT-NOT: simulation.assert.action_state

// A deterministic monitor advances and reports older age bits independently
// of the enable value. Only the current age-zero truth and failure are gated.
// CHECK-LABEL: simulation.func private @unit_1(
// CHECK-SAME: simulation.assertion_path = "assertion_control_concurrent.det"
// CHECK-SAME: simulation.assertion_target_id = [[DET_ID:[0-9]+]] : i64
// CHECK: simulation.assert.kill_epoch %arg0 assertion [[DET_ID]] {simulation.concurrent_kill_epoch_check}
// CHECK: simulation.ref.store
// CHECK: [[DET_ENABLE:%.*]] = simulation.assert.enabled %arg0 assertion [[DET_ID]] {simulation.concurrent_attempt_enable}
// CHECK: arith.cmpi ne
// CHECK: [[OLD_MASK:%.*]] = arith.andi {{.*}} : i64
// CHECK: [[OLD_ACTIVE:%.*]] = arith.cmpi ne, [[OLD_MASK]], {{.*}} : i64
// CHECK: [[OLD_TRUTH:%.*]] = simulation.logic.is_true
// CHECK: [[OLD_FALSE:%.*]] = arith.xori [[OLD_TRUTH]], {{.*}} : i1
// CHECK: arith.andi [[OLD_ACTIVE]], [[OLD_FALSE]] : i1
// CHECK: [[START_TRUTH:%.*]] = simulation.logic.is_true
// CHECK: arith.andi [[START_TRUTH]], [[DET_ENABLE]] {simulation.concurrent_attempt_start} : i1

// Persistent delay and unary monitors use the same prepared identity. For
// ranged eventuality, Off can create holes in the M-cycle warm-up pipeline;
// its second cell is therefore an age bitset shifted every clock, with only
// the enabled start inserted into bit zero.
// CHECK-LABEL: simulation.func private @unit_2(
// CHECK-SAME: simulation.persistent_delay_monitor
// CHECK: simulation.assert.kill_epoch {{.*}} {simulation.concurrent_kill_epoch_check}
// CHECK: simulation.ref.store
// CHECK: [[DELAY_ENABLE:%.*]] = simulation.assert.enabled {{.*}} {simulation.concurrent_attempt_enable}
// CHECK: arith.andi {{.*}}, [[DELAY_ENABLE]]
// CHECK-LABEL: simulation.func private @unit_3(
// CHECK-SAME: simulation.persistent_unary_minimum = 2 : i64
// CHECK: simulation.assert.kill_epoch {{.*}} {simulation.concurrent_kill_epoch_check}
// CHECK-COUNT-3: simulation.ref.store
// CHECK: [[UNARY_ENABLE:%.*]] = simulation.assert.enabled {{.*}} {simulation.concurrent_attempt_enable}
// CHECK: [[UNARY_START:%.*]] = arith.extui [[UNARY_ENABLE]] : i1 to i64
// CHECK: [[ELIGIBLE:%.*]] = simulation.ref.load
// CHECK: [[IMMATURE:%.*]] = simulation.ref.load
// CHECK: [[SHIFTED:%.*]] = arith.shli [[IMMATURE]],
// CHECK: [[RETAINED:%.*]] = arith.andi [[SHIFTED]],
// CHECK: arith.ori [[RETAINED]], [[UNARY_START]] : i64

// Age-insensitive aggregate DFAs add at most the enabled current attempt; the
// already-live count/token state still takes its ordinary transition.
// CHECK-LABEL: simulation.func private @unit_4(
// CHECK-SAME: simulation.persistent_until_monitor
// CHECK: simulation.suspend.edge
// CHECK: simulation.assert.kill_epoch {{.*}} {simulation.concurrent_kill_epoch_check}
// CHECK: [[UNTIL_BODY:\^bb[0-9]+]]([[UNTIL_LIVE:%[0-9]+]]: i64):
// CHECK: [[UNTIL_ENABLE:%.*]] = simulation.assert.enabled
// CHECK: [[UNTIL_START:%.*]] = arith.extui [[UNTIL_ENABLE]] : i1 to i64
// CHECK: arith.addi [[UNTIL_LIVE]], [[UNTIL_START]] : i64
// CHECK-LABEL: simulation.func private @unit_5(
// CHECK-SAME: simulation.persistent_repetition_monitor
// CHECK: simulation.assert.kill_epoch {{.*}} {simulation.concurrent_kill_epoch_check}
// CHECK: simulation.ref.store
// CHECK: [[REPEAT_ENABLE:%.*]] = simulation.assert.enabled
// CHECK: [[REPEAT_START:%.*]] = arith.extui [[REPEAT_ENABLE]] : i1 to i64
// CHECK: [[REPEAT_TRUTH:%.*]] = simulation.logic.is_true
// CHECK: arith.select [[REPEAT_TRUTH]], {{.*}}, [[REPEAT_START]] : i64

// Both bounded branching engines gate their shared source attempt, rather
// than freezing any per-alternative state word.
// CHECK-LABEL: simulation.func private @unit_6(
// CHECK-SAME: simulation.branching_sequence_monitor
// CHECK: simulation.assert.kill_epoch {{.*}} {simulation.concurrent_kill_epoch_check}
// CHECK: simulation.ref.store
// CHECK: [[BRANCH_ENABLE:%.*]] = simulation.assert.enabled
// CHECK: arith.andi {{.*}}, [[BRANCH_ENABLE]] {simulation.concurrent_attempt_start} : i1
// CHECK-LABEL: simulation.func private @unit_7(
// CHECK-SAME: simulation.branching_consequent_monitor
// CHECK: simulation.assert.kill_epoch {{.*}} {simulation.concurrent_kill_epoch_check}
// CHECK: simulation.ref.store
// CHECK: [[CONSEQUENT_ENABLE:%.*]] = simulation.assert.enabled
// CHECK: arith.andi {{.*}}, [[CONSEQUENT_ENABLE]] {simulation.concurrent_attempt_start} : i1
// CHECK-LABEL: simulation.func private @unit_8(
// CHECK-SAME: simulation.branching_antecedent_monitor
// CHECK: simulation.assert.kill_epoch {{.*}} {simulation.concurrent_kill_epoch_check}
// CHECK: simulation.ref.store
// CHECK: [[ANTECEDENT_ENABLE:%.*]] = simulation.assert.enabled
// CHECK: [[ONE_AGE_TRUTH:%.*]] = simulation.logic.is_true
// CHECK: [[ONE_AGE_START:%.*]] = arith.andi [[ONE_AGE_TRUTH]], [[ANTECEDENT_ENABLE]] {simulation.concurrent_attempt_start} : i1
// The one-age alternative's terminal match must be the gated start, not its
// raw truth; otherwise it could still launch a consequent while Off.
// CHECK: arith.andi [[ONE_AGE_START]], {{.*}} : i1

// Abort remains higher priority for existing attempts. Its per-age report
// tests do not use the control query, while the additional attempt on the
// abort clock is dispatched only when enabled.
// CHECK-LABEL: simulation.func private @unit_9(
// CHECK-SAME: simulation.asynchronous_property_abort
// CHECK: simulation.assert.kill_epoch {{.*}} {simulation.concurrent_kill_epoch_check}
// CHECK: [[ABORT_BODY:\^bb[0-9]+]]([[ABORT_STORAGE:%[0-9]+]]: !simulation.ref<i64>):
// CHECK: [[ABORT_STATE:%.*]] = simulation.ref.load [[ABORT_STORAGE]]
// CHECK: [[ABORT_ENABLE:%.*]] = simulation.assert.enabled
// CHECK: simulation.assert.sampled_read
// CHECK: cf.cond_br {{.*}}, [[ABORTED:\^bb[0-9]+]],
// CHECK: [[ABORTED]]:
// CHECK: arith.andi [[ABORT_STATE]],
// CHECK: cf.cond_br [[ABORT_ENABLE]],

// A report guarded by both disable iff and Kill receives each reference
// immediately followed by its scheduled epoch value. This ordering is part of
// the outlined callback ABI and must also be preserved by every spawn.
// CHECK-LABEL: simulation.func private @unit_10.fork.{{[0-9.]+}}(
// CHECK-SAME: %arg5: !simulation.ref<i64>
// CHECK-SAME: %arg6: i64
// CHECK-SAME: %arg7: !simulation.ref<i64>
// CHECK-SAME: %arg8: i64
// CHECK: [[REPORT_KILL:%.*]] = simulation.assert.kill_epoch
// CHECK: arith.cmpi eq, [[REPORT_KILL]], %arg8 : i64
// CHECK: [[REPORT_DISABLE:%.*]] = simulation.ref.load %arg5
// CHECK: arith.cmpi eq, [[REPORT_DISABLE]], %arg6 : i64

// Disable is tested first and still clears live state/advances its epoch. The
// assertion-control query occurs only on the non-disabled edge.
// CHECK-LABEL: simulation.func private @unit_10(
// CHECK: simulation.suspend.edge {{.*}} to [[DISABLE_SAMPLE:\^bb[0-9]+]]
// CHECK: [[DISABLE_SAMPLE]]([[KILL_REF:%[0-9]+]]: !simulation.ref<i64>, [[LIVE_REF:%[0-9]+]]: !simulation.ref<i64>, [[DISABLE_REF:%[0-9]+]]: !simulation.ref<i64>):
// CHECK: simulation.logic.is_true
// CHECK: cf.cond_br {{.*}}, [[DISABLED:\^bb[0-9]+]]{{.*}}, [[CONTROLLED:\^bb[0-9]+]]
// CHECK: [[DISABLED]]{{.*}}:
// CHECK: simulation.ref.store
// CHECK: cf.br
// CHECK: [[CONTROLLED]]{{.*}}:
// CHECK: simulation.assert.kill_epoch {{.*}} {simulation.concurrent_kill_epoch_check}
// CHECK: [[DISABLE_ENABLE:%.*]] = simulation.assert.enabled
// CHECK: arith.andi {{.*}}, [[DISABLE_ENABLE]] {simulation.concurrent_attempt_start} : i1
// CHECK: [[DISABLE_EXPECTED:%.*]] = simulation.ref.load [[DISABLE_REF]]
// CHECK: [[KILL_EXPECTED:%.*]] = simulation.ref.load [[KILL_REF]]
// CHECK: simulation.spawn @unit_10.fork.{{[0-9.]+}}({{.*}}, [[DISABLE_REF]], [[DISABLE_EXPECTED]], [[KILL_REF]], [[KILL_EXPECTED]])

// A multi-clock coordinator admits source tokens only while enabled. Once
// admitted, its bounded ##1 count remains live if Off is applied before the
// destination occurrence; only the final report remains detached.
// CHECK-LABEL: simulation.func private @unit_11(
// CHECK-SAME: simulation.multiclock_sequence_coordinator
// CHECK: simulation.suspend.clock_set
// CHECK: simulation.assert.clock_occurrence.consume
// CHECK: [[MULTI_ENABLE:%.*]] = simulation.assert.enabled
// CHECK: arith.select [[MULTI_ENABLE]]
// CHECK: simulation.spawn @unit_11.fork.
