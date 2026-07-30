// RUN: obelisk-opt %s '--lower-obelisk-to-sim=opt-level=0' | FileCheck %s

module {
  obelisk.sv.symbol.definition attributes {definition_kind = 0 : i32, hierarchical_name = "assertion_lowering", name = "assertion_lowering", node_id = 0 : i64, sym_name = "s0.assertion_lowering"} {
  }
  obelisk.sv.symbol.definition attributes {definition_kind = 2 : i32, hierarchical_name = "assertion_program", name = "assertion_program", node_id = 1 : i64, sym_name = "s1.assertion_program"} {
  }
  obelisk.sv.symbol.root attributes {hierarchical_name = "\\$root ", name = "$root", node_id = 2 : i64, sym_name = "s2.$root"} {
    obelisk.sv.symbol.compilation_unit attributes {hierarchical_name = "$unit", node_id = 3 : i64, sym_name = "s3"} {
    }
    obelisk.sv.symbol.instance attributes {hierarchical_name = "assertion_lowering", is_uninstantiated = false, name = "assertion_lowering", node_id = 4 : i64, referenced_path = "assertion_lowering", referenced_symbol = @s0.assertion_lowering, sym_name = "s4.assertion_lowering"} {
      obelisk.sv.symbol.instance_body attributes {hierarchical_name = "assertion_lowering", name = "assertion_lowering", node_id = 5 : i64, sym_name = "s5.assertion_lowering"} {
        obelisk.sv.symbol.port attributes {direction = 0 : i32, hierarchical_name = "assertion_lowering.a", name = "a", node_id = 6 : i64, semantic_type = !obelisk.integral<1, false, true, 0 : 0, logic>, sym_name = "s6.a"} {
        }
        obelisk.sv.symbol.variable attributes {hierarchical_name = "assertion_lowering.a", lifetime = 1 : i32, name = "a", node_id = 7 : i64, semantic_type = !obelisk.integral<1, false, true, 0 : 0, logic>, sym_name = "s7.a"} {
        }
        obelisk.sv.symbol.variable attributes {hierarchical_name = "assertion_lowering.b", lifetime = 1 : i32, name = "b", node_id = 8 : i64, semantic_type = !obelisk.integral<1, false, true, 0 : 0, logic>, sym_name = "s8.b"} {
        }
        obelisk.sv.symbol.procedural_block attributes {hierarchical_name = "assertion_lowering", node_id = 9 : i64, procedure_kind = 0 : i32, sym_name = "s9", time_precision_fs = 1000000 : i64, time_unit_fs = 1000000 : i64} {
          obelisk.sv.statement.block attributes {node_id = 10 : i64} {
            obelisk.sv.statement.list attributes {node_id = 11 : i64} {
              obelisk.sv.statement.immediate_assertion attributes {assertion_kind = 0 : i32, has_fail_action = true, has_pass_action = true, is_deferred = false, is_final = false, node_id = 12 : i64} {
                obelisk.sv.expression.named_value attributes {node_id = 13 : i64, referenced_path = "assertion_lowering.a", referenced_symbol = @s2.$root::@s4.assertion_lowering::@s5.assertion_lowering::@s7.a, semantic_type = !obelisk.integral<1, false, true, 0 : 0, logic>} {
                }
                obelisk.sv.statement.expression_statement attributes {node_id = 14 : i64} {
                  obelisk.sv.expression.assignment attributes {assignment_kind = 0 : i32, node_id = 15 : i64, semantic_type = !obelisk.integral<1, false, true, 0 : 0, logic>} {
                    obelisk.sv.expression.named_value attributes {node_id = 16 : i64, referenced_path = "assertion_lowering.b", referenced_symbol = @s2.$root::@s4.assertion_lowering::@s5.assertion_lowering::@s8.b, semantic_type = !obelisk.integral<1, false, true, 0 : 0, logic>} {
                    }
                    obelisk.sv.expression.conversion attributes {node_id = 17 : i64, semantic_type = !obelisk.integral<1, false, true, 0 : 0, logic>} {
                      obelisk.sv.expression.conversion attributes {node_id = 18 : i64, semantic_type = !obelisk.ranged_packed_array<31 : 0 x !obelisk.integral<1, true, true, 0 : 0, logic>>} {
                        obelisk.sv.expression.integer_literal attributes {constant_value = "1", node_id = 19 : i64, semantic_type = !obelisk.integral<32, true, false, 31 : 0, int>} {
                        }
                      }
                    }
                  }
                }
                obelisk.sv.statement.expression_statement attributes {node_id = 20 : i64} {
                  obelisk.sv.expression.assignment attributes {assignment_kind = 0 : i32, node_id = 21 : i64, semantic_type = !obelisk.integral<1, false, true, 0 : 0, logic>} {
                    obelisk.sv.expression.named_value attributes {node_id = 22 : i64, referenced_path = "assertion_lowering.b", referenced_symbol = @s2.$root::@s4.assertion_lowering::@s5.assertion_lowering::@s8.b, semantic_type = !obelisk.integral<1, false, true, 0 : 0, logic>} {
                    }
                    obelisk.sv.expression.conversion attributes {node_id = 23 : i64, semantic_type = !obelisk.integral<1, false, true, 0 : 0, logic>} {
                      obelisk.sv.expression.conversion attributes {node_id = 24 : i64, semantic_type = !obelisk.ranged_packed_array<31 : 0 x !obelisk.integral<1, true, true, 0 : 0, logic>>} {
                        obelisk.sv.expression.integer_literal attributes {constant_value = "0", node_id = 25 : i64, semantic_type = !obelisk.integral<32, true, false, 31 : 0, int>} {
                        }
                      }
                    }
                  }
                }
              }
              obelisk.sv.statement.immediate_assertion attributes {assertion_kind = 1 : i32, has_fail_action = true, has_pass_action = true, is_deferred = false, is_final = false, node_id = 26 : i64} {
                obelisk.sv.expression.named_value attributes {node_id = 27 : i64, referenced_path = "assertion_lowering.a", referenced_symbol = @s2.$root::@s4.assertion_lowering::@s5.assertion_lowering::@s7.a, semantic_type = !obelisk.integral<1, false, true, 0 : 0, logic>} {
                }
                obelisk.sv.statement.expression_statement attributes {node_id = 28 : i64} {
                  obelisk.sv.expression.assignment attributes {assignment_kind = 0 : i32, node_id = 29 : i64, semantic_type = !obelisk.integral<1, false, true, 0 : 0, logic>} {
                    obelisk.sv.expression.named_value attributes {node_id = 30 : i64, referenced_path = "assertion_lowering.b", referenced_symbol = @s2.$root::@s4.assertion_lowering::@s5.assertion_lowering::@s8.b, semantic_type = !obelisk.integral<1, false, true, 0 : 0, logic>} {
                    }
                    obelisk.sv.expression.conversion attributes {node_id = 31 : i64, semantic_type = !obelisk.integral<1, false, true, 0 : 0, logic>} {
                      obelisk.sv.expression.conversion attributes {node_id = 32 : i64, semantic_type = !obelisk.ranged_packed_array<31 : 0 x !obelisk.integral<1, true, true, 0 : 0, logic>>} {
                        obelisk.sv.expression.integer_literal attributes {constant_value = "1", node_id = 33 : i64, semantic_type = !obelisk.integral<32, true, false, 31 : 0, int>} {
                        }
                      }
                    }
                  }
                }
                obelisk.sv.statement.expression_statement attributes {node_id = 34 : i64} {
                  obelisk.sv.expression.assignment attributes {assignment_kind = 0 : i32, node_id = 35 : i64, semantic_type = !obelisk.integral<1, false, true, 0 : 0, logic>} {
                    obelisk.sv.expression.named_value attributes {node_id = 36 : i64, referenced_path = "assertion_lowering.b", referenced_symbol = @s2.$root::@s4.assertion_lowering::@s5.assertion_lowering::@s8.b, semantic_type = !obelisk.integral<1, false, true, 0 : 0, logic>} {
                    }
                    obelisk.sv.expression.conversion attributes {node_id = 37 : i64, semantic_type = !obelisk.integral<1, false, true, 0 : 0, logic>} {
                      obelisk.sv.expression.conversion attributes {node_id = 38 : i64, semantic_type = !obelisk.ranged_packed_array<31 : 0 x !obelisk.integral<1, true, true, 0 : 0, logic>>} {
                        obelisk.sv.expression.integer_literal attributes {constant_value = "0", node_id = 39 : i64, semantic_type = !obelisk.integral<32, true, false, 31 : 0, int>} {
                        }
                      }
                    }
                  }
                }
              }
              obelisk.sv.statement.immediate_assertion attributes {assertion_kind = 2 : i32, has_fail_action = false, has_pass_action = true, is_deferred = false, is_final = false, node_id = 40 : i64} {
                obelisk.sv.expression.named_value attributes {node_id = 41 : i64, referenced_path = "assertion_lowering.a", referenced_symbol = @s2.$root::@s4.assertion_lowering::@s5.assertion_lowering::@s7.a, semantic_type = !obelisk.integral<1, false, true, 0 : 0, logic>} {
                }
                obelisk.sv.statement.expression_statement attributes {node_id = 42 : i64} {
                  obelisk.sv.expression.assignment attributes {assignment_kind = 0 : i32, node_id = 43 : i64, semantic_type = !obelisk.integral<1, false, true, 0 : 0, logic>} {
                    obelisk.sv.expression.named_value attributes {node_id = 44 : i64, referenced_path = "assertion_lowering.b", referenced_symbol = @s2.$root::@s4.assertion_lowering::@s5.assertion_lowering::@s8.b, semantic_type = !obelisk.integral<1, false, true, 0 : 0, logic>} {
                    }
                    obelisk.sv.expression.conversion attributes {node_id = 45 : i64, semantic_type = !obelisk.integral<1, false, true, 0 : 0, logic>} {
                      obelisk.sv.expression.conversion attributes {node_id = 46 : i64, semantic_type = !obelisk.ranged_packed_array<31 : 0 x !obelisk.integral<1, true, true, 0 : 0, logic>>} {
                        obelisk.sv.expression.integer_literal attributes {constant_value = "1", node_id = 47 : i64, semantic_type = !obelisk.integral<32, true, false, 31 : 0, int>} {
                        }
                      }
                    }
                  }
                }
              }
              obelisk.sv.statement.immediate_assertion attributes {assertion_kind = 0 : i32, has_fail_action = true, has_pass_action = true, is_deferred = true, is_final = false, node_id = 48 : i64} {
                obelisk.sv.expression.named_value attributes {node_id = 49 : i64, referenced_path = "assertion_lowering.a", referenced_symbol = @s2.$root::@s4.assertion_lowering::@s5.assertion_lowering::@s7.a, semantic_type = !obelisk.integral<1, false, true, 0 : 0, logic>} {
                }
                obelisk.sv.statement.expression_statement attributes {node_id = 50 : i64} {
                  obelisk.sv.expression.call attributes {argument_count = 1 : i64, callee_name = "$display", constraint_restrictions = [], defaulted_arguments = array<i64: 0>, has_inline_constraints = false, has_iterator_expression = false, has_output_arguments = false, has_this_class = false, is_super_class = false, is_system_call = true, node_id = 51 : i64, semantic_type = !obelisk.void, subroutine_kind = 1 : i32, system_library_cell = "work.assertion_lowering", system_scope_path = "assertion_lowering", system_scope_symbol = @s2.$root::@s4.assertion_lowering::@s5.assertion_lowering} {
                    obelisk.sv.expression.string_literal attributes {constant_value = "pass", node_id = 52 : i64, semantic_type = !obelisk.ranged_packed_array<31 : 0 x !obelisk.integral<1, false, false, 0 : 0, bit>>} {
                    }
                  }
                }
                obelisk.sv.statement.expression_statement attributes {node_id = 53 : i64} {
                  obelisk.sv.expression.call attributes {argument_count = 1 : i64, callee_name = "$display", constraint_restrictions = [], defaulted_arguments = array<i64: 0>, has_inline_constraints = false, has_iterator_expression = false, has_output_arguments = false, has_this_class = false, is_super_class = false, is_system_call = true, node_id = 54 : i64, semantic_type = !obelisk.void, subroutine_kind = 1 : i32, system_library_cell = "work.assertion_lowering", system_scope_path = "assertion_lowering", system_scope_symbol = @s2.$root::@s4.assertion_lowering::@s5.assertion_lowering} {
                    obelisk.sv.expression.string_literal attributes {constant_value = "fail", node_id = 55 : i64, semantic_type = !obelisk.ranged_packed_array<31 : 0 x !obelisk.integral<1, false, false, 0 : 0, bit>>} {
                    }
                  }
                }
              }
              obelisk.sv.statement.immediate_assertion attributes {assertion_kind = 0 : i32, has_fail_action = false, has_pass_action = true, is_deferred = true, is_final = false, node_id = 56 : i64} {
                obelisk.sv.expression.named_value attributes {node_id = 57 : i64, referenced_path = "assertion_lowering.a", referenced_symbol = @s2.$root::@s4.assertion_lowering::@s5.assertion_lowering::@s7.a, semantic_type = !obelisk.integral<1, false, true, 0 : 0, logic>} {
                }
                obelisk.sv.statement.empty attributes {node_id = 58 : i64} {
                }
              }
              obelisk.sv.statement.immediate_assertion attributes {assertion_kind = 0 : i32, has_fail_action = false, has_pass_action = true, is_deferred = true, is_final = true, node_id = 59 : i64} {
                obelisk.sv.expression.named_value attributes {node_id = 60 : i64, referenced_path = "assertion_lowering.a", referenced_symbol = @s2.$root::@s4.assertion_lowering::@s5.assertion_lowering::@s7.a, semantic_type = !obelisk.integral<1, false, true, 0 : 0, logic>} {
                }
                obelisk.sv.statement.empty attributes {node_id = 61 : i64} {
                }
              }
            }
          }
        }
      }
    }
    obelisk.sv.symbol.instance attributes {hierarchical_name = "assertion_program", is_uninstantiated = false, name = "assertion_program", node_id = 62 : i64, referenced_path = "assertion_program", referenced_symbol = @s1.assertion_program, sym_name = "s10.assertion_program"} {
      obelisk.sv.symbol.instance_body attributes {hierarchical_name = "assertion_program", name = "assertion_program", node_id = 63 : i64, sym_name = "s11.assertion_program"} {
        obelisk.sv.symbol.port attributes {direction = 0 : i32, hierarchical_name = "assertion_program.a", name = "a", node_id = 64 : i64, semantic_type = !obelisk.integral<1, false, true, 0 : 0, logic>, sym_name = "s12.a"} {
        }
        obelisk.sv.symbol.variable attributes {hierarchical_name = "assertion_program.a", lifetime = 1 : i32, name = "a", node_id = 65 : i64, semantic_type = !obelisk.integral<1, false, true, 0 : 0, logic>, sym_name = "s13.a"} {
        }
        obelisk.sv.symbol.procedural_block attributes {hierarchical_name = "assertion_program", node_id = 66 : i64, procedure_kind = 0 : i32, sym_name = "s14", time_precision_fs = 1000000 : i64, time_unit_fs = 1000000 : i64} {
          obelisk.sv.statement.immediate_assertion attributes {assertion_kind = 0 : i32, has_fail_action = false, has_pass_action = true, is_deferred = true, is_final = false, node_id = 67 : i64} {
            obelisk.sv.expression.named_value attributes {node_id = 68 : i64, referenced_path = "assertion_program.a", referenced_symbol = @s2.$root::@s10.assertion_program::@s11.assertion_program::@s13.a, semantic_type = !obelisk.integral<1, false, true, 0 : 0, logic>} {
            }
            obelisk.sv.statement.empty attributes {node_id = 69 : i64} {
            }
          }
        }
      }
    }
  }
}

// Ordinary immediate assertions are explicit control flow.
// CHECK-DAG: cf.cond_br
// CHECK-DAG: obelisk_sim.ref.store
// CHECK-DAG: "ERROR: {{.*}}immediate assertion failed."

// Deferred sites are coalesced before evaluator processes are spawned.
// CHECK-DAG: obelisk_sim.assert.deferred_once
// CHECK-DAG: home_region = 8 : i32
// CHECK-DAG: home_region = 10 : i32
// CHECK-DAG: home_region = 16 : i32

// Both Observed/Postponed evaluators and the Reactive default action use the
// design domain, including when their enclosing procedural domain differs.
// CHECK-DAG: domain = 0 : i32
// CHECK-NOT: obelisk.sv.
