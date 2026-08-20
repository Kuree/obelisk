// REQUIRES: z3
// RUN: obelisk-opt %s '--lower-obelisk-to-sim=opt-level=0' | FileCheck %s
// RUN: obelisk-opt %s '--lower-obelisk-to-sim=opt-level=3' | FileCheck %s
// RUN: obelisk-opt %s '--lower-obelisk-to-sim=opt-level=3' '--encode-obelisk-sim-to-bytecode=vpi=off' -o /dev/null

module attributes {llvm.data_layout = "e-m:e-p:64:64-i64:64-n8:16:32:64-S128", llvm.target_triple = "x86_64-unknown-linux-gnu"} {
  obelisk.sv.symbol.definition attributes {definition_kind = 0 : i32, hierarchical_name = "top", name = "top", node_id = 0 : i64, sym_name = "s0.top"} {
  }
  obelisk.sv.symbol.root attributes {hierarchical_name = "\\$root ", name = "$root", node_id = 1 : i64, sym_name = "s1.$root"} {
    obelisk.sv.symbol.compilation_unit attributes {hierarchical_name = "$unit", node_id = 2 : i64, sym_name = "s2"} {
    }
    obelisk.sv.symbol.instance attributes {hierarchical_name = "top", is_uninstantiated = false, name = "top", node_id = 3 : i64, referenced_path = "top", referenced_symbol = @s0.top, sym_name = "s3.top"} {
      obelisk.sv.symbol.instance_body attributes {hierarchical_name = "top", name = "top", node_id = 4 : i64, sym_name = "s4.top"} {
        obelisk.sv.symbol.variable attributes {hierarchical_name = "top.clk", lifetime = 1 : i32, name = "clk", node_id = 5 : i64, semantic_type = !obelisk.integral<1, false, true, 0 : 0, logic>, sym_name = "s5.clk"} {
        }
        obelisk.sv.symbol.variable attributes {hierarchical_name = "top.a", lifetime = 1 : i32, name = "a", node_id = 6 : i64, semantic_type = !obelisk.integral<1, false, true, 0 : 0, logic>, sym_name = "s6.a"} {
        }
        obelisk.sv.symbol.variable attributes {hierarchical_name = "top.b", lifetime = 1 : i32, name = "b", node_id = 7 : i64, semantic_type = !obelisk.integral<1, false, true, 0 : 0, logic>, sym_name = "s7.b"} {
        }
        obelisk.sv.symbol.variable attributes {hierarchical_name = "top.c", lifetime = 1 : i32, name = "c", node_id = 8 : i64, semantic_type = !obelisk.integral<1, false, true, 0 : 0, logic>, sym_name = "s8.c"} {
        }
        obelisk.sv.symbol.variable attributes {hierarchical_name = "top.d", lifetime = 1 : i32, name = "d", node_id = 9 : i64, semantic_type = !obelisk.integral<1, false, true, 0 : 0, logic>, sym_name = "s9.d"} {
        }
        obelisk.sv.symbol.variable attributes {hierarchical_name = "top.e", lifetime = 1 : i32, name = "e", node_id = 91 : i64, semantic_type = !obelisk.integral<1, false, true, 0 : 0, logic>, sym_name = "s91.e"} {
        }
        obelisk.sv.symbol.variable attributes {hierarchical_name = "top.f", lifetime = 1 : i32, name = "f", node_id = 92 : i64, semantic_type = !obelisk.integral<1, false, true, 0 : 0, logic>, sym_name = "s92.f"} {
        }
        obelisk.sv.symbol.variable attributes {hierarchical_name = "top.sel", lifetime = 1 : i32, name = "sel", node_id = 90 : i64, semantic_type = !obelisk.ranged_packed_array<1 : 0 x !obelisk.integral<1, false, true, 0 : 0, logic>>, sym_name = "s90.sel"} {
        }

        // cover property ((if (a) b) implies c). The missing else makes !a a
        // vacuous true LHS. The result has two vacuous success regions:
        // a&&!b (false LHS) and !a&&c (vacuous LHS with true RHS), plus the
        // nonvacuous a&&b&&c region. All overlap is coalesced to one pass.
        obelisk.sv.symbol.procedural_block attributes {hierarchical_name = "top", node_id = 10 : i64, procedure_kind = 2 : i32, sym_name = "s10", time_precision_fs = 1000000 : i64, time_unit_fs = 1000000 : i64} {
          obelisk.sv.statement.concurrent_assertion attributes {assertion_kind = 2 : i32, has_default_disable = false, has_fail_action = false, has_pass_action = true, node_id = 11 : i64} {
            obelisk.sv.assertion.clocking attributes {node_id = 12 : i64} {
              obelisk.sv.timing.signal_event attributes {edge_kind = 1 : i32, has_iff = false, node_id = 13 : i64} {
                obelisk.sv.expression.named_value attributes {node_id = 14 : i64, referenced_path = "top.clk", referenced_symbol = @s1.$root::@s3.top::@s4.top::@s5.clk, semantic_type = !obelisk.integral<1, false, true, 0 : 0, logic>} {
                }
              }
              obelisk.sv.assertion.binary attributes {node_id = 15 : i64, operator_kind = 10 : i32} {
                obelisk.sv.assertion.conditional attributes {has_else = false, node_id = 16 : i64} {
                  obelisk.sv.expression.named_value attributes {node_id = 17 : i64, referenced_path = "top.a", referenced_symbol = @s1.$root::@s3.top::@s4.top::@s6.a, semantic_type = !obelisk.integral<1, false, true, 0 : 0, logic>} {
                  }
                  obelisk.sv.assertion.simple attributes {has_repetition = false, is_null = false, node_id = 18 : i64, repetition_is_unbounded = false} {
                    obelisk.sv.expression.named_value attributes {node_id = 19 : i64, referenced_path = "top.b", referenced_symbol = @s1.$root::@s3.top::@s4.top::@s7.b, semantic_type = !obelisk.integral<1, false, true, 0 : 0, logic>} {
                    }
                  }
                }
                obelisk.sv.assertion.simple attributes {has_repetition = false, is_null = false, node_id = 20 : i64, repetition_is_unbounded = false} {
                  obelisk.sv.expression.named_value attributes {node_id = 21 : i64, referenced_path = "top.c", referenced_symbol = @s1.$root::@s3.top::@s4.top::@s8.c, semantic_type = !obelisk.integral<1, false, true, 0 : 0, logic>} {
                  }
                }
              }
            }
            obelisk.sv.statement.expression_statement attributes {node_id = 22 : i64} {
              obelisk.sv.expression.call attributes {argument_count = 0 : i64, callee_name = "$display", constraint_restrictions = [], defaulted_arguments = array<i64>, has_inline_constraints = false, has_iterator_expression = false, has_output_arguments = false, has_this_class = false, is_signed = false, is_super_class = false, is_system_call = true, node_id = 23 : i64, semantic_type = !obelisk.void, subroutine_kind = 1 : i32, system_library_cell = "work.top", system_scope_path = "top", system_scope_symbol = @s1.$root::@s3.top::@s4.top} {
              }
            }
          }
        }

        // cover property ((if (a) b) iff (if (c) d)). Both missing-else
        // operands are vacuous only together on !a&&!c. Every other successful
        // equality is nonvacuous because at least one operand is nonvacuous.
        obelisk.sv.symbol.procedural_block attributes {hierarchical_name = "top", node_id = 40 : i64, procedure_kind = 2 : i32, sym_name = "s40", time_precision_fs = 1000000 : i64, time_unit_fs = 1000000 : i64} {
          obelisk.sv.statement.concurrent_assertion attributes {assertion_kind = 2 : i32, has_default_disable = false, has_fail_action = false, has_pass_action = true, node_id = 41 : i64} {
            obelisk.sv.assertion.clocking attributes {node_id = 42 : i64} {
              obelisk.sv.timing.signal_event attributes {edge_kind = 1 : i32, has_iff = false, node_id = 43 : i64} {
                obelisk.sv.expression.named_value attributes {node_id = 44 : i64, referenced_path = "top.clk", referenced_symbol = @s1.$root::@s3.top::@s4.top::@s5.clk, semantic_type = !obelisk.integral<1, false, true, 0 : 0, logic>} {
                }
              }
              obelisk.sv.assertion.binary attributes {node_id = 45 : i64, operator_kind = 5 : i32} {
                obelisk.sv.assertion.conditional attributes {has_else = false, node_id = 46 : i64} {
                  obelisk.sv.expression.named_value attributes {node_id = 47 : i64, referenced_path = "top.a", referenced_symbol = @s1.$root::@s3.top::@s4.top::@s6.a, semantic_type = !obelisk.integral<1, false, true, 0 : 0, logic>} {
                  }
                  obelisk.sv.assertion.simple attributes {has_repetition = false, is_null = false, node_id = 48 : i64, repetition_is_unbounded = false} {
                    obelisk.sv.expression.named_value attributes {node_id = 49 : i64, referenced_path = "top.b", referenced_symbol = @s1.$root::@s3.top::@s4.top::@s7.b, semantic_type = !obelisk.integral<1, false, true, 0 : 0, logic>} {
                    }
                  }
                }
                obelisk.sv.assertion.conditional attributes {has_else = false, node_id = 50 : i64} {
                  obelisk.sv.expression.named_value attributes {node_id = 51 : i64, referenced_path = "top.c", referenced_symbol = @s1.$root::@s3.top::@s4.top::@s8.c, semantic_type = !obelisk.integral<1, false, true, 0 : 0, logic>} {
                  }
                  obelisk.sv.assertion.simple attributes {has_repetition = false, is_null = false, node_id = 52 : i64, repetition_is_unbounded = false} {
                    obelisk.sv.expression.named_value attributes {node_id = 53 : i64, referenced_path = "top.d", referenced_symbol = @s1.$root::@s3.top::@s4.top::@s9.d, semantic_type = !obelisk.integral<1, false, true, 0 : 0, logic>} {
                    }
                  }
                }
              }
            }
            obelisk.sv.statement.expression_statement attributes {node_id = 54 : i64} {
              obelisk.sv.expression.call attributes {argument_count = 0 : i64, callee_name = "$display", constraint_restrictions = [], defaulted_arguments = array<i64>, has_inline_constraints = false, has_iterator_expression = false, has_output_arguments = false, has_this_class = false, is_signed = false, is_super_class = false, is_system_call = true, node_id = 55 : i64, semantic_type = !obelisk.void, subroutine_kind = 1 : i32, system_library_cell = "work.top", system_scope_path = "top", system_scope_symbol = @s1.$root::@s3.top::@s4.top} {
              }
            }
          }
        }

        // A missing case default has the same vacuity algebra while retaining
        // four-state case equality for its selected branch.
        obelisk.sv.symbol.procedural_block attributes {hierarchical_name = "top", node_id = 70 : i64, procedure_kind = 2 : i32, sym_name = "s70", time_precision_fs = 1000000 : i64, time_unit_fs = 1000000 : i64} {
          obelisk.sv.statement.concurrent_assertion attributes {assertion_kind = 2 : i32, has_default_disable = false, has_fail_action = false, has_pass_action = true, node_id = 71 : i64} {
            obelisk.sv.assertion.clocking attributes {node_id = 72 : i64} {
              obelisk.sv.timing.signal_event attributes {edge_kind = 1 : i32, has_iff = false, node_id = 73 : i64} {
                obelisk.sv.expression.named_value attributes {node_id = 74 : i64, referenced_path = "top.clk", referenced_symbol = @s1.$root::@s3.top::@s4.top::@s5.clk, semantic_type = !obelisk.integral<1, false, true, 0 : 0, logic>} {
                }
              }
              obelisk.sv.assertion.binary attributes {node_id = 75 : i64, operator_kind = 10 : i32} {
                obelisk.sv.assertion.case attributes {has_default = false, item_group_sizes = [1], node_id = 76 : i64} {
                  obelisk.sv.expression.named_value attributes {node_id = 77 : i64, referenced_path = "top.sel", referenced_symbol = @s1.$root::@s3.top::@s4.top::@s90.sel, semantic_type = !obelisk.ranged_packed_array<1 : 0 x !obelisk.integral<1, false, true, 0 : 0, logic>>} {
                  }
                  obelisk.sv.expression.integer_literal attributes {constant_value = "2'b01", node_id = 78 : i64, semantic_type = !obelisk.ranged_packed_array<1 : 0 x !obelisk.integral<1, false, false, 0 : 0, bit>>} {
                  }
                  obelisk.sv.assertion.simple attributes {has_repetition = false, is_null = false, node_id = 79 : i64, repetition_is_unbounded = false} {
                    obelisk.sv.expression.named_value attributes {node_id = 80 : i64, referenced_path = "top.b", referenced_symbol = @s1.$root::@s3.top::@s4.top::@s7.b, semantic_type = !obelisk.integral<1, false, true, 0 : 0, logic>} {
                    }
                  }
                }
                obelisk.sv.assertion.simple attributes {has_repetition = false, is_null = false, node_id = 81 : i64, repetition_is_unbounded = false} {
                  obelisk.sv.expression.named_value attributes {node_id = 82 : i64, referenced_path = "top.c", referenced_symbol = @s1.$root::@s3.top::@s4.top::@s8.c, semantic_type = !obelisk.integral<1, false, true, 0 : 0, logic>} {
                  }
                }
              }
            }
            obelisk.sv.statement.expression_statement attributes {node_id = 83 : i64} {
              obelisk.sv.expression.call attributes {argument_count = 0 : i64, callee_name = "$display", constraint_restrictions = [], defaulted_arguments = array<i64>, has_inline_constraints = false, has_iterator_expression = false, has_output_arguments = false, has_this_class = false, is_signed = false, is_super_class = false, is_system_call = true, node_id = 84 : i64, semantic_type = !obelisk.void, subroutine_kind = 1 : i32, system_library_cell = "work.top", system_scope_path = "top", system_scope_symbol = @s1.$root::@s3.top::@s4.top} {
              }
            }
          }
        }

        // cover property (((a implies b) implies c) iff
        //                 ((d implies e) implies f)). With a=d=0 and c=f=0,
        // both inner implications are vacuous true, both enclosing
        // implications are vacuous false, and the outer iff is vacuous true.
        // Retaining only successful intermediate DNFs would misclassify this
        // region as nonvacuous.
        obelisk.sv.symbol.procedural_block attributes {hierarchical_name = "top", node_id = 100 : i64, procedure_kind = 2 : i32, sym_name = "s100", time_precision_fs = 1000000 : i64, time_unit_fs = 1000000 : i64} {
          obelisk.sv.statement.concurrent_assertion attributes {assertion_kind = 2 : i32, has_default_disable = false, has_fail_action = false, has_pass_action = true, node_id = 101 : i64} {
            obelisk.sv.assertion.clocking attributes {node_id = 102 : i64} {
              obelisk.sv.timing.signal_event attributes {edge_kind = 1 : i32, has_iff = false, node_id = 103 : i64} {
                obelisk.sv.expression.named_value attributes {node_id = 104 : i64, referenced_path = "top.clk", referenced_symbol = @s1.$root::@s3.top::@s4.top::@s5.clk, semantic_type = !obelisk.integral<1, false, true, 0 : 0, logic>} {
                }
              }
              obelisk.sv.assertion.binary attributes {node_id = 105 : i64, operator_kind = 5 : i32} {
                obelisk.sv.assertion.binary attributes {node_id = 106 : i64, operator_kind = 10 : i32} {
                  obelisk.sv.assertion.binary attributes {node_id = 107 : i64, operator_kind = 10 : i32} {
                    obelisk.sv.assertion.simple attributes {has_repetition = false, is_null = false, node_id = 108 : i64, repetition_is_unbounded = false} {
                      obelisk.sv.expression.named_value attributes {node_id = 109 : i64, referenced_path = "top.a", referenced_symbol = @s1.$root::@s3.top::@s4.top::@s6.a, semantic_type = !obelisk.integral<1, false, true, 0 : 0, logic>} {
                      }
                    }
                    obelisk.sv.assertion.simple attributes {has_repetition = false, is_null = false, node_id = 110 : i64, repetition_is_unbounded = false} {
                      obelisk.sv.expression.named_value attributes {node_id = 111 : i64, referenced_path = "top.b", referenced_symbol = @s1.$root::@s3.top::@s4.top::@s7.b, semantic_type = !obelisk.integral<1, false, true, 0 : 0, logic>} {
                      }
                    }
                  }
                  obelisk.sv.assertion.simple attributes {has_repetition = false, is_null = false, node_id = 112 : i64, repetition_is_unbounded = false} {
                    obelisk.sv.expression.named_value attributes {node_id = 113 : i64, referenced_path = "top.c", referenced_symbol = @s1.$root::@s3.top::@s4.top::@s8.c, semantic_type = !obelisk.integral<1, false, true, 0 : 0, logic>} {
                    }
                  }
                }
                obelisk.sv.assertion.binary attributes {node_id = 114 : i64, operator_kind = 10 : i32} {
                  obelisk.sv.assertion.binary attributes {node_id = 115 : i64, operator_kind = 10 : i32} {
                    obelisk.sv.assertion.simple attributes {has_repetition = false, is_null = false, node_id = 116 : i64, repetition_is_unbounded = false} {
                      obelisk.sv.expression.named_value attributes {node_id = 117 : i64, referenced_path = "top.d", referenced_symbol = @s1.$root::@s3.top::@s4.top::@s9.d, semantic_type = !obelisk.integral<1, false, true, 0 : 0, logic>} {
                      }
                    }
                    obelisk.sv.assertion.simple attributes {has_repetition = false, is_null = false, node_id = 118 : i64, repetition_is_unbounded = false} {
                      obelisk.sv.expression.named_value attributes {node_id = 119 : i64, referenced_path = "top.e", referenced_symbol = @s1.$root::@s3.top::@s4.top::@s91.e, semantic_type = !obelisk.integral<1, false, true, 0 : 0, logic>} {
                      }
                    }
                  }
                  obelisk.sv.assertion.simple attributes {has_repetition = false, is_null = false, node_id = 120 : i64, repetition_is_unbounded = false} {
                    obelisk.sv.expression.named_value attributes {node_id = 121 : i64, referenced_path = "top.f", referenced_symbol = @s1.$root::@s3.top::@s4.top::@s92.f, semantic_type = !obelisk.integral<1, false, true, 0 : 0, logic>} {
                    }
                  }
                }
              }
            }
            obelisk.sv.statement.expression_statement attributes {node_id = 122 : i64} {
              obelisk.sv.expression.call attributes {argument_count = 0 : i64, callee_name = "$display", constraint_restrictions = [], defaulted_arguments = array<i64>, has_inline_constraints = false, has_iterator_expression = false, has_output_arguments = false, has_this_class = false, is_signed = false, is_super_class = false, is_system_call = true, node_id = 123 : i64, semantic_type = !obelisk.void, subroutine_kind = 1 : i32, system_library_cell = "work.top", system_scope_path = "top", system_scope_symbol = @s1.$root::@s3.top::@s4.top} {
              }
            }
          }
        }

        // cover property
        //   (not ((if (a) b) implies c) or (if (d) e)) and (if (f) a).
        // At a=d=f=0 and c=0, the implication is vacuous false, its negation
        // and both conditional fallthroughs are vacuous true, and the nested
        // or/and result is therefore vacuous true. This exercises every
        // supported one-cycle vacuity-composition rule in one expression.
        obelisk.sv.symbol.procedural_block attributes {hierarchical_name = "top", node_id = 130 : i64, procedure_kind = 2 : i32, sym_name = "s130", time_precision_fs = 1000000 : i64, time_unit_fs = 1000000 : i64} {
          obelisk.sv.statement.concurrent_assertion attributes {assertion_kind = 2 : i32, has_default_disable = false, has_fail_action = false, has_pass_action = true, node_id = 131 : i64} {
            obelisk.sv.assertion.clocking attributes {node_id = 132 : i64} {
              obelisk.sv.timing.signal_event attributes {edge_kind = 1 : i32, has_iff = false, node_id = 133 : i64} {
                obelisk.sv.expression.named_value attributes {node_id = 134 : i64, referenced_path = "top.clk", referenced_symbol = @s1.$root::@s3.top::@s4.top::@s5.clk, semantic_type = !obelisk.integral<1, false, true, 0 : 0, logic>} {
                }
              }
              obelisk.sv.assertion.binary attributes {node_id = 135 : i64, operator_kind = 0 : i32} {
                obelisk.sv.assertion.binary attributes {node_id = 136 : i64, operator_kind = 1 : i32} {
                  obelisk.sv.assertion.unary attributes {has_range = false, node_id = 137 : i64, operator_kind = 0 : i32, range_is_unbounded = false} {
                    obelisk.sv.assertion.binary attributes {node_id = 138 : i64, operator_kind = 10 : i32} {
                      obelisk.sv.assertion.conditional attributes {has_else = false, node_id = 139 : i64} {
                        obelisk.sv.expression.named_value attributes {node_id = 140 : i64, referenced_path = "top.a", referenced_symbol = @s1.$root::@s3.top::@s4.top::@s6.a, semantic_type = !obelisk.integral<1, false, true, 0 : 0, logic>} {
                        }
                        obelisk.sv.assertion.simple attributes {has_repetition = false, is_null = false, node_id = 141 : i64, repetition_is_unbounded = false} {
                          obelisk.sv.expression.named_value attributes {node_id = 142 : i64, referenced_path = "top.b", referenced_symbol = @s1.$root::@s3.top::@s4.top::@s7.b, semantic_type = !obelisk.integral<1, false, true, 0 : 0, logic>} {
                          }
                        }
                      }
                      obelisk.sv.assertion.simple attributes {has_repetition = false, is_null = false, node_id = 143 : i64, repetition_is_unbounded = false} {
                        obelisk.sv.expression.named_value attributes {node_id = 144 : i64, referenced_path = "top.c", referenced_symbol = @s1.$root::@s3.top::@s4.top::@s8.c, semantic_type = !obelisk.integral<1, false, true, 0 : 0, logic>} {
                        }
                      }
                    }
                  }
                  obelisk.sv.assertion.conditional attributes {has_else = false, node_id = 145 : i64} {
                    obelisk.sv.expression.named_value attributes {node_id = 146 : i64, referenced_path = "top.d", referenced_symbol = @s1.$root::@s3.top::@s4.top::@s9.d, semantic_type = !obelisk.integral<1, false, true, 0 : 0, logic>} {
                    }
                    obelisk.sv.assertion.simple attributes {has_repetition = false, is_null = false, node_id = 147 : i64, repetition_is_unbounded = false} {
                      obelisk.sv.expression.named_value attributes {node_id = 148 : i64, referenced_path = "top.e", referenced_symbol = @s1.$root::@s3.top::@s4.top::@s91.e, semantic_type = !obelisk.integral<1, false, true, 0 : 0, logic>} {
                      }
                    }
                  }
                }
                obelisk.sv.assertion.conditional attributes {has_else = false, node_id = 149 : i64} {
                  obelisk.sv.expression.named_value attributes {node_id = 150 : i64, referenced_path = "top.f", referenced_symbol = @s1.$root::@s3.top::@s4.top::@s92.f, semantic_type = !obelisk.integral<1, false, true, 0 : 0, logic>} {
                  }
                  obelisk.sv.assertion.simple attributes {has_repetition = false, is_null = false, node_id = 151 : i64, repetition_is_unbounded = false} {
                    obelisk.sv.expression.named_value attributes {node_id = 152 : i64, referenced_path = "top.a", referenced_symbol = @s1.$root::@s3.top::@s4.top::@s6.a, semantic_type = !obelisk.integral<1, false, true, 0 : 0, logic>} {
                    }
                  }
                }
              }
            }
            obelisk.sv.statement.expression_statement attributes {node_id = 153 : i64} {
              obelisk.sv.expression.call attributes {argument_count = 0 : i64, callee_name = "$display", constraint_restrictions = [], defaulted_arguments = array<i64>, has_inline_constraints = false, has_iterator_expression = false, has_output_arguments = false, has_this_class = false, is_signed = false, is_super_class = false, is_system_call = true, node_id = 154 : i64, semantic_type = !obelisk.void, subroutine_kind = 1 : i32, system_library_cell = "work.top", system_scope_path = "top", system_scope_symbol = @s1.$root::@s3.top::@s4.top} {
              }
            }
          }
        }

        // cover property not not (a implies b). The first not has only a
        // false-vacuous region when a is false; the second not must recover
        // that region as a vacuous success rather than a plain Boolean cube.
        obelisk.sv.symbol.procedural_block attributes {hierarchical_name = "top", node_id = 160 : i64, procedure_kind = 2 : i32, sym_name = "s160", time_precision_fs = 1000000 : i64, time_unit_fs = 1000000 : i64} {
          obelisk.sv.statement.concurrent_assertion attributes {assertion_kind = 2 : i32, has_default_disable = false, has_fail_action = false, has_pass_action = true, node_id = 161 : i64} {
            obelisk.sv.assertion.clocking attributes {node_id = 162 : i64} {
              obelisk.sv.timing.signal_event attributes {edge_kind = 1 : i32, has_iff = false, node_id = 163 : i64} {
                obelisk.sv.expression.named_value attributes {node_id = 164 : i64, referenced_path = "top.clk", referenced_symbol = @s1.$root::@s3.top::@s4.top::@s5.clk, semantic_type = !obelisk.integral<1, false, true, 0 : 0, logic>} {
                }
              }
              obelisk.sv.assertion.unary attributes {has_range = false, node_id = 165 : i64, operator_kind = 0 : i32, range_is_unbounded = false} {
                obelisk.sv.assertion.unary attributes {has_range = false, node_id = 166 : i64, operator_kind = 0 : i32, range_is_unbounded = false} {
                  obelisk.sv.assertion.binary attributes {node_id = 167 : i64, operator_kind = 10 : i32} {
                    obelisk.sv.assertion.simple attributes {has_repetition = false, is_null = false, node_id = 168 : i64, repetition_is_unbounded = false} {
                      obelisk.sv.expression.named_value attributes {node_id = 169 : i64, referenced_path = "top.a", referenced_symbol = @s1.$root::@s3.top::@s4.top::@s6.a, semantic_type = !obelisk.integral<1, false, true, 0 : 0, logic>} {
                      }
                    }
                    obelisk.sv.assertion.simple attributes {has_repetition = false, is_null = false, node_id = 170 : i64, repetition_is_unbounded = false} {
                      obelisk.sv.expression.named_value attributes {node_id = 171 : i64, referenced_path = "top.b", referenced_symbol = @s1.$root::@s3.top::@s4.top::@s7.b, semantic_type = !obelisk.integral<1, false, true, 0 : 0, logic>} {
                      }
                    }
                  }
                }
              }
            }
            obelisk.sv.statement.expression_statement attributes {node_id = 172 : i64} {
              obelisk.sv.expression.call attributes {argument_count = 0 : i64, callee_name = "$display", constraint_restrictions = [], defaulted_arguments = array<i64>, has_inline_constraints = false, has_iterator_expression = false, has_output_arguments = false, has_this_class = false, is_signed = false, is_super_class = false, is_system_call = true, node_id = 173 : i64, semantic_type = !obelisk.void, subroutine_kind = 1 : i32, system_library_cell = "work.top", system_scope_path = "top", system_scope_symbol = @s1.$root::@s3.top::@s4.top} {
              }
            }
          }
        }
      }
    }
  }
}

