// RUN: obelisk-opt %s '--lower-obelisk-to-sim=opt-level=0' | FileCheck %s
// RUN: obelisk-opt %s '--lower-obelisk-to-sim=opt-level=3' -o /dev/null
// RUN: obelisk-opt %s '--lower-obelisk-to-sim=opt-level=3' '--encode-obelisk-sim-to-bytecode=vpi=off' -o /dev/null

// Deterministic multi-cycle antecedents carry initialized assertion locals
// through every live age. Ordered match assignments update that one attempt's
// values before an overlapped consequent or nonoverlapped handoff begins.

module attributes {llvm.data_layout = "e-m:e-p:64:64-i64:64-n8:16:32:64-S128", llvm.target_triple = "x86_64-unknown-linux-gnu"} {
  obelisk.sv.symbol.definition attributes {definition_kind = 0 : i32, hierarchical_name = "sva_local_multicycle_antecedent", name = "sva_local_multicycle_antecedent", node_id = 0 : i64, sym_name = "s0.sva_local_multicycle_antecedent"} {
  }
  obelisk.sv.symbol.root attributes {hierarchical_name = "\\$root ", name = "$root", node_id = 1 : i64, sym_name = "s1.$root"} {
    obelisk.sv.symbol.compilation_unit attributes {hierarchical_name = "$unit", node_id = 2 : i64, sym_name = "s2"} {
    }
    obelisk.sv.symbol.instance attributes {hierarchical_name = "sva_local_multicycle_antecedent", is_uninstantiated = false, name = "sva_local_multicycle_antecedent", node_id = 3 : i64, referenced_path = "sva_local_multicycle_antecedent", referenced_symbol = @s0.sva_local_multicycle_antecedent, sym_name = "s3.sva_local_multicycle_antecedent"} {
      obelisk.sv.symbol.instance_body attributes {hierarchical_name = "sva_local_multicycle_antecedent", name = "sva_local_multicycle_antecedent", node_id = 4 : i64, sym_name = "s4.sva_local_multicycle_antecedent", time_precision_fs = 1000000 : i64, time_unit_fs = 1000000 : i64} {
        obelisk.sv.symbol.port attributes {direction = 0 : i32, hierarchical_name = "sva_local_multicycle_antecedent.clk", name = "clk", node_id = 5 : i64, semantic_type = !obelisk.integral<1, false, true, 0 : 0, logic>, sym_name = "s5.clk"} {
        }
        obelisk.sv.symbol.variable attributes {hierarchical_name = "sva_local_multicycle_antecedent.clk", lifetime = 1 : i32, name = "clk", node_id = 6 : i64, semantic_type = !obelisk.integral<1, false, true, 0 : 0, logic>, sym_name = "s6.clk"} {
        }
        obelisk.sv.symbol.port attributes {direction = 0 : i32, hierarchical_name = "sva_local_multicycle_antecedent.a", name = "a", node_id = 7 : i64, semantic_type = !obelisk.integral<1, false, true, 0 : 0, logic>, sym_name = "s7.a"} {
        }
        obelisk.sv.symbol.variable attributes {hierarchical_name = "sva_local_multicycle_antecedent.a", lifetime = 1 : i32, name = "a", node_id = 8 : i64, semantic_type = !obelisk.integral<1, false, true, 0 : 0, logic>, sym_name = "s8.a"} {
        }
        obelisk.sv.symbol.port attributes {direction = 0 : i32, hierarchical_name = "sva_local_multicycle_antecedent.b", name = "b", node_id = 9 : i64, semantic_type = !obelisk.integral<1, false, true, 0 : 0, logic>, sym_name = "s9.b"} {
        }
        obelisk.sv.symbol.variable attributes {hierarchical_name = "sva_local_multicycle_antecedent.b", lifetime = 1 : i32, name = "b", node_id = 10 : i64, semantic_type = !obelisk.integral<1, false, true, 0 : 0, logic>, sym_name = "s10.b"} {
        }
        obelisk.sv.symbol.port attributes {direction = 0 : i32, hierarchical_name = "sva_local_multicycle_antecedent.c", name = "c", node_id = 11 : i64, semantic_type = !obelisk.integral<1, false, true, 0 : 0, logic>, sym_name = "s11.c"} {
        }
        obelisk.sv.symbol.variable attributes {hierarchical_name = "sva_local_multicycle_antecedent.c", lifetime = 1 : i32, name = "c", node_id = 12 : i64, semantic_type = !obelisk.integral<1, false, true, 0 : 0, logic>, sym_name = "s12.c"} {
        }
        obelisk.sv.symbol.port attributes {direction = 0 : i32, hierarchical_name = "sva_local_multicycle_antecedent.d", name = "d", node_id = 13 : i64, semantic_type = !obelisk.integral<1, false, true, 0 : 0, logic>, sym_name = "s13.d"} {
        }
        obelisk.sv.symbol.variable attributes {hierarchical_name = "sva_local_multicycle_antecedent.d", lifetime = 1 : i32, name = "d", node_id = 14 : i64, semantic_type = !obelisk.integral<1, false, true, 0 : 0, logic>, sym_name = "s14.d"} {
        }
        obelisk.sv.symbol.property attributes {has_default_instance = true, hierarchical_name = "sva_local_multicycle_antecedent.p_overlap", name = "p_overlap", node_id = 15 : i64, port_count = 0 : i64, port_paths = [], port_symbols = [], sym_name = "s15.p_overlap"} {
          obelisk.sv.expression.assertion_instance attributes {argument_count = 0 : i64, argument_formal_paths = [], argument_formal_symbols = [], argument_kinds = array<i64>, has_expanded_body = true, is_recursive_property = false, is_signed = false, local_variable_count = 1 : i64, local_variable_has_initializer = array<i64: 1>, local_variable_paths = ["sva_local_multicycle_antecedent.p_overlap.x"], local_variable_symbols = [@s1.$root::@s3.sva_local_multicycle_antecedent::@s4.sva_local_multicycle_antecedent::@s15.p_overlap::@s16.x], node_id = 16 : i64, referenced_path = "sva_local_multicycle_antecedent.p_overlap", referenced_symbol = @s1.$root::@s3.sva_local_multicycle_antecedent::@s4.sva_local_multicycle_antecedent::@s15.p_overlap, semantic_type = !obelisk.property} {
            obelisk.sv.assertion.clocking attributes {node_id = 17 : i64} {
              obelisk.sv.timing.signal_event attributes {edge_kind = 1 : i32, has_iff = false, node_id = 18 : i64} {
                obelisk.sv.expression.named_value attributes {is_signed = false, node_id = 19 : i64, referenced_path = "sva_local_multicycle_antecedent.clk", referenced_symbol = @s1.$root::@s3.sva_local_multicycle_antecedent::@s4.sva_local_multicycle_antecedent::@s6.clk, semantic_type = !obelisk.integral<1, false, true, 0 : 0, logic>} {
                }
              }
              obelisk.sv.assertion.binary attributes {node_id = 20 : i64, operator_kind = 11 : i32} {
                obelisk.sv.assertion.sequence_concat attributes {delays = [{is_unbounded = false, max = 0 : i64, min = 0 : i64}, {is_unbounded = false, max = 1 : i64, min = 1 : i64}], node_id = 21 : i64} {
                  obelisk.sv.assertion.sequence_with_match attributes {has_repetition = false, match_item_count = 1 : i64, node_id = 22 : i64, repetition_is_unbounded = false} {
                    obelisk.sv.assertion.simple attributes {has_repetition = false, is_null = false, node_id = 23 : i64, repetition_is_unbounded = false} {
                      obelisk.sv.expression.named_value attributes {is_signed = false, node_id = 24 : i64, referenced_path = "sva_local_multicycle_antecedent.a", referenced_symbol = @s1.$root::@s3.sva_local_multicycle_antecedent::@s4.sva_local_multicycle_antecedent::@s8.a, semantic_type = !obelisk.integral<1, false, true, 0 : 0, logic>} {
                      }
                    }
                    obelisk.sv.expression.assignment attributes {assignment_kind = 0 : i32, is_signed = false, node_id = 25 : i64, semantic_type = !obelisk.integral<1, false, true, 0 : 0, logic>} {
                      obelisk.sv.expression.named_value attributes {is_signed = false, node_id = 26 : i64, referenced_path = "sva_local_multicycle_antecedent.p_overlap.x", referenced_symbol = @s1.$root::@s3.sva_local_multicycle_antecedent::@s4.sva_local_multicycle_antecedent::@s15.p_overlap::@s16.x, semantic_type = !obelisk.integral<1, false, true, 0 : 0, logic>} {
                      }
                      obelisk.sv.expression.named_value attributes {is_signed = false, node_id = 27 : i64, referenced_path = "sva_local_multicycle_antecedent.b", referenced_symbol = @s1.$root::@s3.sva_local_multicycle_antecedent::@s4.sva_local_multicycle_antecedent::@s10.b, semantic_type = !obelisk.integral<1, false, true, 0 : 0, logic>} {
                      }
                    }
                  }
                  obelisk.sv.assertion.sequence_with_match attributes {has_repetition = false, match_item_count = 2 : i64, node_id = 28 : i64, repetition_is_unbounded = false} {
                    obelisk.sv.assertion.simple attributes {has_repetition = false, is_null = false, node_id = 29 : i64, repetition_is_unbounded = false} {
                      obelisk.sv.expression.named_value attributes {is_signed = false, node_id = 30 : i64, referenced_path = "sva_local_multicycle_antecedent.b", referenced_symbol = @s1.$root::@s3.sva_local_multicycle_antecedent::@s4.sva_local_multicycle_antecedent::@s10.b, semantic_type = !obelisk.integral<1, false, true, 0 : 0, logic>} {
                      }
                    }
                    obelisk.sv.expression.assignment attributes {assignment_kind = 0 : i32, is_signed = false, node_id = 31 : i64, semantic_type = !obelisk.integral<1, false, true, 0 : 0, logic>} {
                      obelisk.sv.expression.named_value attributes {is_signed = false, node_id = 32 : i64, referenced_path = "sva_local_multicycle_antecedent.p_overlap.x", referenced_symbol = @s1.$root::@s3.sva_local_multicycle_antecedent::@s4.sva_local_multicycle_antecedent::@s15.p_overlap::@s16.x, semantic_type = !obelisk.integral<1, false, true, 0 : 0, logic>} {
                      }
                      obelisk.sv.expression.unary_op attributes {is_signed = false, node_id = 33 : i64, operator_kind = 9 : i32, semantic_type = !obelisk.integral<1, false, true, 0 : 0, logic>} {
                        obelisk.sv.expression.named_value attributes {is_signed = false, node_id = 34 : i64, referenced_path = "sva_local_multicycle_antecedent.p_overlap.x", referenced_symbol = @s1.$root::@s3.sva_local_multicycle_antecedent::@s4.sva_local_multicycle_antecedent::@s15.p_overlap::@s16.x, semantic_type = !obelisk.integral<1, false, true, 0 : 0, logic>} {
                        }
                      }
                    }
                    obelisk.sv.expression.call attributes {argument_count = 2 : i64, callee_name = "$display", constraint_restrictions = [], defaulted_arguments = array<i64: 0, 0>, has_inline_constraints = false, has_iterator_expression = false, has_output_arguments = false, has_this_class = false, is_signed = false, is_super_class = false, is_system_call = true, node_id = 35 : i64, semantic_type = !obelisk.void, subroutine_kind = 1 : i32, system_library_cell = "work.sva_local_multicycle_antecedent", system_scope_path = "sva_local_multicycle_antecedent.p_overlap", system_scope_symbol = @s1.$root::@s3.sva_local_multicycle_antecedent::@s4.sva_local_multicycle_antecedent::@s15.p_overlap} {
                      obelisk.sv.expression.string_literal attributes {constant_value = "x=%0b", is_signed = false, node_id = 36 : i64, semantic_type = !obelisk.ranged_packed_array<39 : 0 x !obelisk.integral<1, false, false, 0 : 0, bit>>} {
                      }
                      obelisk.sv.expression.named_value attributes {is_signed = false, node_id = 37 : i64, referenced_path = "sva_local_multicycle_antecedent.p_overlap.x", referenced_symbol = @s1.$root::@s3.sva_local_multicycle_antecedent::@s4.sva_local_multicycle_antecedent::@s15.p_overlap::@s16.x, semantic_type = !obelisk.integral<1, false, true, 0 : 0, logic>} {
                      }
                    }
                  }
                }
                obelisk.sv.assertion.sequence_concat attributes {delays = [{is_unbounded = false, max = 0 : i64, min = 0 : i64}, {is_unbounded = false, max = 1 : i64, min = 1 : i64}], node_id = 38 : i64} {
                  obelisk.sv.assertion.simple attributes {has_repetition = false, is_null = false, node_id = 39 : i64, repetition_is_unbounded = false} {
                    obelisk.sv.expression.binary_op attributes {is_signed = false, node_id = 40 : i64, operator_kind = 9 : i32, semantic_type = !obelisk.integral<1, false, true, 0 : 0, logic>} {
                      obelisk.sv.expression.named_value attributes {is_signed = false, node_id = 41 : i64, referenced_path = "sva_local_multicycle_antecedent.c", referenced_symbol = @s1.$root::@s3.sva_local_multicycle_antecedent::@s4.sva_local_multicycle_antecedent::@s12.c, semantic_type = !obelisk.integral<1, false, true, 0 : 0, logic>} {
                      }
                      obelisk.sv.expression.named_value attributes {is_signed = false, node_id = 42 : i64, referenced_path = "sva_local_multicycle_antecedent.p_overlap.x", referenced_symbol = @s1.$root::@s3.sva_local_multicycle_antecedent::@s4.sva_local_multicycle_antecedent::@s15.p_overlap::@s16.x, semantic_type = !obelisk.integral<1, false, true, 0 : 0, logic>} {
                      }
                    }
                  }
                  obelisk.sv.assertion.simple attributes {has_repetition = false, is_null = false, node_id = 43 : i64, repetition_is_unbounded = false} {
                    obelisk.sv.expression.named_value attributes {is_signed = false, node_id = 44 : i64, referenced_path = "sva_local_multicycle_antecedent.d", referenced_symbol = @s1.$root::@s3.sva_local_multicycle_antecedent::@s4.sva_local_multicycle_antecedent::@s14.d, semantic_type = !obelisk.integral<1, false, true, 0 : 0, logic>} {
                    }
                  }
                }
              }
            }
            obelisk.sv.expression.named_value attributes {is_signed = false, node_id = 45 : i64, referenced_path = "sva_local_multicycle_antecedent.a", referenced_symbol = @s1.$root::@s3.sva_local_multicycle_antecedent::@s4.sva_local_multicycle_antecedent::@s8.a, semantic_type = !obelisk.integral<1, false, true, 0 : 0, logic>} {
            }
          }
          obelisk.sv.symbol.local_assertion_var attributes {hierarchical_name = "sva_local_multicycle_antecedent.p_overlap.x", name = "x", node_id = 180 : i64, semantic_type = !obelisk.integral<1, false, true, 0 : 0, logic>, sym_name = "s16.x"} {
            obelisk.sv.expression.named_value attributes {is_signed = false, node_id = 181 : i64, referenced_path = "sva_local_multicycle_antecedent.a", referenced_symbol = @s1.$root::@s3.sva_local_multicycle_antecedent::@s4.sva_local_multicycle_antecedent::@s8.a, semantic_type = !obelisk.integral<1, false, true, 0 : 0, logic>} {
            }
          }
          obelisk.sv.symbol.local_assertion_var attributes {hierarchical_name = "sva_local_multicycle_antecedent.p_overlap.x", name = "x", node_id = 182 : i64, semantic_type = !obelisk.integral<1, false, true, 0 : 0, logic>, sym_name = "s20.x"} {
            obelisk.sv.expression.named_value attributes {is_signed = false, node_id = 183 : i64, referenced_path = "sva_local_multicycle_antecedent.a", referenced_symbol = @s1.$root::@s3.sva_local_multicycle_antecedent::@s4.sva_local_multicycle_antecedent::@s8.a, semantic_type = !obelisk.integral<1, false, true, 0 : 0, logic>} {
            }
          }
        }
        obelisk.sv.symbol.property attributes {has_default_instance = true, hierarchical_name = "sva_local_multicycle_antecedent.p_nonoverlap", name = "p_nonoverlap", node_id = 46 : i64, port_count = 0 : i64, port_paths = [], port_symbols = [], sym_name = "s17.p_nonoverlap"} {
          obelisk.sv.expression.assertion_instance attributes {argument_count = 0 : i64, argument_formal_paths = [], argument_formal_symbols = [], argument_kinds = array<i64>, has_expanded_body = true, is_recursive_property = false, is_signed = false, local_variable_count = 1 : i64, local_variable_has_initializer = array<i64: 1>, local_variable_paths = ["sva_local_multicycle_antecedent.p_nonoverlap.x"], local_variable_symbols = [@s1.$root::@s3.sva_local_multicycle_antecedent::@s4.sva_local_multicycle_antecedent::@s17.p_nonoverlap::@s18.x], node_id = 47 : i64, referenced_path = "sva_local_multicycle_antecedent.p_nonoverlap", referenced_symbol = @s1.$root::@s3.sva_local_multicycle_antecedent::@s4.sva_local_multicycle_antecedent::@s17.p_nonoverlap, semantic_type = !obelisk.property} {
            obelisk.sv.assertion.clocking attributes {node_id = 48 : i64} {
              obelisk.sv.timing.signal_event attributes {edge_kind = 1 : i32, has_iff = false, node_id = 49 : i64} {
                obelisk.sv.expression.named_value attributes {is_signed = false, node_id = 50 : i64, referenced_path = "sva_local_multicycle_antecedent.clk", referenced_symbol = @s1.$root::@s3.sva_local_multicycle_antecedent::@s4.sva_local_multicycle_antecedent::@s6.clk, semantic_type = !obelisk.integral<1, false, true, 0 : 0, logic>} {
                }
              }
              obelisk.sv.assertion.binary attributes {node_id = 51 : i64, operator_kind = 14 : i32} {
                obelisk.sv.assertion.sequence_concat attributes {delays = [{is_unbounded = false, max = 0 : i64, min = 0 : i64}, {is_unbounded = false, max = 2 : i64, min = 2 : i64}], node_id = 52 : i64} {
                  obelisk.sv.assertion.sequence_with_match attributes {has_repetition = false, match_item_count = 1 : i64, node_id = 53 : i64, repetition_is_unbounded = false} {
                    obelisk.sv.assertion.simple attributes {has_repetition = false, is_null = false, node_id = 54 : i64, repetition_is_unbounded = false} {
                      obelisk.sv.expression.named_value attributes {is_signed = false, node_id = 55 : i64, referenced_path = "sva_local_multicycle_antecedent.a", referenced_symbol = @s1.$root::@s3.sva_local_multicycle_antecedent::@s4.sva_local_multicycle_antecedent::@s8.a, semantic_type = !obelisk.integral<1, false, true, 0 : 0, logic>} {
                      }
                    }
                    obelisk.sv.expression.assignment attributes {assignment_kind = 0 : i32, is_signed = false, node_id = 56 : i64, semantic_type = !obelisk.integral<1, false, true, 0 : 0, logic>} {
                      obelisk.sv.expression.named_value attributes {is_signed = false, node_id = 57 : i64, referenced_path = "sva_local_multicycle_antecedent.p_nonoverlap.x", referenced_symbol = @s1.$root::@s3.sva_local_multicycle_antecedent::@s4.sva_local_multicycle_antecedent::@s17.p_nonoverlap::@s18.x, semantic_type = !obelisk.integral<1, false, true, 0 : 0, logic>} {
                      }
                      obelisk.sv.expression.named_value attributes {is_signed = false, node_id = 58 : i64, referenced_path = "sva_local_multicycle_antecedent.b", referenced_symbol = @s1.$root::@s3.sva_local_multicycle_antecedent::@s4.sva_local_multicycle_antecedent::@s10.b, semantic_type = !obelisk.integral<1, false, true, 0 : 0, logic>} {
                      }
                    }
                  }
                  obelisk.sv.assertion.sequence_with_match attributes {has_repetition = false, match_item_count = 1 : i64, node_id = 59 : i64, repetition_is_unbounded = false} {
                    obelisk.sv.assertion.simple attributes {has_repetition = false, is_null = false, node_id = 60 : i64, repetition_is_unbounded = false} {
                      obelisk.sv.expression.named_value attributes {is_signed = false, node_id = 61 : i64, referenced_path = "sva_local_multicycle_antecedent.b", referenced_symbol = @s1.$root::@s3.sva_local_multicycle_antecedent::@s4.sva_local_multicycle_antecedent::@s10.b, semantic_type = !obelisk.integral<1, false, true, 0 : 0, logic>} {
                      }
                    }
                    obelisk.sv.expression.assignment attributes {assignment_kind = 0 : i32, is_signed = false, node_id = 62 : i64, semantic_type = !obelisk.integral<1, false, true, 0 : 0, logic>} {
                      obelisk.sv.expression.named_value attributes {is_signed = false, node_id = 63 : i64, referenced_path = "sva_local_multicycle_antecedent.p_nonoverlap.x", referenced_symbol = @s1.$root::@s3.sva_local_multicycle_antecedent::@s4.sva_local_multicycle_antecedent::@s17.p_nonoverlap::@s18.x, semantic_type = !obelisk.integral<1, false, true, 0 : 0, logic>} {
                      }
                      obelisk.sv.expression.unary_op attributes {is_signed = false, node_id = 64 : i64, operator_kind = 9 : i32, semantic_type = !obelisk.integral<1, false, true, 0 : 0, logic>} {
                        obelisk.sv.expression.named_value attributes {is_signed = false, node_id = 65 : i64, referenced_path = "sva_local_multicycle_antecedent.p_nonoverlap.x", referenced_symbol = @s1.$root::@s3.sva_local_multicycle_antecedent::@s4.sva_local_multicycle_antecedent::@s17.p_nonoverlap::@s18.x, semantic_type = !obelisk.integral<1, false, true, 0 : 0, logic>} {
                        }
                      }
                    }
                  }
                }
                obelisk.sv.assertion.sequence_concat attributes {delays = [{is_unbounded = false, max = 0 : i64, min = 0 : i64}, {is_unbounded = false, max = 1 : i64, min = 1 : i64}], node_id = 66 : i64} {
                  obelisk.sv.assertion.simple attributes {has_repetition = false, is_null = false, node_id = 67 : i64, repetition_is_unbounded = false} {
                    obelisk.sv.expression.binary_op attributes {is_signed = false, node_id = 68 : i64, operator_kind = 9 : i32, semantic_type = !obelisk.integral<1, false, true, 0 : 0, logic>} {
                      obelisk.sv.expression.named_value attributes {is_signed = false, node_id = 69 : i64, referenced_path = "sva_local_multicycle_antecedent.c", referenced_symbol = @s1.$root::@s3.sva_local_multicycle_antecedent::@s4.sva_local_multicycle_antecedent::@s12.c, semantic_type = !obelisk.integral<1, false, true, 0 : 0, logic>} {
                      }
                      obelisk.sv.expression.named_value attributes {is_signed = false, node_id = 70 : i64, referenced_path = "sva_local_multicycle_antecedent.p_nonoverlap.x", referenced_symbol = @s1.$root::@s3.sva_local_multicycle_antecedent::@s4.sva_local_multicycle_antecedent::@s17.p_nonoverlap::@s18.x, semantic_type = !obelisk.integral<1, false, true, 0 : 0, logic>} {
                      }
                    }
                  }
                  obelisk.sv.assertion.simple attributes {has_repetition = false, is_null = false, node_id = 71 : i64, repetition_is_unbounded = false} {
                    obelisk.sv.expression.named_value attributes {is_signed = false, node_id = 72 : i64, referenced_path = "sva_local_multicycle_antecedent.d", referenced_symbol = @s1.$root::@s3.sva_local_multicycle_antecedent::@s4.sva_local_multicycle_antecedent::@s14.d, semantic_type = !obelisk.integral<1, false, true, 0 : 0, logic>} {
                    }
                  }
                }
              }
            }
            obelisk.sv.expression.named_value attributes {is_signed = false, node_id = 73 : i64, referenced_path = "sva_local_multicycle_antecedent.a", referenced_symbol = @s1.$root::@s3.sva_local_multicycle_antecedent::@s4.sva_local_multicycle_antecedent::@s8.a, semantic_type = !obelisk.integral<1, false, true, 0 : 0, logic>} {
            }
          }
          obelisk.sv.symbol.local_assertion_var attributes {hierarchical_name = "sva_local_multicycle_antecedent.p_nonoverlap.x", name = "x", node_id = 174 : i64, semantic_type = !obelisk.integral<1, false, true, 0 : 0, logic>, sym_name = "s18.x"} {
            obelisk.sv.expression.named_value attributes {is_signed = false, node_id = 175 : i64, referenced_path = "sva_local_multicycle_antecedent.a", referenced_symbol = @s1.$root::@s3.sva_local_multicycle_antecedent::@s4.sva_local_multicycle_antecedent::@s8.a, semantic_type = !obelisk.integral<1, false, true, 0 : 0, logic>} {
            }
          }
          obelisk.sv.symbol.local_assertion_var attributes {hierarchical_name = "sva_local_multicycle_antecedent.p_nonoverlap.x", name = "x", node_id = 176 : i64, semantic_type = !obelisk.integral<1, false, true, 0 : 0, logic>, sym_name = "s22.x"} {
            obelisk.sv.expression.named_value attributes {is_signed = false, node_id = 177 : i64, referenced_path = "sva_local_multicycle_antecedent.a", referenced_symbol = @s1.$root::@s3.sva_local_multicycle_antecedent::@s4.sva_local_multicycle_antecedent::@s8.a, semantic_type = !obelisk.integral<1, false, true, 0 : 0, logic>} {
            }
          }
          obelisk.sv.symbol.local_assertion_var attributes {hierarchical_name = "sva_local_multicycle_antecedent.p_nonoverlap.x", name = "x", node_id = 178 : i64, semantic_type = !obelisk.integral<1, false, true, 0 : 0, logic>, sym_name = "s24.x"} {
            obelisk.sv.expression.named_value attributes {is_signed = false, node_id = 179 : i64, referenced_path = "sva_local_multicycle_antecedent.a", referenced_symbol = @s1.$root::@s3.sva_local_multicycle_antecedent::@s4.sva_local_multicycle_antecedent::@s8.a, semantic_type = !obelisk.integral<1, false, true, 0 : 0, logic>} {
            }
          }
        }
        obelisk.sv.symbol.procedural_block attributes {hierarchical_name = "sva_local_multicycle_antecedent", node_id = 74 : i64, procedure_kind = 2 : i32, sym_name = "s19", time_precision_fs = 1000000 : i64, time_unit_fs = 1000000 : i64} {
          obelisk.sv.statement.concurrent_assertion attributes {assertion_kind = 0 : i32, has_default_disable = false, has_fail_action = false, has_pass_action = true, node_id = 75 : i64} {
            obelisk.sv.assertion.simple attributes {has_repetition = false, is_null = false, node_id = 76 : i64, repetition_is_unbounded = false} {
              obelisk.sv.expression.assertion_instance attributes {argument_count = 0 : i64, argument_formal_paths = [], argument_formal_symbols = [], argument_kinds = array<i64>, has_expanded_body = true, is_recursive_property = false, is_signed = false, local_variable_count = 1 : i64, local_variable_has_initializer = array<i64: 1>, local_variable_paths = ["sva_local_multicycle_antecedent.p_overlap.x"], local_variable_symbols = [@s1.$root::@s3.sva_local_multicycle_antecedent::@s4.sva_local_multicycle_antecedent::@s15.p_overlap::@s20.x], node_id = 77 : i64, referenced_path = "sva_local_multicycle_antecedent.p_overlap", referenced_symbol = @s1.$root::@s3.sva_local_multicycle_antecedent::@s4.sva_local_multicycle_antecedent::@s15.p_overlap, semantic_type = !obelisk.property} {
                obelisk.sv.assertion.clocking attributes {node_id = 78 : i64} {
                  obelisk.sv.timing.signal_event attributes {edge_kind = 1 : i32, has_iff = false, node_id = 79 : i64} {
                    obelisk.sv.expression.named_value attributes {is_signed = false, node_id = 80 : i64, referenced_path = "sva_local_multicycle_antecedent.clk", referenced_symbol = @s1.$root::@s3.sva_local_multicycle_antecedent::@s4.sva_local_multicycle_antecedent::@s6.clk, semantic_type = !obelisk.integral<1, false, true, 0 : 0, logic>} {
                    }
                  }
                  obelisk.sv.assertion.binary attributes {node_id = 81 : i64, operator_kind = 11 : i32} {
                    obelisk.sv.assertion.sequence_concat attributes {delays = [{is_unbounded = false, max = 0 : i64, min = 0 : i64}, {is_unbounded = false, max = 1 : i64, min = 1 : i64}], node_id = 82 : i64} {
                      obelisk.sv.assertion.sequence_with_match attributes {has_repetition = false, match_item_count = 1 : i64, node_id = 83 : i64, repetition_is_unbounded = false} {
                        obelisk.sv.assertion.simple attributes {has_repetition = false, is_null = false, node_id = 84 : i64, repetition_is_unbounded = false} {
                          obelisk.sv.expression.named_value attributes {is_signed = false, node_id = 85 : i64, referenced_path = "sva_local_multicycle_antecedent.a", referenced_symbol = @s1.$root::@s3.sva_local_multicycle_antecedent::@s4.sva_local_multicycle_antecedent::@s8.a, semantic_type = !obelisk.integral<1, false, true, 0 : 0, logic>} {
                          }
                        }
                        obelisk.sv.expression.assignment attributes {assignment_kind = 0 : i32, is_signed = false, node_id = 86 : i64, semantic_type = !obelisk.integral<1, false, true, 0 : 0, logic>} {
                          obelisk.sv.expression.named_value attributes {is_signed = false, node_id = 87 : i64, referenced_path = "sva_local_multicycle_antecedent.p_overlap.x", referenced_symbol = @s1.$root::@s3.sva_local_multicycle_antecedent::@s4.sva_local_multicycle_antecedent::@s15.p_overlap::@s20.x, semantic_type = !obelisk.integral<1, false, true, 0 : 0, logic>} {
                          }
                          obelisk.sv.expression.named_value attributes {is_signed = false, node_id = 88 : i64, referenced_path = "sva_local_multicycle_antecedent.b", referenced_symbol = @s1.$root::@s3.sva_local_multicycle_antecedent::@s4.sva_local_multicycle_antecedent::@s10.b, semantic_type = !obelisk.integral<1, false, true, 0 : 0, logic>} {
                          }
                        }
                      }
                      obelisk.sv.assertion.sequence_with_match attributes {has_repetition = false, match_item_count = 2 : i64, node_id = 89 : i64, repetition_is_unbounded = false} {
                        obelisk.sv.assertion.simple attributes {has_repetition = false, is_null = false, node_id = 90 : i64, repetition_is_unbounded = false} {
                          obelisk.sv.expression.named_value attributes {is_signed = false, node_id = 91 : i64, referenced_path = "sva_local_multicycle_antecedent.b", referenced_symbol = @s1.$root::@s3.sva_local_multicycle_antecedent::@s4.sva_local_multicycle_antecedent::@s10.b, semantic_type = !obelisk.integral<1, false, true, 0 : 0, logic>} {
                          }
                        }
                        obelisk.sv.expression.assignment attributes {assignment_kind = 0 : i32, is_signed = false, node_id = 92 : i64, semantic_type = !obelisk.integral<1, false, true, 0 : 0, logic>} {
                          obelisk.sv.expression.named_value attributes {is_signed = false, node_id = 93 : i64, referenced_path = "sva_local_multicycle_antecedent.p_overlap.x", referenced_symbol = @s1.$root::@s3.sva_local_multicycle_antecedent::@s4.sva_local_multicycle_antecedent::@s15.p_overlap::@s20.x, semantic_type = !obelisk.integral<1, false, true, 0 : 0, logic>} {
                          }
                          obelisk.sv.expression.unary_op attributes {is_signed = false, node_id = 94 : i64, operator_kind = 9 : i32, semantic_type = !obelisk.integral<1, false, true, 0 : 0, logic>} {
                            obelisk.sv.expression.named_value attributes {is_signed = false, node_id = 95 : i64, referenced_path = "sva_local_multicycle_antecedent.p_overlap.x", referenced_symbol = @s1.$root::@s3.sva_local_multicycle_antecedent::@s4.sva_local_multicycle_antecedent::@s15.p_overlap::@s20.x, semantic_type = !obelisk.integral<1, false, true, 0 : 0, logic>} {
                            }
                          }
                        }
                        obelisk.sv.expression.call attributes {argument_count = 2 : i64, callee_name = "$display", constraint_restrictions = [], defaulted_arguments = array<i64: 0, 0>, has_inline_constraints = false, has_iterator_expression = false, has_output_arguments = false, has_this_class = false, is_signed = false, is_super_class = false, is_system_call = true, node_id = 96 : i64, semantic_type = !obelisk.void, subroutine_kind = 1 : i32, system_library_cell = "work.sva_local_multicycle_antecedent", system_scope_path = "sva_local_multicycle_antecedent.p_overlap", system_scope_symbol = @s1.$root::@s3.sva_local_multicycle_antecedent::@s4.sva_local_multicycle_antecedent::@s15.p_overlap} {
                          obelisk.sv.expression.string_literal attributes {constant_value = "x=%0b", is_signed = false, node_id = 97 : i64, semantic_type = !obelisk.ranged_packed_array<39 : 0 x !obelisk.integral<1, false, false, 0 : 0, bit>>} {
                          }
                          obelisk.sv.expression.named_value attributes {is_signed = false, node_id = 98 : i64, referenced_path = "sva_local_multicycle_antecedent.p_overlap.x", referenced_symbol = @s1.$root::@s3.sva_local_multicycle_antecedent::@s4.sva_local_multicycle_antecedent::@s15.p_overlap::@s20.x, semantic_type = !obelisk.integral<1, false, true, 0 : 0, logic>} {
                          }
                        }
                      }
                    }
                    obelisk.sv.assertion.sequence_concat attributes {delays = [{is_unbounded = false, max = 0 : i64, min = 0 : i64}, {is_unbounded = false, max = 1 : i64, min = 1 : i64}], node_id = 99 : i64} {
                      obelisk.sv.assertion.simple attributes {has_repetition = false, is_null = false, node_id = 100 : i64, repetition_is_unbounded = false} {
                        obelisk.sv.expression.binary_op attributes {is_signed = false, node_id = 101 : i64, operator_kind = 9 : i32, semantic_type = !obelisk.integral<1, false, true, 0 : 0, logic>} {
                          obelisk.sv.expression.named_value attributes {is_signed = false, node_id = 102 : i64, referenced_path = "sva_local_multicycle_antecedent.c", referenced_symbol = @s1.$root::@s3.sva_local_multicycle_antecedent::@s4.sva_local_multicycle_antecedent::@s12.c, semantic_type = !obelisk.integral<1, false, true, 0 : 0, logic>} {
                          }
                          obelisk.sv.expression.named_value attributes {is_signed = false, node_id = 103 : i64, referenced_path = "sva_local_multicycle_antecedent.p_overlap.x", referenced_symbol = @s1.$root::@s3.sva_local_multicycle_antecedent::@s4.sva_local_multicycle_antecedent::@s15.p_overlap::@s20.x, semantic_type = !obelisk.integral<1, false, true, 0 : 0, logic>} {
                          }
                        }
                      }
                      obelisk.sv.assertion.simple attributes {has_repetition = false, is_null = false, node_id = 104 : i64, repetition_is_unbounded = false} {
                        obelisk.sv.expression.named_value attributes {is_signed = false, node_id = 105 : i64, referenced_path = "sva_local_multicycle_antecedent.d", referenced_symbol = @s1.$root::@s3.sva_local_multicycle_antecedent::@s4.sva_local_multicycle_antecedent::@s14.d, semantic_type = !obelisk.integral<1, false, true, 0 : 0, logic>} {
                        }
                      }
                    }
                  }
                }
                obelisk.sv.expression.named_value attributes {is_signed = false, node_id = 106 : i64, referenced_path = "sva_local_multicycle_antecedent.a", referenced_symbol = @s1.$root::@s3.sva_local_multicycle_antecedent::@s4.sva_local_multicycle_antecedent::@s8.a, semantic_type = !obelisk.integral<1, false, true, 0 : 0, logic>} {
                }
              }
            }
            obelisk.sv.statement.expression_statement attributes {node_id = 107 : i64} {
              obelisk.sv.expression.call attributes {argument_count = 1 : i64, callee_name = "$display", constraint_restrictions = [], defaulted_arguments = array<i64: 0>, has_inline_constraints = false, has_iterator_expression = false, has_output_arguments = false, has_this_class = false, is_signed = false, is_super_class = false, is_system_call = true, node_id = 108 : i64, semantic_type = !obelisk.void, subroutine_kind = 1 : i32, system_library_cell = "work.sva_local_multicycle_antecedent", system_scope_path = "sva_local_multicycle_antecedent", system_scope_symbol = @s1.$root::@s3.sva_local_multicycle_antecedent::@s4.sva_local_multicycle_antecedent} {
                obelisk.sv.expression.string_literal attributes {constant_value = "overlap pass", is_signed = false, node_id = 109 : i64, semantic_type = !obelisk.ranged_packed_array<95 : 0 x !obelisk.integral<1, false, false, 0 : 0, bit>>} {
                }
              }
            }
          }
        }
        obelisk.sv.symbol.procedural_block attributes {hierarchical_name = "sva_local_multicycle_antecedent", node_id = 110 : i64, procedure_kind = 2 : i32, sym_name = "s21", time_precision_fs = 1000000 : i64, time_unit_fs = 1000000 : i64} {
          obelisk.sv.statement.concurrent_assertion attributes {assertion_kind = 2 : i32, has_default_disable = false, has_fail_action = false, has_pass_action = true, node_id = 111 : i64} {
            obelisk.sv.assertion.simple attributes {has_repetition = false, is_null = false, node_id = 112 : i64, repetition_is_unbounded = false} {
              obelisk.sv.expression.assertion_instance attributes {argument_count = 0 : i64, argument_formal_paths = [], argument_formal_symbols = [], argument_kinds = array<i64>, has_expanded_body = true, is_recursive_property = false, is_signed = false, local_variable_count = 1 : i64, local_variable_has_initializer = array<i64: 1>, local_variable_paths = ["sva_local_multicycle_antecedent.p_nonoverlap.x"], local_variable_symbols = [@s1.$root::@s3.sva_local_multicycle_antecedent::@s4.sva_local_multicycle_antecedent::@s17.p_nonoverlap::@s22.x], node_id = 113 : i64, referenced_path = "sva_local_multicycle_antecedent.p_nonoverlap", referenced_symbol = @s1.$root::@s3.sva_local_multicycle_antecedent::@s4.sva_local_multicycle_antecedent::@s17.p_nonoverlap, semantic_type = !obelisk.property} {
                obelisk.sv.assertion.clocking attributes {node_id = 114 : i64} {
                  obelisk.sv.timing.signal_event attributes {edge_kind = 1 : i32, has_iff = false, node_id = 115 : i64} {
                    obelisk.sv.expression.named_value attributes {is_signed = false, node_id = 116 : i64, referenced_path = "sva_local_multicycle_antecedent.clk", referenced_symbol = @s1.$root::@s3.sva_local_multicycle_antecedent::@s4.sva_local_multicycle_antecedent::@s6.clk, semantic_type = !obelisk.integral<1, false, true, 0 : 0, logic>} {
                    }
                  }
                  obelisk.sv.assertion.binary attributes {node_id = 117 : i64, operator_kind = 14 : i32} {
                    obelisk.sv.assertion.sequence_concat attributes {delays = [{is_unbounded = false, max = 0 : i64, min = 0 : i64}, {is_unbounded = false, max = 2 : i64, min = 2 : i64}], node_id = 118 : i64} {
                      obelisk.sv.assertion.sequence_with_match attributes {has_repetition = false, match_item_count = 1 : i64, node_id = 119 : i64, repetition_is_unbounded = false} {
                        obelisk.sv.assertion.simple attributes {has_repetition = false, is_null = false, node_id = 120 : i64, repetition_is_unbounded = false} {
                          obelisk.sv.expression.named_value attributes {is_signed = false, node_id = 121 : i64, referenced_path = "sva_local_multicycle_antecedent.a", referenced_symbol = @s1.$root::@s3.sva_local_multicycle_antecedent::@s4.sva_local_multicycle_antecedent::@s8.a, semantic_type = !obelisk.integral<1, false, true, 0 : 0, logic>} {
                          }
                        }
                        obelisk.sv.expression.assignment attributes {assignment_kind = 0 : i32, is_signed = false, node_id = 122 : i64, semantic_type = !obelisk.integral<1, false, true, 0 : 0, logic>} {
                          obelisk.sv.expression.named_value attributes {is_signed = false, node_id = 123 : i64, referenced_path = "sva_local_multicycle_antecedent.p_nonoverlap.x", referenced_symbol = @s1.$root::@s3.sva_local_multicycle_antecedent::@s4.sva_local_multicycle_antecedent::@s17.p_nonoverlap::@s22.x, semantic_type = !obelisk.integral<1, false, true, 0 : 0, logic>} {
                          }
                          obelisk.sv.expression.named_value attributes {is_signed = false, node_id = 124 : i64, referenced_path = "sva_local_multicycle_antecedent.b", referenced_symbol = @s1.$root::@s3.sva_local_multicycle_antecedent::@s4.sva_local_multicycle_antecedent::@s10.b, semantic_type = !obelisk.integral<1, false, true, 0 : 0, logic>} {
                          }
                        }
                      }
                      obelisk.sv.assertion.sequence_with_match attributes {has_repetition = false, match_item_count = 1 : i64, node_id = 125 : i64, repetition_is_unbounded = false} {
                        obelisk.sv.assertion.simple attributes {has_repetition = false, is_null = false, node_id = 126 : i64, repetition_is_unbounded = false} {
                          obelisk.sv.expression.named_value attributes {is_signed = false, node_id = 127 : i64, referenced_path = "sva_local_multicycle_antecedent.b", referenced_symbol = @s1.$root::@s3.sva_local_multicycle_antecedent::@s4.sva_local_multicycle_antecedent::@s10.b, semantic_type = !obelisk.integral<1, false, true, 0 : 0, logic>} {
                          }
                        }
                        obelisk.sv.expression.assignment attributes {assignment_kind = 0 : i32, is_signed = false, node_id = 128 : i64, semantic_type = !obelisk.integral<1, false, true, 0 : 0, logic>} {
                          obelisk.sv.expression.named_value attributes {is_signed = false, node_id = 129 : i64, referenced_path = "sva_local_multicycle_antecedent.p_nonoverlap.x", referenced_symbol = @s1.$root::@s3.sva_local_multicycle_antecedent::@s4.sva_local_multicycle_antecedent::@s17.p_nonoverlap::@s22.x, semantic_type = !obelisk.integral<1, false, true, 0 : 0, logic>} {
                          }
                          obelisk.sv.expression.unary_op attributes {is_signed = false, node_id = 130 : i64, operator_kind = 9 : i32, semantic_type = !obelisk.integral<1, false, true, 0 : 0, logic>} {
                            obelisk.sv.expression.named_value attributes {is_signed = false, node_id = 131 : i64, referenced_path = "sva_local_multicycle_antecedent.p_nonoverlap.x", referenced_symbol = @s1.$root::@s3.sva_local_multicycle_antecedent::@s4.sva_local_multicycle_antecedent::@s17.p_nonoverlap::@s22.x, semantic_type = !obelisk.integral<1, false, true, 0 : 0, logic>} {
                            }
                          }
                        }
                      }
                    }
                    obelisk.sv.assertion.sequence_concat attributes {delays = [{is_unbounded = false, max = 0 : i64, min = 0 : i64}, {is_unbounded = false, max = 1 : i64, min = 1 : i64}], node_id = 132 : i64} {
                      obelisk.sv.assertion.simple attributes {has_repetition = false, is_null = false, node_id = 133 : i64, repetition_is_unbounded = false} {
                        obelisk.sv.expression.binary_op attributes {is_signed = false, node_id = 134 : i64, operator_kind = 9 : i32, semantic_type = !obelisk.integral<1, false, true, 0 : 0, logic>} {
                          obelisk.sv.expression.named_value attributes {is_signed = false, node_id = 135 : i64, referenced_path = "sva_local_multicycle_antecedent.c", referenced_symbol = @s1.$root::@s3.sva_local_multicycle_antecedent::@s4.sva_local_multicycle_antecedent::@s12.c, semantic_type = !obelisk.integral<1, false, true, 0 : 0, logic>} {
                          }
                          obelisk.sv.expression.named_value attributes {is_signed = false, node_id = 136 : i64, referenced_path = "sva_local_multicycle_antecedent.p_nonoverlap.x", referenced_symbol = @s1.$root::@s3.sva_local_multicycle_antecedent::@s4.sva_local_multicycle_antecedent::@s17.p_nonoverlap::@s22.x, semantic_type = !obelisk.integral<1, false, true, 0 : 0, logic>} {
                          }
                        }
                      }
                      obelisk.sv.assertion.simple attributes {has_repetition = false, is_null = false, node_id = 137 : i64, repetition_is_unbounded = false} {
                        obelisk.sv.expression.named_value attributes {is_signed = false, node_id = 138 : i64, referenced_path = "sva_local_multicycle_antecedent.d", referenced_symbol = @s1.$root::@s3.sva_local_multicycle_antecedent::@s4.sva_local_multicycle_antecedent::@s14.d, semantic_type = !obelisk.integral<1, false, true, 0 : 0, logic>} {
                        }
                      }
                    }
                  }
                }
                obelisk.sv.expression.named_value attributes {is_signed = false, node_id = 139 : i64, referenced_path = "sva_local_multicycle_antecedent.a", referenced_symbol = @s1.$root::@s3.sva_local_multicycle_antecedent::@s4.sva_local_multicycle_antecedent::@s8.a, semantic_type = !obelisk.integral<1, false, true, 0 : 0, logic>} {
                }
              }
            }
            obelisk.sv.statement.expression_statement attributes {node_id = 140 : i64} {
              obelisk.sv.expression.call attributes {argument_count = 1 : i64, callee_name = "$display", constraint_restrictions = [], defaulted_arguments = array<i64: 0>, has_inline_constraints = false, has_iterator_expression = false, has_output_arguments = false, has_this_class = false, is_signed = false, is_super_class = false, is_system_call = true, node_id = 141 : i64, semantic_type = !obelisk.void, subroutine_kind = 1 : i32, system_library_cell = "work.sva_local_multicycle_antecedent", system_scope_path = "sva_local_multicycle_antecedent", system_scope_symbol = @s1.$root::@s3.sva_local_multicycle_antecedent::@s4.sva_local_multicycle_antecedent} {
                obelisk.sv.expression.string_literal attributes {constant_value = "nonoverlap hit", is_signed = false, node_id = 142 : i64, semantic_type = !obelisk.ranged_packed_array<111 : 0 x !obelisk.integral<1, false, false, 0 : 0, bit>>} {
                }
              }
            }
          }
        }
        obelisk.sv.symbol.procedural_block attributes {hierarchical_name = "sva_local_multicycle_antecedent", node_id = 143 : i64, procedure_kind = 2 : i32, sym_name = "s23", time_precision_fs = 1000000 : i64, time_unit_fs = 1000000 : i64} {
          obelisk.sv.statement.concurrent_assertion attributes {assertion_kind = 0 : i32, has_default_disable = false, has_fail_action = false, has_pass_action = true, node_id = 144 : i64} {
            obelisk.sv.assertion.simple attributes {has_repetition = false, is_null = false, node_id = 145 : i64, repetition_is_unbounded = false} {
              obelisk.sv.expression.assertion_instance attributes {argument_count = 0 : i64, argument_formal_paths = [], argument_formal_symbols = [], argument_kinds = array<i64>, has_expanded_body = true, is_recursive_property = false, is_signed = false, local_variable_count = 1 : i64, local_variable_has_initializer = array<i64: 1>, local_variable_paths = ["sva_local_multicycle_antecedent.p_nonoverlap.x"], local_variable_symbols = [@s1.$root::@s3.sva_local_multicycle_antecedent::@s4.sva_local_multicycle_antecedent::@s17.p_nonoverlap::@s24.x], node_id = 146 : i64, referenced_path = "sva_local_multicycle_antecedent.p_nonoverlap", referenced_symbol = @s1.$root::@s3.sva_local_multicycle_antecedent::@s4.sva_local_multicycle_antecedent::@s17.p_nonoverlap, semantic_type = !obelisk.property} {
                obelisk.sv.assertion.clocking attributes {node_id = 147 : i64} {
                  obelisk.sv.timing.signal_event attributes {edge_kind = 1 : i32, has_iff = false, node_id = 148 : i64} {
                    obelisk.sv.expression.named_value attributes {is_signed = false, node_id = 149 : i64, referenced_path = "sva_local_multicycle_antecedent.clk", referenced_symbol = @s1.$root::@s3.sva_local_multicycle_antecedent::@s4.sva_local_multicycle_antecedent::@s6.clk, semantic_type = !obelisk.integral<1, false, true, 0 : 0, logic>} {
                    }
                  }
                  obelisk.sv.assertion.binary attributes {node_id = 150 : i64, operator_kind = 14 : i32} {
                    obelisk.sv.assertion.sequence_concat attributes {delays = [{is_unbounded = false, max = 0 : i64, min = 0 : i64}, {is_unbounded = false, max = 2 : i64, min = 2 : i64}], node_id = 151 : i64} {
                      obelisk.sv.assertion.sequence_with_match attributes {has_repetition = false, match_item_count = 1 : i64, node_id = 152 : i64, repetition_is_unbounded = false} {
                        obelisk.sv.assertion.simple attributes {has_repetition = false, is_null = false, node_id = 153 : i64, repetition_is_unbounded = false} {
                          obelisk.sv.expression.named_value attributes {is_signed = false, node_id = 154 : i64, referenced_path = "sva_local_multicycle_antecedent.a", referenced_symbol = @s1.$root::@s3.sva_local_multicycle_antecedent::@s4.sva_local_multicycle_antecedent::@s8.a, semantic_type = !obelisk.integral<1, false, true, 0 : 0, logic>} {
                          }
                        }
                        obelisk.sv.expression.assignment attributes {assignment_kind = 0 : i32, is_signed = false, node_id = 155 : i64, semantic_type = !obelisk.integral<1, false, true, 0 : 0, logic>} {
                          obelisk.sv.expression.named_value attributes {is_signed = false, node_id = 156 : i64, referenced_path = "sva_local_multicycle_antecedent.p_nonoverlap.x", referenced_symbol = @s1.$root::@s3.sva_local_multicycle_antecedent::@s4.sva_local_multicycle_antecedent::@s17.p_nonoverlap::@s24.x, semantic_type = !obelisk.integral<1, false, true, 0 : 0, logic>} {
                          }
                          obelisk.sv.expression.named_value attributes {is_signed = false, node_id = 157 : i64, referenced_path = "sva_local_multicycle_antecedent.b", referenced_symbol = @s1.$root::@s3.sva_local_multicycle_antecedent::@s4.sva_local_multicycle_antecedent::@s10.b, semantic_type = !obelisk.integral<1, false, true, 0 : 0, logic>} {
                          }
                        }
                      }
                      obelisk.sv.assertion.sequence_with_match attributes {has_repetition = false, match_item_count = 1 : i64, node_id = 158 : i64, repetition_is_unbounded = false} {
                        obelisk.sv.assertion.simple attributes {has_repetition = false, is_null = false, node_id = 159 : i64, repetition_is_unbounded = false} {
                          obelisk.sv.expression.named_value attributes {is_signed = false, node_id = 160 : i64, referenced_path = "sva_local_multicycle_antecedent.b", referenced_symbol = @s1.$root::@s3.sva_local_multicycle_antecedent::@s4.sva_local_multicycle_antecedent::@s10.b, semantic_type = !obelisk.integral<1, false, true, 0 : 0, logic>} {
                          }
                        }
                        obelisk.sv.expression.assignment attributes {assignment_kind = 0 : i32, is_signed = false, node_id = 161 : i64, semantic_type = !obelisk.integral<1, false, true, 0 : 0, logic>} {
                          obelisk.sv.expression.named_value attributes {is_signed = false, node_id = 162 : i64, referenced_path = "sva_local_multicycle_antecedent.p_nonoverlap.x", referenced_symbol = @s1.$root::@s3.sva_local_multicycle_antecedent::@s4.sva_local_multicycle_antecedent::@s17.p_nonoverlap::@s24.x, semantic_type = !obelisk.integral<1, false, true, 0 : 0, logic>} {
                          }
                          obelisk.sv.expression.unary_op attributes {is_signed = false, node_id = 163 : i64, operator_kind = 9 : i32, semantic_type = !obelisk.integral<1, false, true, 0 : 0, logic>} {
                            obelisk.sv.expression.named_value attributes {is_signed = false, node_id = 164 : i64, referenced_path = "sva_local_multicycle_antecedent.p_nonoverlap.x", referenced_symbol = @s1.$root::@s3.sva_local_multicycle_antecedent::@s4.sva_local_multicycle_antecedent::@s17.p_nonoverlap::@s24.x, semantic_type = !obelisk.integral<1, false, true, 0 : 0, logic>} {
                            }
                          }
                        }
                      }
                    }
                    obelisk.sv.assertion.sequence_concat attributes {delays = [{is_unbounded = false, max = 0 : i64, min = 0 : i64}, {is_unbounded = false, max = 1 : i64, min = 1 : i64}], node_id = 165 : i64} {
                      obelisk.sv.assertion.simple attributes {has_repetition = false, is_null = false, node_id = 166 : i64, repetition_is_unbounded = false} {
                        obelisk.sv.expression.binary_op attributes {is_signed = false, node_id = 167 : i64, operator_kind = 9 : i32, semantic_type = !obelisk.integral<1, false, true, 0 : 0, logic>} {
                          obelisk.sv.expression.named_value attributes {is_signed = false, node_id = 168 : i64, referenced_path = "sva_local_multicycle_antecedent.c", referenced_symbol = @s1.$root::@s3.sva_local_multicycle_antecedent::@s4.sva_local_multicycle_antecedent::@s12.c, semantic_type = !obelisk.integral<1, false, true, 0 : 0, logic>} {
                          }
                          obelisk.sv.expression.named_value attributes {is_signed = false, node_id = 169 : i64, referenced_path = "sva_local_multicycle_antecedent.p_nonoverlap.x", referenced_symbol = @s1.$root::@s3.sva_local_multicycle_antecedent::@s4.sva_local_multicycle_antecedent::@s17.p_nonoverlap::@s24.x, semantic_type = !obelisk.integral<1, false, true, 0 : 0, logic>} {
                          }
                        }
                      }
                      obelisk.sv.assertion.simple attributes {has_repetition = false, is_null = false, node_id = 170 : i64, repetition_is_unbounded = false} {
                        obelisk.sv.expression.named_value attributes {is_signed = false, node_id = 171 : i64, referenced_path = "sva_local_multicycle_antecedent.d", referenced_symbol = @s1.$root::@s3.sva_local_multicycle_antecedent::@s4.sva_local_multicycle_antecedent::@s14.d, semantic_type = !obelisk.integral<1, false, true, 0 : 0, logic>} {
                        }
                      }
                    }
                  }
                }
                obelisk.sv.expression.named_value attributes {is_signed = false, node_id = 172 : i64, referenced_path = "sva_local_multicycle_antecedent.a", referenced_symbol = @s1.$root::@s3.sva_local_multicycle_antecedent::@s4.sva_local_multicycle_antecedent::@s8.a, semantic_type = !obelisk.integral<1, false, true, 0 : 0, logic>} {
                }
              }
            }
            obelisk.sv.statement.empty attributes {node_id = 173 : i64} {
            }
          }
        }
      }
    }
  }
}