// The exact partition initially emits four successful cubes. Z3 removes one
// redundant cube within its vacuity class without crossing classification.
// CHECK-LABEL: obelisk_sim.func private @unit_0(
// CHECK-SAME: obelisk_sim.branching_sequence_alternatives = 3 : i64
// CHECK-SAME: obelisk_sim.sva_boolean_alternatives_after = 3 : i64
// CHECK-SAME: obelisk_sim.sva_boolean_alternatives_before = 4 : i64
// CHECK-SAME: obelisk_sim.sva_boolean_literals_after = 7 : i64
// CHECK-SAME: obelisk_sim.sva_boolean_literals_before = 11 : i64
// CHECK-SAME: obelisk_sim.sva_boolean_solver = "z3"
// CHECK-SAME: obelisk_sim.vacuous_sequence_alternatives = 2 : i64
// CHECK-COUNT-3: obelisk_sim.assert.sampled_read
// CHECK-NOT: obelisk_sim.assert.sampled_read
// CHECK-COUNT-1: obelisk_sim.spawn @unit_0.fork.11.0.0
// CHECK-NOT: obelisk_sim.spawn @unit_0.fork.11.0.0

// `iff` keeps only !a&&!c vacuous. The other four successful regions are
// nonvacuous, including equality through two false operand evaluations.
// CHECK-LABEL: obelisk_sim.func private @unit_1(
// CHECK-SAME: obelisk_sim.branching_sequence_alternatives = 5 : i64
// CHECK-SAME: obelisk_sim.sva_boolean_alternatives_after = 5 : i64
// CHECK-SAME: obelisk_sim.sva_boolean_alternatives_before = 5 : i64
// CHECK-SAME: obelisk_sim.sva_boolean_literals_after = 15 : i64
// CHECK-SAME: obelisk_sim.sva_boolean_literals_before = 16 : i64
// CHECK-SAME: obelisk_sim.sva_boolean_solver = "z3"
// CHECK-SAME: obelisk_sim.vacuous_sequence_alternatives = 1 : i64
// CHECK-COUNT-4: obelisk_sim.assert.sampled_read
// CHECK-NOT: obelisk_sim.assert.sampled_read
// CHECK-COUNT-1: obelisk_sim.spawn @unit_1.fork.41.0.0
// CHECK-NOT: obelisk_sim.spawn @unit_1.fork.41.0.0