// A pending overlapped consequent is weak for assert-property and therefore
// dispatches its pass action once for the one live consequent age.
// CHECK-LABEL: obelisk_sim.func private @unit_0.$concurrent_eos.{{.*}}.weak(
// CHECK-SAME: obelisk_sim.concurrent_eos_coordinator
// CHECK: obelisk_sim.ref.load
// CHECK-COUNT-1: obelisk_sim.spawn @unit_0.$concurrent_eos_report.{{.*}}.weak
// CHECK-NOT: obelisk_sim.spawn @unit_0.$concurrent_eos_report.{{.*}}.weak

// An antecedent that is still live at EOS has no match. Implication therefore
// completes vacuously true, independently of consequent strength.
// CHECK-LABEL: obelisk_sim.func private @unit_0.$concurrent_eos.{{.*}}.pass.antecedent_no_match(
// CHECK-SAME: obelisk_sim.concurrent_eos_coordinator
// CHECK-SAME: obelisk_sim.concurrent_eos_forced_completion
// CHECK-SAME: obelisk_sim.concurrent_eos_vacuous
// CHECK: obelisk_sim.ref.load
// CHECK-COUNT-1: obelisk_sim.spawn @unit_0.$concurrent_eos_report.{{.*}}.pass.antecedent_no_match
// CHECK-NOT: obelisk_sim.spawn @unit_0.$concurrent_eos_report.{{.*}}.pass.antecedent_no_match

// The terminal antecedent match call receives the value after its preceding
// blocking assignment and runs as a detached Reactive callback.
// CHECK: obelisk_sim.func private @[[MATCH_CALL:unit_0\.[^(]+]](
// CHECK-SAME: %arg1: !obelisk_sim.logic<1>
// CHECK-SAME: home_region = 10 : i32
// CHECK-SAME: obelisk_sim.concurrent_match_call

// CHECK-LABEL: obelisk_sim.func private @unit_0(
// CHECK-SAME: obelisk_sim.bounded_antecedent_horizon = 2 : i64
// CHECK-SAME: obelisk_sim.end_of_simulation_strength = "weak"
// CHECK: %[[STATE:.*]] = obelisk_sim.ref.alloc
// CHECK: obelisk_sim.spawn @unit_0.$concurrent_eos.{{.*}}.weak({{.*}}, %[[STATE]])
// CHECK: obelisk_sim.spawn @unit_0.$concurrent_eos.{{.*}}.pass.antecedent_no_match({{.*}}, %[[STATE]])
// CHECK: obelisk_sim.suspend.edge posedge
// CHECK: arith.andi {{.*}}, %c8_i64{{.*}}
// CHECK: %[[ANTE_ACTIVE:.*]] = arith.cmpi ne
// CHECK: %[[ANTE_VALUE:.*]] = obelisk_sim.assert.sampled_read
// CHECK: %[[ANTE_TRUE:.*]] = obelisk_sim.logic.is_true %[[ANTE_VALUE]]
// CHECK: arith.andi %[[ANTE_ACTIVE]], {{.*}} {obelisk_sim.implication_antecedent_failure}
// CHECK: %[[ANTE_MATCH:.*]] = arith.andi %[[ANTE_ACTIVE]], %[[ANTE_TRUE]] {obelisk_sim.implication_antecedent}
// CHECK: cf.cond_br %[[ANTE_MATCH]]
// CHECK: %[[UPDATED_LOCAL:.*]] = obelisk_sim.logic.unary logical_not
// CHECK: obelisk_sim.spawn @[[MATCH_CALL]](%arg0, %[[UPDATED_LOCAL]])
// CHECK: obelisk_sim.logic.compare eq {{.*}}, %[[UPDATED_LOCAL]]