// The case form retains one selector comparison while producing the same
// 4->3 solver reduction and two vacuous success regions as the conditional.
// CHECK-LABEL: obelisk_sim.func private @unit_2(
// CHECK-SAME: obelisk_sim.branching_sequence_alternatives = 3 : i64
// CHECK-SAME: obelisk_sim.sva_boolean_alternatives_after = 3 : i64
// CHECK-SAME: obelisk_sim.sva_boolean_alternatives_before = 4 : i64
// CHECK-SAME: obelisk_sim.sva_boolean_literals_after = 7 : i64
// CHECK-SAME: obelisk_sim.sva_boolean_literals_before = 11 : i64
// CHECK-SAME: obelisk_sim.sva_boolean_solver = "z3"
// CHECK-SAME: obelisk_sim.vacuous_sequence_alternatives = 2 : i64
// CHECK-COUNT-3: obelisk_sim.assert.sampled_read
// CHECK: obelisk_sim.logic.compare case_eq
// CHECK-NOT: obelisk_sim.assert.sampled_read
// CHECK-COUNT-1: obelisk_sim.spawn @unit_2.fork.71.0.0
// CHECK-NOT: obelisk_sim.spawn @unit_2.fork.71.0.0

// Nested false-vacuous results remain distinct until the outer iff is
// classified. The generated monitor still samples each semantic atom once and
// dispatches one cover-property action for the single source evaluation.
// CHECK-LABEL: obelisk_sim.func private @unit_3(
// CHECK-SAME: obelisk_sim.branching_sequence_alternatives = 12 : i64
// CHECK-SAME: obelisk_sim.sva_boolean_alternatives_after = 12 : i64
// CHECK-SAME: obelisk_sim.sva_boolean_alternatives_before = 34 : i64
// CHECK-SAME: obelisk_sim.sva_boolean_literals_after = 53 : i64
// CHECK-SAME: obelisk_sim.sva_boolean_literals_before = 204 : i64
// CHECK-SAME: obelisk_sim.sva_boolean_solver = "z3"
// CHECK-SAME: obelisk_sim.vacuous_sequence_alternatives = 5 : i64
// CHECK-COUNT-6: obelisk_sim.assert.sampled_read
// CHECK-NOT: obelisk_sim.assert.sampled_read
// CHECK-COUNT-1: obelisk_sim.spawn @unit_3.fork.101.0.0
// CHECK-NOT: obelisk_sim.spawn @unit_3.fork.101.0.0

// Nested not/or/and lowering preserves false-vacuous results and minimizes
// only within each successful classification before materializing the monitor.
// CHECK-LABEL: obelisk_sim.func private @unit_4(
// CHECK-SAME: obelisk_sim.branching_sequence_alternatives = 8 : i64
// CHECK-SAME: obelisk_sim.sva_boolean_alternatives_after = 8 : i64
// CHECK-SAME: obelisk_sim.sva_boolean_alternatives_before = 28 : i64
// CHECK-SAME: obelisk_sim.sva_boolean_literals_after = 25 : i64
// CHECK-SAME: obelisk_sim.sva_boolean_literals_before = 160 : i64
// CHECK-SAME: obelisk_sim.sva_boolean_solver = "z3"
// CHECK-SAME: obelisk_sim.vacuous_sequence_alternatives = 2 : i64
// CHECK-COUNT-6: obelisk_sim.assert.sampled_read
// CHECK-NOT: obelisk_sim.assert.sampled_read
// CHECK-COUNT-1: obelisk_sim.spawn @unit_4.fork.131.0.0
// CHECK-NOT: obelisk_sim.spawn @unit_4.fork.131.0.0

// The double-negated implication is truth-equivalent to the original and its
// false-antecedent success remains vacuous across both negations.
// CHECK-LABEL: obelisk_sim.func private @unit_5(
// CHECK-SAME: obelisk_sim.branching_sequence_alternatives = 2 : i64
// CHECK-SAME: obelisk_sim.sva_boolean_alternatives_after = 2 : i64
// CHECK-SAME: obelisk_sim.sva_boolean_alternatives_before = 3 : i64
// CHECK-SAME: obelisk_sim.sva_boolean_literals_after = 3 : i64
// CHECK-SAME: obelisk_sim.sva_boolean_literals_before = 6 : i64
// CHECK-SAME: obelisk_sim.sva_boolean_solver = "z3"
// CHECK-SAME: obelisk_sim.vacuous_sequence_alternatives = 1 : i64
// CHECK-COUNT-2: obelisk_sim.assert.sampled_read
// CHECK-NOT: obelisk_sim.assert.sampled_read
// CHECK-COUNT-1: obelisk_sim.spawn @unit_5.fork.161.0.0
// CHECK-NOT: obelisk_sim.spawn @unit_5.fork.161.0.0
// CHECK-NOT: obelisk.sv.assertion