// The cover form shares the same three-age antecedent and explicit one-tick
// handoff. Its action-silent false result does not require an EOS callback.
// CHECK-LABEL: obelisk_sim.func private @unit_1(
// CHECK-SAME: obelisk_sim.bounded_antecedent_horizon = 3 : i64
// CHECK-SAME: obelisk_sim.followed_by_monitor
// CHECK: obelisk_sim.suspend.edge posedge
// CHECK: arith.andi {{.*}} {obelisk_sim.implication_antecedent}
// CHECK: arith.ori {{.*}}, %c1_i64

// For assert followed-by, both still-live antecedent ages fail vacuously at
// EOS, while a terminal live match starts the nonoverlapped bit-zero handoff.
// CHECK-LABEL: obelisk_sim.func private @unit_2.$concurrent_eos.{{.*}}.fail.antecedent_no_match(
// CHECK-SAME: obelisk_sim.concurrent_eos_forced_completion
// CHECK-SAME: obelisk_sim.concurrent_eos_vacuous
// CHECK: arith.andi {{.*}}, %c16_i64
// CHECK: obelisk_sim.spawn @unit_2.$concurrent_eos_report.{{.*}}.fail.antecedent_no_match
// CHECK: arith.andi {{.*}}, %c8_i64
// CHECK: obelisk_sim.spawn @unit_2.$concurrent_eos_report.{{.*}}.fail.antecedent_no_match
// CHECK-NOT: obelisk_sim.spawn @unit_2.$concurrent_eos_report.{{.*}}.fail.antecedent_no_match

// CHECK-LABEL: obelisk_sim.func private @unit_2(
// CHECK-SAME: obelisk_sim.bounded_antecedent_horizon = 3 : i64
// CHECK-SAME: obelisk_sim.end_of_simulation_strength = "weak"
// CHECK-SAME: obelisk_sim.followed_by_monitor
// CHECK: %[[FOLLOW_STATE:.*]] = obelisk_sim.ref.alloc
// CHECK: obelisk_sim.spawn @unit_2.$concurrent_eos.{{.*}}.fail.antecedent_no_match({{.*}}, %[[FOLLOW_STATE]]
// CHECK: obelisk_sim.suspend.edge posedge
// CHECK: arith.andi {{.*}} {obelisk_sim.implication_antecedent_failure}
// CHECK: arith.andi {{.*}} {obelisk_sim.implication_antecedent}
// CHECK: arith.ori {{.*}}, %c1_i64
