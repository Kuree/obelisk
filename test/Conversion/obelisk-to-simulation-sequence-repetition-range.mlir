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
        obelisk.sv.symbol.variable attributes {hierarchical_name = "top.b", lifetime = 1 : i32, name = "b", node_id = 14 : i64, semantic_type = !obelisk.integral<1, false, true, 0 : 0, logic>, sym_name = "s14.b"} {
        }
        obelisk.sv.symbol.procedural_block attributes {hierarchical_name = "top", node_id = 7 : i64, procedure_kind = 2 : i32, sym_name = "s7", time_precision_fs = 1000000 : i64, time_unit_fs = 1000000 : i64} {
          obelisk.sv.statement.concurrent_assertion attributes {assertion_kind = 0 : i32, has_default_disable = false, has_fail_action = false, has_pass_action = false, node_id = 8 : i64} {
            obelisk.sv.assertion.clocking attributes {node_id = 9 : i64} {
              obelisk.sv.timing.signal_event attributes {edge_kind = 1 : i32, has_iff = false, node_id = 10 : i64} {
                obelisk.sv.expression.named_value attributes {node_id = 11 : i64, referenced_path = "top.clk", referenced_symbol = @s1.$root::@s3.top::@s4.top::@s5.clk, semantic_type = !obelisk.integral<1, false, true, 0 : 0, logic>} {
                }
              }
              obelisk.sv.assertion.simple attributes {has_repetition = true, is_null = false, node_id = 12 : i64, repetition_is_unbounded = false, repetition_kind = 0 : i32, repetition_max = 3 : i64, repetition_min = 1 : i64} {
                obelisk.sv.expression.named_value attributes {node_id = 13 : i64, referenced_path = "top.a", referenced_symbol = @s1.$root::@s3.top::@s4.top::@s6.a, semantic_type = !obelisk.integral<1, false, true, 0 : 0, logic>} {
                }
              }
            }
          }
        }
        obelisk.sv.symbol.procedural_block attributes {hierarchical_name = "top", node_id = 15 : i64, procedure_kind = 2 : i32, sym_name = "s15", time_precision_fs = 1000000 : i64, time_unit_fs = 1000000 : i64} {
          obelisk.sv.statement.concurrent_assertion attributes {assertion_kind = 0 : i32, has_default_disable = false, has_fail_action = false, has_pass_action = false, node_id = 16 : i64} {
            obelisk.sv.assertion.clocking attributes {node_id = 17 : i64} {
              obelisk.sv.timing.signal_event attributes {edge_kind = 1 : i32, has_iff = false, node_id = 18 : i64} {
                obelisk.sv.expression.named_value attributes {node_id = 19 : i64, referenced_path = "top.clk", referenced_symbol = @s1.$root::@s3.top::@s4.top::@s5.clk, semantic_type = !obelisk.integral<1, false, true, 0 : 0, logic>} {
                }
              }
              obelisk.sv.assertion.binary attributes {node_id = 20 : i64, operator_kind = 4 : i32} {
                obelisk.sv.assertion.simple attributes {has_repetition = false, is_null = false, node_id = 21 : i64, repetition_is_unbounded = false} {
                  obelisk.sv.expression.named_value attributes {node_id = 22 : i64, referenced_path = "top.a", referenced_symbol = @s1.$root::@s3.top::@s4.top::@s6.a, semantic_type = !obelisk.integral<1, false, true, 0 : 0, logic>} {
                  }
                }
                obelisk.sv.assertion.simple attributes {has_repetition = true, is_null = false, node_id = 23 : i64, repetition_is_unbounded = false, repetition_kind = 0 : i32, repetition_max = 3 : i64, repetition_min = 3 : i64} {
                  obelisk.sv.expression.named_value attributes {node_id = 24 : i64, referenced_path = "top.b", referenced_symbol = @s1.$root::@s3.top::@s4.top::@s14.b, semantic_type = !obelisk.integral<1, false, true, 0 : 0, logic>} {
                  }
                }
              }
            }
          }
        }
        obelisk.sv.symbol.procedural_block attributes {hierarchical_name = "top", node_id = 25 : i64, procedure_kind = 2 : i32, sym_name = "s25", time_precision_fs = 1000000 : i64, time_unit_fs = 1000000 : i64} {
          obelisk.sv.statement.concurrent_assertion attributes {assertion_kind = 0 : i32, has_default_disable = false, has_fail_action = false, has_pass_action = false, node_id = 26 : i64} {
            obelisk.sv.assertion.clocking attributes {node_id = 27 : i64} {
              obelisk.sv.timing.signal_event attributes {edge_kind = 1 : i32, has_iff = false, node_id = 28 : i64} {
                obelisk.sv.expression.named_value attributes {node_id = 29 : i64, referenced_path = "top.clk", referenced_symbol = @s1.$root::@s3.top::@s4.top::@s5.clk, semantic_type = !obelisk.integral<1, false, true, 0 : 0, logic>} {
                }
              }
              obelisk.sv.assertion.first_match attributes {match_item_count = 0 : i64, node_id = 30 : i64} {
                obelisk.sv.assertion.sequence_concat attributes {delays = [{is_unbounded = false, max = 0 : i64, min = 0 : i64}, {is_unbounded = false, max = 3 : i64, min = 1 : i64}], node_id = 31 : i64} {
                  obelisk.sv.assertion.simple attributes {has_repetition = false, is_null = false, node_id = 32 : i64, repetition_is_unbounded = false} {
                    obelisk.sv.expression.named_value attributes {node_id = 33 : i64, referenced_path = "top.a", referenced_symbol = @s1.$root::@s3.top::@s4.top::@s6.a, semantic_type = !obelisk.integral<1, false, true, 0 : 0, logic>} {
                    }
                  }
                  obelisk.sv.assertion.simple attributes {has_repetition = false, is_null = false, node_id = 34 : i64, repetition_is_unbounded = false} {
                    obelisk.sv.expression.named_value attributes {node_id = 35 : i64, referenced_path = "top.b", referenced_symbol = @s1.$root::@s3.top::@s4.top::@s14.b, semantic_type = !obelisk.integral<1, false, true, 0 : 0, logic>} {
                    }
                  }
                }
              }
            }
          }
        }
        obelisk.sv.symbol.procedural_block attributes {hierarchical_name = "top", node_id = 36 : i64, procedure_kind = 2 : i32, sym_name = "s36", time_precision_fs = 1000000 : i64, time_unit_fs = 1000000 : i64} {
          obelisk.sv.statement.concurrent_assertion attributes {assertion_kind = 0 : i32, has_default_disable = false, has_fail_action = false, has_pass_action = false, node_id = 37 : i64} {
            obelisk.sv.assertion.clocking attributes {node_id = 38 : i64} {
              obelisk.sv.timing.signal_event attributes {edge_kind = 1 : i32, has_iff = false, node_id = 39 : i64} {
                obelisk.sv.expression.named_value attributes {node_id = 40 : i64, referenced_path = "top.clk", referenced_symbol = @s1.$root::@s3.top::@s4.top::@s5.clk, semantic_type = !obelisk.integral<1, false, true, 0 : 0, logic>} {
                }
              }
              obelisk.sv.assertion.sequence_concat attributes {delays = [{is_unbounded = false, max = 0 : i64, min = 0 : i64}, {is_unbounded = false, max = 1 : i64, min = 1 : i64}], node_id = 41 : i64} {
                obelisk.sv.assertion.first_match attributes {match_item_count = 0 : i64, node_id = 42 : i64} {
                  obelisk.sv.assertion.sequence_concat attributes {delays = [{is_unbounded = false, max = 0 : i64, min = 0 : i64}, {is_unbounded = false, max = 2 : i64, min = 1 : i64}], node_id = 43 : i64} {
                    obelisk.sv.assertion.simple attributes {has_repetition = false, is_null = false, node_id = 44 : i64, repetition_is_unbounded = false} {
                      obelisk.sv.expression.named_value attributes {node_id = 45 : i64, referenced_path = "top.a", referenced_symbol = @s1.$root::@s3.top::@s4.top::@s6.a, semantic_type = !obelisk.integral<1, false, true, 0 : 0, logic>} {
                      }
                    }
                    obelisk.sv.assertion.simple attributes {has_repetition = false, is_null = false, node_id = 46 : i64, repetition_is_unbounded = false} {
                      obelisk.sv.expression.named_value attributes {node_id = 47 : i64, referenced_path = "top.b", referenced_symbol = @s1.$root::@s3.top::@s4.top::@s14.b, semantic_type = !obelisk.integral<1, false, true, 0 : 0, logic>} {
                      }
                    }
                  }
                }
                obelisk.sv.assertion.simple attributes {has_repetition = false, is_null = false, node_id = 48 : i64, repetition_is_unbounded = false} {
                  obelisk.sv.expression.named_value attributes {node_id = 49 : i64, referenced_path = "top.a", referenced_symbol = @s1.$root::@s3.top::@s4.top::@s6.a, semantic_type = !obelisk.integral<1, false, true, 0 : 0, logic>} {
                  }
                }
              }
            }
          }
        }
        obelisk.sv.symbol.procedural_block attributes {hierarchical_name = "top", node_id = 50 : i64, procedure_kind = 2 : i32, sym_name = "s50", time_precision_fs = 1000000 : i64, time_unit_fs = 1000000 : i64} {
          obelisk.sv.statement.concurrent_assertion attributes {assertion_kind = 0 : i32, has_default_disable = false, has_fail_action = false, has_pass_action = true, node_id = 51 : i64} {
            obelisk.sv.assertion.clocking attributes {node_id = 52 : i64} {
              obelisk.sv.timing.signal_event attributes {edge_kind = 1 : i32, has_iff = false, node_id = 53 : i64} {
                obelisk.sv.expression.named_value attributes {node_id = 54 : i64, referenced_path = "top.clk", referenced_symbol = @s1.$root::@s3.top::@s4.top::@s5.clk, semantic_type = !obelisk.integral<1, false, true, 0 : 0, logic>} {
                }
              }
              obelisk.sv.assertion.binary attributes {node_id = 55 : i64, operator_kind = 11 : i32} {
                obelisk.sv.assertion.sequence_concat attributes {delays = [{is_unbounded = false, max = 0 : i64, min = 0 : i64}, {is_unbounded = false, max = 1 : i64, min = 1 : i64}], node_id = 56 : i64} {
                  obelisk.sv.assertion.simple attributes {has_repetition = false, is_null = false, node_id = 57 : i64, repetition_is_unbounded = false} {
                    obelisk.sv.expression.named_value attributes {node_id = 58 : i64, referenced_path = "top.a", referenced_symbol = @s1.$root::@s3.top::@s4.top::@s6.a, semantic_type = !obelisk.integral<1, false, true, 0 : 0, logic>} {
                    }
                  }
                  obelisk.sv.assertion.simple attributes {has_repetition = false, is_null = false, node_id = 59 : i64, repetition_is_unbounded = false} {
                    obelisk.sv.expression.named_value attributes {node_id = 60 : i64, referenced_path = "top.b", referenced_symbol = @s1.$root::@s3.top::@s4.top::@s14.b, semantic_type = !obelisk.integral<1, false, true, 0 : 0, logic>} {
                    }
                  }
                }
                obelisk.sv.assertion.sequence_concat attributes {delays = [{is_unbounded = false, max = 0 : i64, min = 0 : i64}, {is_unbounded = false, max = 1 : i64, min = 1 : i64}], node_id = 61 : i64} {
                  obelisk.sv.assertion.simple attributes {has_repetition = false, is_null = false, node_id = 62 : i64, repetition_is_unbounded = false} {
                    obelisk.sv.expression.named_value attributes {node_id = 63 : i64, referenced_path = "top.b", referenced_symbol = @s1.$root::@s3.top::@s4.top::@s14.b, semantic_type = !obelisk.integral<1, false, true, 0 : 0, logic>} {
                    }
                  }
                  obelisk.sv.assertion.simple attributes {has_repetition = false, is_null = false, node_id = 64 : i64, repetition_is_unbounded = false} {
                    obelisk.sv.expression.named_value attributes {node_id = 65 : i64, referenced_path = "top.a", referenced_symbol = @s1.$root::@s3.top::@s4.top::@s6.a, semantic_type = !obelisk.integral<1, false, true, 0 : 0, logic>} {
                    }
                  }
                }
              }
            }
            obelisk.sv.statement.expression_statement attributes {node_id = 79 : i64} {
              obelisk.sv.expression.call attributes {argument_count = 0 : i64, callee_name = "$display", constraint_restrictions = [], defaulted_arguments = array<i64>, has_inline_constraints = false, has_iterator_expression = false, has_output_arguments = false, has_this_class = false, is_signed = false, is_super_class = false, is_system_call = true, node_id = 80 : i64, semantic_type = !obelisk.void, subroutine_kind = 1 : i32, system_library_cell = "work.top", system_scope_path = "top", system_scope_symbol = @s1.$root::@s3.top::@s4.top} {
              }
            }
          }
        }
        obelisk.sv.symbol.procedural_block attributes {hierarchical_name = "top", node_id = 66 : i64, procedure_kind = 2 : i32, sym_name = "s66", time_precision_fs = 1000000 : i64, time_unit_fs = 1000000 : i64} {
          obelisk.sv.statement.concurrent_assertion attributes {assertion_kind = 0 : i32, has_default_disable = false, has_fail_action = false, has_pass_action = true, node_id = 67 : i64} {
            obelisk.sv.assertion.clocking attributes {node_id = 68 : i64} {
              obelisk.sv.timing.signal_event attributes {edge_kind = 1 : i32, has_iff = false, node_id = 69 : i64} {
                obelisk.sv.expression.named_value attributes {node_id = 70 : i64, referenced_path = "top.clk", referenced_symbol = @s1.$root::@s3.top::@s4.top::@s5.clk, semantic_type = !obelisk.integral<1, false, true, 0 : 0, logic>} {
                }
              }
              obelisk.sv.assertion.binary attributes {node_id = 71 : i64, operator_kind = 14 : i32} {
                obelisk.sv.assertion.sequence_concat attributes {delays = [{is_unbounded = false, max = 0 : i64, min = 0 : i64}, {is_unbounded = false, max = 1 : i64, min = 1 : i64}], node_id = 72 : i64} {
                  obelisk.sv.assertion.simple attributes {has_repetition = false, is_null = false, node_id = 73 : i64, repetition_is_unbounded = false} {
                    obelisk.sv.expression.named_value attributes {node_id = 74 : i64, referenced_path = "top.a", referenced_symbol = @s1.$root::@s3.top::@s4.top::@s6.a, semantic_type = !obelisk.integral<1, false, true, 0 : 0, logic>} {
                    }
                  }
                  obelisk.sv.assertion.simple attributes {has_repetition = false, is_null = false, node_id = 75 : i64, repetition_is_unbounded = false} {
                    obelisk.sv.expression.named_value attributes {node_id = 76 : i64, referenced_path = "top.b", referenced_symbol = @s1.$root::@s3.top::@s4.top::@s14.b, semantic_type = !obelisk.integral<1, false, true, 0 : 0, logic>} {
                    }
                  }
                }
                obelisk.sv.assertion.simple attributes {has_repetition = false, is_null = false, node_id = 77 : i64, repetition_is_unbounded = false} {
                  obelisk.sv.expression.named_value attributes {node_id = 78 : i64, referenced_path = "top.a", referenced_symbol = @s1.$root::@s3.top::@s4.top::@s6.a, semantic_type = !obelisk.integral<1, false, true, 0 : 0, logic>} {
                  }
                }
              }
            }
            obelisk.sv.statement.expression_statement attributes {node_id = 81 : i64} {
              obelisk.sv.expression.call attributes {argument_count = 0 : i64, callee_name = "$display", constraint_restrictions = [], defaulted_arguments = array<i64>, has_inline_constraints = false, has_iterator_expression = false, has_output_arguments = false, has_this_class = false, is_signed = false, is_super_class = false, is_system_call = true, node_id = 82 : i64, semantic_type = !obelisk.void, subroutine_kind = 1 : i32, system_library_cell = "work.top", system_scope_path = "top", system_scope_symbol = @s1.$root::@s3.top::@s4.top} {
              }
            }
          }
        }
        obelisk.sv.symbol.procedural_block attributes {hierarchical_name = "top", node_id = 100 : i64, procedure_kind = 2 : i32, sym_name = "s100", time_precision_fs = 1000000 : i64, time_unit_fs = 1000000 : i64} {
          obelisk.sv.statement.concurrent_assertion attributes {assertion_kind = 0 : i32, has_default_disable = false, has_fail_action = false, has_pass_action = false, node_id = 101 : i64} {
            obelisk.sv.assertion.clocking attributes {node_id = 102 : i64} {
              obelisk.sv.timing.signal_event attributes {edge_kind = 1 : i32, has_iff = false, node_id = 103 : i64} {
                obelisk.sv.expression.named_value attributes {node_id = 104 : i64, referenced_path = "top.clk", referenced_symbol = @s1.$root::@s3.top::@s4.top::@s5.clk, semantic_type = !obelisk.integral<1, false, true, 0 : 0, logic>} {
                }
              }
              obelisk.sv.assertion.first_match attributes {match_item_count = 0 : i64, node_id = 105 : i64} {
                obelisk.sv.assertion.sequence_concat attributes {delays = [{is_unbounded = false, max = 0 : i64, min = 0 : i64}, {is_unbounded = false, max = 1 : i64, min = 1 : i64}, {is_unbounded = false, max = 2 : i64, min = 1 : i64}], node_id = 106 : i64} {
                  obelisk.sv.assertion.first_match attributes {match_item_count = 0 : i64, node_id = 107 : i64} {
                    obelisk.sv.assertion.sequence_concat attributes {delays = [{is_unbounded = false, max = 0 : i64, min = 0 : i64}, {is_unbounded = false, max = 2 : i64, min = 1 : i64}], node_id = 108 : i64} {
                      obelisk.sv.assertion.simple attributes {has_repetition = false, is_null = false, node_id = 109 : i64, repetition_is_unbounded = false} {
                        obelisk.sv.expression.named_value attributes {node_id = 110 : i64, referenced_path = "top.a", referenced_symbol = @s1.$root::@s3.top::@s4.top::@s6.a, semantic_type = !obelisk.integral<1, false, true, 0 : 0, logic>} {
                        }
                      }
                      obelisk.sv.assertion.simple attributes {has_repetition = false, is_null = false, node_id = 111 : i64, repetition_is_unbounded = false} {
                        obelisk.sv.expression.named_value attributes {node_id = 112 : i64, referenced_path = "top.b", referenced_symbol = @s1.$root::@s3.top::@s4.top::@s14.b, semantic_type = !obelisk.integral<1, false, true, 0 : 0, logic>} {
                        }
                      }
                    }
                  }
                  obelisk.sv.assertion.simple attributes {has_repetition = false, is_null = false, node_id = 113 : i64, repetition_is_unbounded = false} {
                    obelisk.sv.expression.named_value attributes {node_id = 114 : i64, referenced_path = "top.a", referenced_symbol = @s1.$root::@s3.top::@s4.top::@s6.a, semantic_type = !obelisk.integral<1, false, true, 0 : 0, logic>} {
                    }
                  }
                  obelisk.sv.assertion.simple attributes {has_repetition = false, is_null = false, node_id = 115 : i64, repetition_is_unbounded = false} {
                    obelisk.sv.expression.named_value attributes {node_id = 116 : i64, referenced_path = "top.b", referenced_symbol = @s1.$root::@s3.top::@s4.top::@s14.b, semantic_type = !obelisk.integral<1, false, true, 0 : 0, logic>} {
                    }
                  }
                }
              }
            }
          }
        }
        obelisk.sv.symbol.procedural_block attributes {hierarchical_name = "top", node_id = 120 : i64, procedure_kind = 2 : i32, sym_name = "s120", time_precision_fs = 1000000 : i64, time_unit_fs = 1000000 : i64} {
          obelisk.sv.statement.concurrent_assertion attributes {assertion_kind = 0 : i32, has_default_disable = false, has_fail_action = false, has_pass_action = false, node_id = 121 : i64} {
            obelisk.sv.assertion.clocking attributes {node_id = 122 : i64} {
              obelisk.sv.timing.signal_event attributes {edge_kind = 1 : i32, has_iff = false, node_id = 123 : i64} {
                obelisk.sv.expression.named_value attributes {node_id = 124 : i64, referenced_path = "top.clk", referenced_symbol = @s1.$root::@s3.top::@s4.top::@s5.clk, semantic_type = !obelisk.integral<1, false, true, 0 : 0, logic>} {
                }
              }
              obelisk.sv.assertion.sequence_concat attributes {delays = [{is_unbounded = false, max = 0 : i64, min = 0 : i64}, {is_unbounded = false, max = 1 : i64, min = 1 : i64}], node_id = 125 : i64} {
                obelisk.sv.assertion.first_match attributes {match_item_count = 0 : i64, node_id = 126 : i64} {
                  obelisk.sv.assertion.sequence_concat attributes {delays = [{is_unbounded = false, max = 0 : i64, min = 0 : i64}, {is_unbounded = false, max = 2 : i64, min = 1 : i64}], node_id = 127 : i64} {
                    obelisk.sv.assertion.simple attributes {has_repetition = false, is_null = false, node_id = 128 : i64, repetition_is_unbounded = false} {
                      obelisk.sv.expression.named_value attributes {node_id = 129 : i64, referenced_path = "top.a", referenced_symbol = @s1.$root::@s3.top::@s4.top::@s6.a, semantic_type = !obelisk.integral<1, false, true, 0 : 0, logic>} {
                      }
                    }
                    obelisk.sv.assertion.simple attributes {has_repetition = false, is_null = false, node_id = 130 : i64, repetition_is_unbounded = false} {
                      obelisk.sv.expression.named_value attributes {node_id = 131 : i64, referenced_path = "top.b", referenced_symbol = @s1.$root::@s3.top::@s4.top::@s14.b, semantic_type = !obelisk.integral<1, false, true, 0 : 0, logic>} {
                      }
                    }
                  }
                }
                obelisk.sv.assertion.first_match attributes {match_item_count = 0 : i64, node_id = 132 : i64} {
                  obelisk.sv.assertion.sequence_concat attributes {delays = [{is_unbounded = false, max = 0 : i64, min = 0 : i64}, {is_unbounded = false, max = 2 : i64, min = 1 : i64}], node_id = 133 : i64} {
                    obelisk.sv.assertion.simple attributes {has_repetition = false, is_null = false, node_id = 134 : i64, repetition_is_unbounded = false} {
                      obelisk.sv.expression.named_value attributes {node_id = 135 : i64, referenced_path = "top.a", referenced_symbol = @s1.$root::@s3.top::@s4.top::@s6.a, semantic_type = !obelisk.integral<1, false, true, 0 : 0, logic>} {
                      }
                    }
                    obelisk.sv.assertion.simple attributes {has_repetition = false, is_null = false, node_id = 136 : i64, repetition_is_unbounded = false} {
                      obelisk.sv.expression.named_value attributes {node_id = 137 : i64, referenced_path = "top.b", referenced_symbol = @s1.$root::@s3.top::@s4.top::@s14.b, semantic_type = !obelisk.integral<1, false, true, 0 : 0, logic>} {
                      }
                    }
                  }
                }
              }
            }
          }
        }
        obelisk.sv.symbol.procedural_block attributes {hierarchical_name = "top", node_id = 140 : i64, procedure_kind = 2 : i32, sym_name = "s140", time_precision_fs = 1000000 : i64, time_unit_fs = 1000000 : i64} {
          obelisk.sv.statement.concurrent_assertion attributes {assertion_kind = 0 : i32, has_default_disable = false, has_fail_action = false, has_pass_action = false, node_id = 141 : i64} {
            obelisk.sv.assertion.clocking attributes {node_id = 142 : i64} {
              obelisk.sv.timing.signal_event attributes {edge_kind = 1 : i32, has_iff = false, node_id = 143 : i64} {
                obelisk.sv.expression.named_value attributes {node_id = 144 : i64, referenced_path = "top.clk", referenced_symbol = @s1.$root::@s3.top::@s4.top::@s5.clk, semantic_type = !obelisk.integral<1, false, true, 0 : 0, logic>} {
                }
              }
              obelisk.sv.assertion.sequence_concat attributes {delays = [{is_unbounded = false, max = 0 : i64, min = 0 : i64}, {is_unbounded = false, max = 2 : i64, min = 1 : i64}, {is_unbounded = false, max = 1 : i64, min = 1 : i64}, {is_unbounded = false, max = 1 : i64, min = 1 : i64}], node_id = 145 : i64} {
                obelisk.sv.assertion.simple attributes {has_repetition = false, is_null = false, node_id = 146 : i64, repetition_is_unbounded = false} {
                  obelisk.sv.expression.named_value attributes {node_id = 147 : i64, referenced_path = "top.a", referenced_symbol = @s1.$root::@s3.top::@s4.top::@s6.a, semantic_type = !obelisk.integral<1, false, true, 0 : 0, logic>} {
                  }
                }
                obelisk.sv.assertion.simple attributes {has_repetition = false, is_null = false, node_id = 148 : i64, repetition_is_unbounded = false} {
                  obelisk.sv.expression.named_value attributes {node_id = 149 : i64, referenced_path = "top.b", referenced_symbol = @s1.$root::@s3.top::@s4.top::@s14.b, semantic_type = !obelisk.integral<1, false, true, 0 : 0, logic>} {
                  }
                }
                obelisk.sv.assertion.first_match attributes {match_item_count = 0 : i64, node_id = 150 : i64} {
                  obelisk.sv.assertion.sequence_concat attributes {delays = [{is_unbounded = false, max = 0 : i64, min = 0 : i64}, {is_unbounded = false, max = 2 : i64, min = 1 : i64}], node_id = 151 : i64} {
                    obelisk.sv.assertion.simple attributes {has_repetition = false, is_null = false, node_id = 152 : i64, repetition_is_unbounded = false} {
                      obelisk.sv.expression.named_value attributes {node_id = 153 : i64, referenced_path = "top.a", referenced_symbol = @s1.$root::@s3.top::@s4.top::@s6.a, semantic_type = !obelisk.integral<1, false, true, 0 : 0, logic>} {
                      }
                    }
                    obelisk.sv.assertion.simple attributes {has_repetition = false, is_null = false, node_id = 154 : i64, repetition_is_unbounded = false} {
                      obelisk.sv.expression.named_value attributes {node_id = 155 : i64, referenced_path = "top.b", referenced_symbol = @s1.$root::@s3.top::@s4.top::@s14.b, semantic_type = !obelisk.integral<1, false, true, 0 : 0, logic>} {
                      }
                    }
                  }
                }
                obelisk.sv.assertion.simple attributes {has_repetition = false, is_null = false, node_id = 156 : i64, repetition_is_unbounded = false} {
                  obelisk.sv.expression.named_value attributes {node_id = 157 : i64, referenced_path = "top.a", referenced_symbol = @s1.$root::@s3.top::@s4.top::@s6.a, semantic_type = !obelisk.integral<1, false, true, 0 : 0, logic>} {
                  }
                }
              }
            }
          }
        }
        obelisk.sv.symbol.procedural_block attributes {hierarchical_name = "top", node_id = 160 : i64, procedure_kind = 2 : i32, sym_name = "s160", time_precision_fs = 1000000 : i64, time_unit_fs = 1000000 : i64} {
          obelisk.sv.statement.concurrent_assertion attributes {assertion_kind = 0 : i32, has_default_disable = false, has_fail_action = false, has_pass_action = false, node_id = 161 : i64} {
            obelisk.sv.assertion.clocking attributes {node_id = 162 : i64} {
              obelisk.sv.timing.signal_event attributes {edge_kind = 1 : i32, has_iff = false, node_id = 163 : i64} {
                obelisk.sv.expression.named_value attributes {node_id = 164 : i64, referenced_path = "top.clk", referenced_symbol = @s1.$root::@s3.top::@s4.top::@s5.clk, semantic_type = !obelisk.integral<1, false, true, 0 : 0, logic>} {
                }
              }
              obelisk.sv.assertion.unary attributes {has_range = true, node_id = 165 : i64, operator_kind = 5 : i32, range_is_unbounded = false, range_max = 2 : i64, range_min = 1 : i64} {
                obelisk.sv.assertion.first_match attributes {match_item_count = 0 : i64, node_id = 166 : i64} {
                  obelisk.sv.assertion.sequence_concat attributes {delays = [{is_unbounded = false, max = 0 : i64, min = 0 : i64}, {is_unbounded = false, max = 2 : i64, min = 1 : i64}], node_id = 167 : i64} {
                    obelisk.sv.assertion.simple attributes {has_repetition = false, is_null = false, node_id = 168 : i64, repetition_is_unbounded = false} {
                      obelisk.sv.expression.named_value attributes {node_id = 169 : i64, referenced_path = "top.a", referenced_symbol = @s1.$root::@s3.top::@s4.top::@s6.a, semantic_type = !obelisk.integral<1, false, true, 0 : 0, logic>} {
                      }
                    }
                    obelisk.sv.assertion.simple attributes {has_repetition = false, is_null = false, node_id = 170 : i64, repetition_is_unbounded = false} {
                      obelisk.sv.expression.named_value attributes {node_id = 171 : i64, referenced_path = "top.b", referenced_symbol = @s1.$root::@s3.top::@s4.top::@s14.b, semantic_type = !obelisk.integral<1, false, true, 0 : 0, logic>} {
                      }
                    }
                  }
                }
              }
            }
          }
        }
        obelisk.sv.symbol.procedural_block attributes {hierarchical_name = "top", node_id = 180 : i64, procedure_kind = 2 : i32, sym_name = "s180", time_precision_fs = 1000000 : i64, time_unit_fs = 1000000 : i64} {
          obelisk.sv.statement.concurrent_assertion attributes {assertion_kind = 0 : i32, has_default_disable = false, has_fail_action = false, has_pass_action = false, node_id = 181 : i64} {
            obelisk.sv.assertion.clocking attributes {node_id = 182 : i64} {
              obelisk.sv.timing.signal_event attributes {edge_kind = 1 : i32, has_iff = false, node_id = 183 : i64} {
                obelisk.sv.expression.named_value attributes {node_id = 184 : i64, referenced_path = "top.clk", referenced_symbol = @s1.$root::@s3.top::@s4.top::@s5.clk, semantic_type = !obelisk.integral<1, false, true, 0 : 0, logic>} {
                }
              }
              obelisk.sv.assertion.unary attributes {has_range = true, node_id = 185 : i64, operator_kind = 3 : i32, range_is_unbounded = false, range_max = 2 : i64, range_min = 1 : i64} {
                obelisk.sv.assertion.first_match attributes {match_item_count = 0 : i64, node_id = 186 : i64} {
                  obelisk.sv.assertion.sequence_concat attributes {delays = [{is_unbounded = false, max = 0 : i64, min = 0 : i64}, {is_unbounded = false, max = 2 : i64, min = 1 : i64}], node_id = 187 : i64} {
                    obelisk.sv.assertion.simple attributes {has_repetition = false, is_null = false, node_id = 188 : i64, repetition_is_unbounded = false} {
                      obelisk.sv.expression.named_value attributes {node_id = 189 : i64, referenced_path = "top.a", referenced_symbol = @s1.$root::@s3.top::@s4.top::@s6.a, semantic_type = !obelisk.integral<1, false, true, 0 : 0, logic>} {
                      }
                    }
                    obelisk.sv.assertion.simple attributes {has_repetition = false, is_null = false, node_id = 190 : i64, repetition_is_unbounded = false} {
                      obelisk.sv.expression.named_value attributes {node_id = 191 : i64, referenced_path = "top.b", referenced_symbol = @s1.$root::@s3.top::@s4.top::@s14.b, semantic_type = !obelisk.integral<1, false, true, 0 : 0, logic>} {
                      }
                    }
                  }
                }
              }
            }
          }
        }
        obelisk.sv.symbol.procedural_block attributes {hierarchical_name = "top", node_id = 200 : i64, procedure_kind = 2 : i32, sym_name = "s200", time_precision_fs = 1000000 : i64, time_unit_fs = 1000000 : i64} {
          obelisk.sv.statement.concurrent_assertion attributes {assertion_kind = 0 : i32, has_default_disable = false, has_fail_action = false, has_pass_action = true, node_id = 201 : i64} {
            obelisk.sv.assertion.clocking attributes {node_id = 202 : i64} {
              obelisk.sv.timing.signal_event attributes {edge_kind = 1 : i32, has_iff = false, node_id = 203 : i64} {
                obelisk.sv.expression.named_value attributes {node_id = 204 : i64, referenced_path = "top.clk", referenced_symbol = @s1.$root::@s3.top::@s4.top::@s5.clk, semantic_type = !obelisk.integral<1, false, true, 0 : 0, logic>} {
                }
              }
              obelisk.sv.assertion.binary attributes {node_id = 205 : i64, operator_kind = 11 : i32} {
                obelisk.sv.assertion.sequence_concat attributes {delays = [{is_unbounded = false, max = 0 : i64, min = 0 : i64}, {is_unbounded = false, max = 2 : i64, min = 1 : i64}], node_id = 206 : i64} {
                  obelisk.sv.assertion.simple attributes {has_repetition = false, is_null = false, node_id = 207 : i64, repetition_is_unbounded = false} {
                    obelisk.sv.expression.named_value attributes {node_id = 208 : i64, referenced_path = "top.a", referenced_symbol = @s1.$root::@s3.top::@s4.top::@s6.a, semantic_type = !obelisk.integral<1, false, true, 0 : 0, logic>} {
                    }
                  }
                  obelisk.sv.assertion.simple attributes {has_repetition = false, is_null = false, node_id = 209 : i64, repetition_is_unbounded = false} {
                    obelisk.sv.expression.named_value attributes {node_id = 210 : i64, referenced_path = "top.b", referenced_symbol = @s1.$root::@s3.top::@s4.top::@s14.b, semantic_type = !obelisk.integral<1, false, true, 0 : 0, logic>} {
                    }
                  }
                }
                obelisk.sv.assertion.sequence_concat attributes {delays = [{is_unbounded = false, max = 0 : i64, min = 0 : i64}, {is_unbounded = false, max = 1 : i64, min = 1 : i64}], node_id = 211 : i64} {
                  obelisk.sv.assertion.simple attributes {has_repetition = false, is_null = false, node_id = 212 : i64, repetition_is_unbounded = false} {
                    obelisk.sv.expression.named_value attributes {node_id = 213 : i64, referenced_path = "top.a", referenced_symbol = @s1.$root::@s3.top::@s4.top::@s6.a, semantic_type = !obelisk.integral<1, false, true, 0 : 0, logic>} {
                    }
                  }
                  obelisk.sv.assertion.simple attributes {has_repetition = false, is_null = false, node_id = 214 : i64, repetition_is_unbounded = false} {
                    obelisk.sv.expression.named_value attributes {node_id = 215 : i64, referenced_path = "top.b", referenced_symbol = @s1.$root::@s3.top::@s4.top::@s14.b, semantic_type = !obelisk.integral<1, false, true, 0 : 0, logic>} {
                    }
                  }
                }
              }
            }
            obelisk.sv.statement.expression_statement attributes {node_id = 216 : i64} {
              obelisk.sv.expression.call attributes {argument_count = 0 : i64, callee_name = "$display", constraint_restrictions = [], defaulted_arguments = array<i64>, has_inline_constraints = false, has_iterator_expression = false, has_output_arguments = false, has_this_class = false, is_signed = false, is_super_class = false, is_system_call = true, node_id = 217 : i64, semantic_type = !obelisk.void, subroutine_kind = 1 : i32, system_library_cell = "work.top", system_scope_path = "top", system_scope_symbol = @s1.$root::@s3.top::@s4.top} {
              }
            }
          }
        }
        // (a or b) #-# a. Same-endpoint antecedent matches are distinct
        // existential obligations of one followed-by property attempt.
        obelisk.sv.symbol.procedural_block attributes {hierarchical_name = "top", node_id = 220 : i64, procedure_kind = 2 : i32, sym_name = "s220", time_precision_fs = 1000000 : i64, time_unit_fs = 1000000 : i64} {
          obelisk.sv.statement.concurrent_assertion attributes {assertion_kind = 0 : i32, has_default_disable = false, has_fail_action = false, has_pass_action = true, node_id = 221 : i64} {
            obelisk.sv.assertion.clocking attributes {node_id = 222 : i64} {
              obelisk.sv.timing.signal_event attributes {edge_kind = 1 : i32, has_iff = false, node_id = 223 : i64} {
                obelisk.sv.expression.named_value attributes {node_id = 224 : i64, referenced_path = "top.clk", referenced_symbol = @s1.$root::@s3.top::@s4.top::@s5.clk, semantic_type = !obelisk.integral<1, false, true, 0 : 0, logic>} {
                }
              }
              obelisk.sv.assertion.binary attributes {node_id = 225 : i64, operator_kind = 13 : i32} {
                obelisk.sv.assertion.binary attributes {node_id = 226 : i64, operator_kind = 1 : i32} {
                  obelisk.sv.assertion.simple attributes {has_repetition = false, is_null = false, node_id = 227 : i64, repetition_is_unbounded = false} {
                    obelisk.sv.expression.named_value attributes {node_id = 228 : i64, referenced_path = "top.a", referenced_symbol = @s1.$root::@s3.top::@s4.top::@s6.a, semantic_type = !obelisk.integral<1, false, true, 0 : 0, logic>} {
                    }
                  }
                  obelisk.sv.assertion.simple attributes {has_repetition = false, is_null = false, node_id = 229 : i64, repetition_is_unbounded = false} {
                    obelisk.sv.expression.named_value attributes {node_id = 230 : i64, referenced_path = "top.b", referenced_symbol = @s1.$root::@s3.top::@s4.top::@s14.b, semantic_type = !obelisk.integral<1, false, true, 0 : 0, logic>} {
                    }
                  }
                }
                obelisk.sv.assertion.simple attributes {has_repetition = false, is_null = false, node_id = 231 : i64, repetition_is_unbounded = false} {
                  obelisk.sv.expression.named_value attributes {node_id = 232 : i64, referenced_path = "top.a", referenced_symbol = @s1.$root::@s3.top::@s4.top::@s6.a, semantic_type = !obelisk.integral<1, false, true, 0 : 0, logic>} {
                  }
                }
              }
            }
            obelisk.sv.statement.expression_statement attributes {node_id = 233 : i64} {
              obelisk.sv.expression.call attributes {argument_count = 0 : i64, callee_name = "$display", constraint_restrictions = [], defaulted_arguments = array<i64>, has_inline_constraints = false, has_iterator_expression = false, has_output_arguments = false, has_this_class = false, is_signed = false, is_super_class = false, is_system_call = true, node_id = 234 : i64, semantic_type = !obelisk.void, subroutine_kind = 1 : i32, system_library_cell = "work.top", system_scope_path = "top", system_scope_symbol = @s1.$root::@s3.top::@s4.top} {
              }
            }
          }
        }
        // cover property ((a ##[1:2] b) |-> a). If simulation ends while an
        // antecedent endpoint is still possible, that path can no longer
        // match; the implication succeeds vacuously once per source attempt.
        obelisk.sv.symbol.procedural_block attributes {hierarchical_name = "top", node_id = 240 : i64, procedure_kind = 2 : i32, sym_name = "s240", time_precision_fs = 1000000 : i64, time_unit_fs = 1000000 : i64} {
          obelisk.sv.statement.concurrent_assertion attributes {assertion_kind = 2 : i32, has_default_disable = false, has_fail_action = false, has_pass_action = true, node_id = 241 : i64} {
            obelisk.sv.assertion.clocking attributes {node_id = 242 : i64} {
              obelisk.sv.timing.signal_event attributes {edge_kind = 1 : i32, has_iff = false, node_id = 243 : i64} {
                obelisk.sv.expression.named_value attributes {node_id = 244 : i64, referenced_path = "top.clk", referenced_symbol = @s1.$root::@s3.top::@s4.top::@s5.clk, semantic_type = !obelisk.integral<1, false, true, 0 : 0, logic>} {
                }
              }
              obelisk.sv.assertion.binary attributes {node_id = 245 : i64, operator_kind = 11 : i32} {
                obelisk.sv.assertion.sequence_concat attributes {delays = [{is_unbounded = false, max = 0 : i64, min = 0 : i64}, {is_unbounded = false, max = 2 : i64, min = 1 : i64}], node_id = 246 : i64} {
                  obelisk.sv.assertion.simple attributes {has_repetition = false, is_null = false, node_id = 247 : i64, repetition_is_unbounded = false} {
                    obelisk.sv.expression.named_value attributes {node_id = 248 : i64, referenced_path = "top.a", referenced_symbol = @s1.$root::@s3.top::@s4.top::@s6.a, semantic_type = !obelisk.integral<1, false, true, 0 : 0, logic>} {
                    }
                  }
                  obelisk.sv.assertion.simple attributes {has_repetition = false, is_null = false, node_id = 249 : i64, repetition_is_unbounded = false} {
                    obelisk.sv.expression.named_value attributes {node_id = 250 : i64, referenced_path = "top.b", referenced_symbol = @s1.$root::@s3.top::@s4.top::@s14.b, semantic_type = !obelisk.integral<1, false, true, 0 : 0, logic>} {
                    }
                  }
                }
                obelisk.sv.assertion.simple attributes {has_repetition = false, is_null = false, node_id = 251 : i64, repetition_is_unbounded = false} {
                  obelisk.sv.expression.named_value attributes {node_id = 252 : i64, referenced_path = "top.a", referenced_symbol = @s1.$root::@s3.top::@s4.top::@s6.a, semantic_type = !obelisk.integral<1, false, true, 0 : 0, logic>} {
                  }
                }
              }
            }
            obelisk.sv.statement.expression_statement attributes {node_id = 253 : i64} {
              obelisk.sv.expression.call attributes {argument_count = 0 : i64, callee_name = "$display", constraint_restrictions = [], defaulted_arguments = array<i64>, has_inline_constraints = false, has_iterator_expression = false, has_output_arguments = false, has_this_class = false, is_signed = false, is_super_class = false, is_system_call = true, node_id = 254 : i64, semantic_type = !obelisk.void, subroutine_kind = 1 : i32, system_library_cell = "work.top", system_scope_path = "top", system_scope_symbol = @s1.$root::@s3.top::@s4.top} {
              }
            }
          }
        }
        // assert property ((a ##[1:2] b) #=# a). At EOS, an already matched
        // source with its weak nonoverlapped consequent pending succeeds;
        // a source with only an unfinished antecedent fails existentially.
        obelisk.sv.symbol.procedural_block attributes {hierarchical_name = "top", node_id = 260 : i64, procedure_kind = 2 : i32, sym_name = "s260", time_precision_fs = 1000000 : i64, time_unit_fs = 1000000 : i64} {
          obelisk.sv.statement.concurrent_assertion attributes {assertion_kind = 0 : i32, has_default_disable = false, has_fail_action = false, has_pass_action = true, node_id = 261 : i64} {
            obelisk.sv.assertion.clocking attributes {node_id = 262 : i64} {
              obelisk.sv.timing.signal_event attributes {edge_kind = 1 : i32, has_iff = false, node_id = 263 : i64} {
                obelisk.sv.expression.named_value attributes {node_id = 264 : i64, referenced_path = "top.clk", referenced_symbol = @s1.$root::@s3.top::@s4.top::@s5.clk, semantic_type = !obelisk.integral<1, false, true, 0 : 0, logic>} {
                }
              }
              obelisk.sv.assertion.binary attributes {node_id = 265 : i64, operator_kind = 14 : i32} {
                obelisk.sv.assertion.sequence_concat attributes {delays = [{is_unbounded = false, max = 0 : i64, min = 0 : i64}, {is_unbounded = false, max = 2 : i64, min = 1 : i64}], node_id = 266 : i64} {
                  obelisk.sv.assertion.simple attributes {has_repetition = false, is_null = false, node_id = 267 : i64, repetition_is_unbounded = false} {
                    obelisk.sv.expression.named_value attributes {node_id = 268 : i64, referenced_path = "top.a", referenced_symbol = @s1.$root::@s3.top::@s4.top::@s6.a, semantic_type = !obelisk.integral<1, false, true, 0 : 0, logic>} {
                    }
                  }
                  obelisk.sv.assertion.simple attributes {has_repetition = false, is_null = false, node_id = 269 : i64, repetition_is_unbounded = false} {
                    obelisk.sv.expression.named_value attributes {node_id = 270 : i64, referenced_path = "top.b", referenced_symbol = @s1.$root::@s3.top::@s4.top::@s14.b, semantic_type = !obelisk.integral<1, false, true, 0 : 0, logic>} {
                    }
                  }
                }
                obelisk.sv.assertion.simple attributes {has_repetition = false, is_null = false, node_id = 271 : i64, repetition_is_unbounded = false} {
                  obelisk.sv.expression.named_value attributes {node_id = 272 : i64, referenced_path = "top.a", referenced_symbol = @s1.$root::@s3.top::@s4.top::@s6.a, semantic_type = !obelisk.integral<1, false, true, 0 : 0, logic>} {
                  }
                }
              }
            }
            obelisk.sv.statement.expression_statement attributes {node_id = 273 : i64} {
              obelisk.sv.expression.call attributes {argument_count = 0 : i64, callee_name = "$display", constraint_restrictions = [], defaulted_arguments = array<i64>, has_inline_constraints = false, has_iterator_expression = false, has_output_arguments = false, has_this_class = false, is_signed = false, is_super_class = false, is_system_call = true, node_id = 274 : i64, semantic_type = !obelisk.void, subroutine_kind = 1 : i32, system_library_cell = "work.top", system_scope_path = "top", system_scope_symbol = @s1.$root::@s3.top::@s4.top} {
              }
            }
          }
        }
        // Intrinsically strong s_nexttime overrides assert's default weak
        // sequence completion for a pending implication consequent.
        obelisk.sv.symbol.procedural_block attributes {hierarchical_name = "top", node_id = 280 : i64, procedure_kind = 2 : i32, sym_name = "s280", time_precision_fs = 1000000 : i64, time_unit_fs = 1000000 : i64} {
          obelisk.sv.statement.concurrent_assertion attributes {assertion_kind = 0 : i32, has_default_disable = false, has_fail_action = false, has_pass_action = true, node_id = 281 : i64} {
            obelisk.sv.assertion.clocking attributes {node_id = 282 : i64} {
              obelisk.sv.timing.signal_event attributes {edge_kind = 1 : i32, has_iff = false, node_id = 283 : i64} {
                obelisk.sv.expression.named_value attributes {node_id = 284 : i64, referenced_path = "top.clk", referenced_symbol = @s1.$root::@s3.top::@s4.top::@s5.clk, semantic_type = !obelisk.integral<1, false, true, 0 : 0, logic>} {
                }
              }
              obelisk.sv.assertion.binary attributes {node_id = 285 : i64, operator_kind = 11 : i32} {
                obelisk.sv.assertion.sequence_concat attributes {delays = [{is_unbounded = false, max = 0 : i64, min = 0 : i64}, {is_unbounded = false, max = 2 : i64, min = 1 : i64}], node_id = 286 : i64} {
                  obelisk.sv.assertion.simple attributes {has_repetition = false, is_null = false, node_id = 287 : i64, repetition_is_unbounded = false} {
                    obelisk.sv.expression.named_value attributes {node_id = 288 : i64, referenced_path = "top.a", referenced_symbol = @s1.$root::@s3.top::@s4.top::@s6.a, semantic_type = !obelisk.integral<1, false, true, 0 : 0, logic>} {
                    }
                  }
                  obelisk.sv.assertion.simple attributes {has_repetition = false, is_null = false, node_id = 289 : i64, repetition_is_unbounded = false} {
                    obelisk.sv.expression.named_value attributes {node_id = 290 : i64, referenced_path = "top.b", referenced_symbol = @s1.$root::@s3.top::@s4.top::@s14.b, semantic_type = !obelisk.integral<1, false, true, 0 : 0, logic>} {
                    }
                  }
                }
                obelisk.sv.assertion.unary attributes {has_range = false, node_id = 291 : i64, operator_kind = 2 : i32, range_is_unbounded = false} {
                  obelisk.sv.assertion.simple attributes {has_repetition = false, is_null = false, node_id = 292 : i64, repetition_is_unbounded = false} {
                    obelisk.sv.expression.named_value attributes {node_id = 293 : i64, referenced_path = "top.a", referenced_symbol = @s1.$root::@s3.top::@s4.top::@s6.a, semantic_type = !obelisk.integral<1, false, true, 0 : 0, logic>} {
                    }
                  }
                }
              }
            }
            obelisk.sv.statement.expression_statement attributes {node_id = 294 : i64} {
              obelisk.sv.expression.call attributes {argument_count = 0 : i64, callee_name = "$display", constraint_restrictions = [], defaulted_arguments = array<i64>, has_inline_constraints = false, has_iterator_expression = false, has_output_arguments = false, has_this_class = false, is_signed = false, is_super_class = false, is_system_call = true, node_id = 295 : i64, semantic_type = !obelisk.void, subroutine_kind = 1 : i32, system_library_cell = "work.top", system_scope_path = "top", system_scope_symbol = @s1.$root::@s3.top::@s4.top} {
              }
            }
          }
        }
        // Intrinsically weak nexttime overrides cover property's default
        // strong sequence completion for a pending implication consequent.
        obelisk.sv.symbol.procedural_block attributes {hierarchical_name = "top", node_id = 300 : i64, procedure_kind = 2 : i32, sym_name = "s300", time_precision_fs = 1000000 : i64, time_unit_fs = 1000000 : i64} {
          obelisk.sv.statement.concurrent_assertion attributes {assertion_kind = 2 : i32, has_default_disable = false, has_fail_action = false, has_pass_action = true, node_id = 301 : i64} {
            obelisk.sv.assertion.clocking attributes {node_id = 302 : i64} {
              obelisk.sv.timing.signal_event attributes {edge_kind = 1 : i32, has_iff = false, node_id = 303 : i64} {
                obelisk.sv.expression.named_value attributes {node_id = 304 : i64, referenced_path = "top.clk", referenced_symbol = @s1.$root::@s3.top::@s4.top::@s5.clk, semantic_type = !obelisk.integral<1, false, true, 0 : 0, logic>} {
                }
              }
              obelisk.sv.assertion.binary attributes {node_id = 305 : i64, operator_kind = 11 : i32} {
                obelisk.sv.assertion.sequence_concat attributes {delays = [{is_unbounded = false, max = 0 : i64, min = 0 : i64}, {is_unbounded = false, max = 2 : i64, min = 1 : i64}], node_id = 306 : i64} {
                  obelisk.sv.assertion.simple attributes {has_repetition = false, is_null = false, node_id = 307 : i64, repetition_is_unbounded = false} {
                    obelisk.sv.expression.named_value attributes {node_id = 308 : i64, referenced_path = "top.a", referenced_symbol = @s1.$root::@s3.top::@s4.top::@s6.a, semantic_type = !obelisk.integral<1, false, true, 0 : 0, logic>} {
                    }
                  }
                  obelisk.sv.assertion.simple attributes {has_repetition = false, is_null = false, node_id = 309 : i64, repetition_is_unbounded = false} {
                    obelisk.sv.expression.named_value attributes {node_id = 310 : i64, referenced_path = "top.b", referenced_symbol = @s1.$root::@s3.top::@s4.top::@s14.b, semantic_type = !obelisk.integral<1, false, true, 0 : 0, logic>} {
                    }
                  }
                }
                obelisk.sv.assertion.unary attributes {has_range = false, node_id = 311 : i64, operator_kind = 1 : i32, range_is_unbounded = false} {
                  obelisk.sv.assertion.simple attributes {has_repetition = false, is_null = false, node_id = 312 : i64, repetition_is_unbounded = false} {
                    obelisk.sv.expression.named_value attributes {node_id = 313 : i64, referenced_path = "top.a", referenced_symbol = @s1.$root::@s3.top::@s4.top::@s6.a, semantic_type = !obelisk.integral<1, false, true, 0 : 0, logic>} {
                    }
                  }
                }
              }
            }
            obelisk.sv.statement.expression_statement attributes {node_id = 314 : i64} {
              obelisk.sv.expression.call attributes {argument_count = 0 : i64, callee_name = "$display", constraint_restrictions = [], defaulted_arguments = array<i64>, has_inline_constraints = false, has_iterator_expression = false, has_output_arguments = false, has_this_class = false, is_signed = false, is_super_class = false, is_system_call = true, node_id = 315 : i64, semantic_type = !obelisk.void, subroutine_kind = 1 : i32, system_library_cell = "work.top", system_scope_path = "top", system_scope_symbol = @s1.$root::@s3.top::@s4.top} {
              }
            }
          }
        }
      }
    }
  }
}

// [*1:3] expands into the three exact traces a, a##1a, and a##1a##1a.
// The monitor shares one sampled read and keeps distinct endpoint state for
// all three alternatives.
// CHECK-LABEL: obelisk_sim.func private @unit_0(
// CHECK-SAME: home_region = 8 : i32
// CHECK-SAME: obelisk_sim.branching_sequence_alternatives = 3 : i64
// CHECK-SAME: obelisk_sim.branching_sequence_monitor
// CHECK: obelisk_sim.suspend.edge posedge
// CHECK-SAME: resume_region = 8 : i32
// CHECK-COUNT-1: obelisk_sim.assert.sampled_read
// CHECK: arith.select
// CHECK: cf.cond_br

// `a within b[*3]` has three exact placements for a while preserving all
// three required b samples.
// CHECK-LABEL: obelisk_sim.func private @unit_1(
// CHECK-SAME: home_region = 8 : i32
// CHECK-SAME: obelisk_sim.branching_sequence_alternatives = 3 : i64
// CHECK-SAME: obelisk_sim.branching_sequence_monitor
// CHECK: obelisk_sim.suspend.edge posedge
// CHECK-SAME: resume_region = 8 : i32
// CHECK-COUNT-2: obelisk_sim.assert.sampled_read
// CHECK: arith.select
// CHECK: cf.cond_br

// Top-level first_match shares the same bounded alternatives but records that
// later live endpoints are suppressed after the first successful endpoint.
// CHECK-LABEL: obelisk_sim.func private @unit_2(
// CHECK-SAME: home_region = 8 : i32
// CHECK-SAME: obelisk_sim.branching_sequence_alternatives = 3 : i64
// CHECK-SAME: obelisk_sim.branching_sequence_monitor
// CHECK-SAME: obelisk_sim.first_match_monitor
// CHECK-SAME: obelisk_sim.first_match_priority_groups = 1 : i64
// CHECK: obelisk_sim.suspend.edge posedge
// CHECK-SAME: resume_region = 8 : i32
// CHECK-COUNT-2: obelisk_sim.assert.sampled_read
// CHECK: arith.select
// CHECK: cf.cond_br

// A first_match nested before ##1 records the sub-sequence endpoint on every
// trace. The surviving priority gate cancels the later candidate when the
// earlier endpoint succeeds, while tautological gates fold away.
// CHECK-LABEL: obelisk_sim.func private @unit_3(
// CHECK-SAME: home_region = 8 : i32
// CHECK-SAME: obelisk_sim.branching_sequence_alternatives = 2 : i64
// CHECK-SAME: obelisk_sim.branching_sequence_monitor
// CHECK-SAME: obelisk_sim.first_match_monitor
// CHECK-SAME: obelisk_sim.first_match_priority_groups = 1 : i64
// CHECK: obelisk_sim.suspend.edge posedge
// CHECK-SAME: resume_region = 8 : i32
// CHECK: arith.andi {{.*}}obelisk_sim.first_match_priority
// CHECK: cf.cond_br

// A deterministic two-cycle antecedent uses the same source-age coalescer as
// a branching antecedent. At EOS, an unfinished antecedent is a vacuous
// implication success and a pending weak consequent is also a success. The
// two source ages therefore dispatch exactly two pass actions, oldest first.
// CHECK-LABEL: obelisk_sim.func private @unit_4.$concurrent_eos_branch_report.51.pass(
// CHECK-SAME: obelisk_sim.branching_antecedent_eos_report
// CHECK-SAME: obelisk_sim.concurrent_eos_report
// CHECK-LABEL: obelisk_sim.func private @unit_4.$concurrent_eos_branch.51(
// CHECK-SAME: obelisk_sim.branching_antecedent_eos_coalescer
// CHECK-COUNT-3: obelisk_sim.ref.load
// CHECK-NOT: obelisk_sim.ref.load
// CHECK: obelisk_sim.spawn @unit_4.$concurrent_eos_branch_report.51.pass{{.*}}obelisk_sim.branching_antecedent_eos_source_age = 2 : i64
// CHECK: obelisk_sim.spawn @unit_4.$concurrent_eos_branch_report.51.pass{{.*}}obelisk_sim.branching_antecedent_eos_source_age = 1 : i64
// CHECK-NOT: obelisk_sim.spawn @unit_4.$concurrent_eos_branch_report.51.pass
// CHECK-LABEL: obelisk_sim.func private @unit_4(
// CHECK-SAME: home_region = 8 : i32
// CHECK-SAME: obelisk_sim.bounded_antecedent_horizon = 2 : i64
// CHECK-SAME: obelisk_sim.branching_antecedent_alternatives = 1 : i64
// CHECK-SAME: obelisk_sim.branching_antecedent_consequent_eos_strength = "weak"
// CHECK-SAME: obelisk_sim.branching_antecedent_eos_coalescer
// CHECK-SAME: obelisk_sim.branching_antecedent_match_channels = 1 : i64
// CHECK-SAME: obelisk_sim.branching_antecedent_result_horizon = 3 : i64
// CHECK-COUNT-3: obelisk_sim.ref.alloc
// CHECK-NOT: obelisk_sim.ref.alloc
// CHECK: obelisk_sim.spawn @unit_4.$concurrent_eos_branch.51
// CHECK: obelisk_sim.branching_antecedent_vacuity
// CHECK: obelisk_sim.branching_antecedent_universal_failure
// CHECK: obelisk_sim.branching_antecedent_universal_success
// CHECK: obelisk_sim.branching_antecedent_matched_history
// CHECK: obelisk_sim.branching_antecedent_result_cancel
// CHECK: cf.br {{.*}}obelisk_sim.branching_antecedent_backedge

// The nonoverlapped followed-by form keeps a distinct consequent bit-0
// handoff. At EOS that weak pending consequence passes, while an unfinished
// antecedent has produced no match and fails. The age-two decision retains a
// mutually exclusive failure path for matched history without a pending weak
// consequence.
// CHECK-LABEL: obelisk_sim.func private @unit_5.$concurrent_eos_branch_report.67.pass(
// CHECK-SAME: obelisk_sim.branching_antecedent_eos_report
// CHECK-LABEL: obelisk_sim.func private @unit_5.$concurrent_eos_branch_report.67.fail(
// CHECK-SAME: obelisk_sim.branching_antecedent_eos_report
// CHECK-LABEL: obelisk_sim.func private @unit_5.$concurrent_eos_branch.67(
// CHECK-SAME: obelisk_sim.branching_antecedent_eos_coalescer
// CHECK: [[ONE:%.*]] = arith.constant 1 : i64
// CHECK-COUNT-3: obelisk_sim.ref.load
// CHECK-NOT: obelisk_sim.ref.load
// CHECK: [[HANDOFF:%.*]] = arith.andi {{%.*}}, [[ONE]] : i64
// CHECK: obelisk_sim.spawn @unit_5.$concurrent_eos_branch_report.67.pass{{.*}}obelisk_sim.branching_antecedent_eos_source_age = 2 : i64
// CHECK: obelisk_sim.spawn @unit_5.$concurrent_eos_branch_report.67.fail{{.*}}obelisk_sim.branching_antecedent_eos_source_age = 2 : i64
// CHECK: obelisk_sim.spawn @unit_5.$concurrent_eos_branch_report.67.fail{{.*}}obelisk_sim.branching_antecedent_eos_source_age = 1 : i64
// CHECK-NOT: obelisk_sim.spawn @unit_5.$concurrent_eos_branch_report.67.
// CHECK-LABEL: obelisk_sim.func private @unit_5(
// CHECK-SAME: home_region = 8 : i32
// CHECK-SAME: obelisk_sim.bounded_antecedent_horizon = 2 : i64
// CHECK-SAME: obelisk_sim.branching_antecedent_alternatives = 1 : i64
// CHECK-SAME: obelisk_sim.branching_antecedent_consequent_eos_strength = "weak"
// CHECK-SAME: obelisk_sim.branching_antecedent_eos_coalescer
// CHECK-SAME: obelisk_sim.branching_antecedent_match_channels = 1 : i64
// CHECK-SAME: obelisk_sim.branching_antecedent_result_horizon = 3 : i64
// CHECK-SAME: obelisk_sim.followed_by_monitor
// CHECK-COUNT-3: obelisk_sim.ref.alloc
// CHECK-NOT: obelisk_sim.ref.alloc
// CHECK: obelisk_sim.spawn @unit_5.$concurrent_eos_branch.67
// CHECK: arith.extui
// CHECK: obelisk_sim.branching_antecedent_existential_failure
// CHECK: obelisk_sim.branching_antecedent_matched_history
// CHECK: obelisk_sim.branching_antecedent_existential_success
// CHECK: obelisk_sim.branching_antecedent_result_cancel
// CHECK: cf.br {{.*}}obelisk_sim.branching_antecedent_backedge

// Nested first_match scopes retain independent inner and outer priority
// groups across all four exact endpoint combinations.
// CHECK-LABEL: obelisk_sim.func private @unit_6(
// CHECK-SAME: obelisk_sim.branching_sequence_alternatives = 4 : i64
// CHECK-SAME: obelisk_sim.first_match_monitor
// CHECK-SAME: obelisk_sim.first_match_priority_groups = 2 : i64
// CHECK-COUNT-10: obelisk_sim.first_match_priority}

// Two first_match terms concatenated at distinct syntax sites retain one
// scope for the first and two independently activated scopes for the second.
// Success in one activation cannot cancel another activation.
// CHECK-LABEL: obelisk_sim.func private @unit_7(
// CHECK-SAME: obelisk_sim.branching_sequence_alternatives = 4 : i64
// CHECK-SAME: obelisk_sim.first_match_monitor
// CHECK-SAME: obelisk_sim.first_match_priority_groups = 3 : i64
// CHECK-COUNT-8: obelisk_sim.first_match_priority}

// A ranged prefix starts the same suffix first_match twice. Those activations
// must have separate priority state so an early completion whose continuation
// fails cannot suppress the later activation and its continuation.
// CHECK-LABEL: obelisk_sim.func private @unit_8(
// CHECK-SAME: obelisk_sim.branching_sequence_alternatives = 4 : i64
// CHECK-SAME: obelisk_sim.first_match_monitor
// CHECK-SAME: obelisk_sim.first_match_priority_groups = 2 : i64

// Each finite eventually offset starts an independent operand activation.
// CHECK-LABEL: obelisk_sim.func private @unit_9(
// CHECK-SAME: obelisk_sim.branching_sequence_alternatives = 4 : i64
// CHECK-SAME: obelisk_sim.first_match_monitor
// CHECK-SAME: obelisk_sim.first_match_priority_groups = 2 : i64

// Finite always likewise keeps each required operand activation independent.
// CHECK-LABEL: obelisk_sim.func private @unit_10(
// CHECK-SAME: obelisk_sim.branching_sequence_alternatives = 4 : i64
// CHECK-SAME: obelisk_sim.first_match_monitor
// CHECK-SAME: obelisk_sim.first_match_priority_groups = 2 : i64

// A ranged antecedent retains both exact traces and gives each trace its own
// compact consequent channel. The source-age coalescer delays success until
// every later antecedent endpoint and consequent obligation has resolved,
// reports the first universal failure once, and cancels all remaining state
// for that source attempt. With a four-age result horizon this produces seven
// static result sites (three fail plus four pass), rather than one site per
// channel at each consequence endpoint.
// CHECK: obelisk_sim.func private @[[RANGE_PASS:unit_11\.fork\.201\.0\.0]](
// CHECK: obelisk_sim.func private @[[RANGE_FAIL:unit_11\.fork\.201\.1\.2]](
// CHECK-LABEL: obelisk_sim.func private @unit_11(
// CHECK-SAME: obelisk_sim.bounded_antecedent_horizon = 3 : i64
// CHECK-SAME: obelisk_sim.branching_antecedent_alternatives = 2 : i64
// CHECK-SAME: obelisk_sim.branching_antecedent_match_channels = 2 : i64
// CHECK-SAME: obelisk_sim.branching_antecedent_monitor
// CHECK-SAME: obelisk_sim.branching_antecedent_result_coalescer
// CHECK-SAME: obelisk_sim.branching_antecedent_result_horizon = 4 : i64
// CHECK: arith.andi {{.*}}obelisk_sim.branching_antecedent_channel = 0 : i64{{.*}}obelisk_sim.branching_antecedent_consequent_trigger
// CHECK: arith.andi {{.*}}obelisk_sim.branching_antecedent_channel = 1 : i64{{.*}}obelisk_sim.branching_antecedent_consequent_trigger
// CHECK: arith.select {{.*}}obelisk_sim.branching_antecedent_matched_history
// CHECK: arith.andi {{.*}}obelisk_sim.branching_antecedent_vacuity
// CHECK: arith.andi {{.*}}obelisk_sim.branching_antecedent_universal_failure
// CHECK: arith.ori {{.*}}obelisk_sim.branching_antecedent_universal_success
// CHECK-DAG: obelisk_sim.spawn @[[RANGE_FAIL]]
// CHECK-DAG: obelisk_sim.spawn @[[RANGE_FAIL]]
// CHECK-DAG: obelisk_sim.spawn @[[RANGE_FAIL]]
// CHECK-DAG: obelisk_sim.spawn @[[RANGE_PASS]]
// CHECK-DAG: obelisk_sim.spawn @[[RANGE_PASS]]
// CHECK-DAG: obelisk_sim.spawn @[[RANGE_PASS]]
// CHECK-DAG: obelisk_sim.spawn @[[RANGE_PASS]]
// CHECK-NOT: obelisk_sim.spawn @unit_11.fork.201.
// CHECK: obelisk_sim.branching_antecedent_result_cancel
// CHECK: cf.br {{.*}}obelisk_sim.branching_antecedent_backedge

// Same-endpoint `or` alternatives feed one existential followed-by result.
// Either successful consequent match executes the single pass callback; only
// exhaustion without either success executes the single default failure.
// CHECK: obelisk_sim.func private @[[FOLLOW_PASS:unit_12\.fork\.221\.0\.0]](
// CHECK: obelisk_sim.display
// CHECK: obelisk_sim.func private @[[FOLLOW_FAIL:unit_12\.fork\.221\.1\.2]](
// CHECK: obelisk_sim.bytes.constant "ERROR:
// CHECK-LABEL: obelisk_sim.func private @unit_12(
// CHECK-SAME: obelisk_sim.branching_antecedent_alternatives = 2 : i64
// CHECK-SAME: obelisk_sim.branching_antecedent_match_channels = 2 : i64
// CHECK-SAME: obelisk_sim.branching_antecedent_monitor
// CHECK-SAME: obelisk_sim.branching_antecedent_result_coalescer
// CHECK-SAME: obelisk_sim.branching_antecedent_result_horizon = 1 : i64
// CHECK-SAME: obelisk_sim.followed_by_monitor
// CHECK: [[A_SAMPLE:%.*]] = obelisk_sim.assert.sampled_read %arg0 from %arg2
// CHECK: [[A_MATCH:%.*]] = obelisk_sim.logic.is_true [[A_SAMPLE]]
// CHECK: [[B_SAMPLE:%.*]] = obelisk_sim.assert.sampled_read %arg0 from %arg3
// CHECK: [[B_MATCH:%.*]] = obelisk_sim.logic.is_true [[B_SAMPLE]]
// CHECK: [[ANTECEDENT_ANY:%.*]] = arith.ori [[A_MATCH]], [[B_MATCH]]
// CHECK: arith.andi {{.*}}obelisk_sim.branching_antecedent_consequent_trigger
// CHECK: [[CONSEQUENT_ANY:%.*]] = arith.ori
// CHECK: [[FOLLOW_FAILED:%.*]] = arith.ori {{.*}} {obelisk_sim.branching_antecedent_existential_failure}
// CHECK: cf.cond_br [[FOLLOW_FAILED]],
// CHECK: obelisk_sim.spawn @[[FOLLOW_FAIL]]
// CHECK: cf.cond_br [[CONSEQUENT_ANY]],
// CHECK: obelisk_sim.spawn @[[FOLLOW_PASS]]
// CHECK-NOT: obelisk_sim.spawn @unit_12.fork.221.
// CHECK: cf.br {{.*}}obelisk_sim.branching_antecedent_backedge

// The strong cover consequent has no pending state, but the two ranged
// antecedent traces and their matched history do. At EOS the source-age
// coordinator unions those three words and emits one vacuous cover pass for
// each of the two still-live source ages, oldest first.
// CHECK-LABEL: obelisk_sim.func private @unit_13.$concurrent_eos_branch_report.241.pass(
// CHECK-SAME: obelisk_sim.branching_antecedent_eos_report
// CHECK: obelisk_sim.display
// CHECK-LABEL: obelisk_sim.func private @unit_13.$concurrent_eos_branch.241(
// CHECK-SAME: %arg1: !obelisk_sim.ref<i64>
// CHECK-SAME: %arg2: !obelisk_sim.ref<i64>
// CHECK-SAME: %arg3: !obelisk_sim.ref<i64>
// CHECK-SAME: obelisk_sim.branching_antecedent_eos_coalescer
// CHECK-COUNT-3: obelisk_sim.ref.load
// CHECK-NOT: obelisk_sim.ref.load
// CHECK: obelisk_sim.spawn @unit_13.$concurrent_eos_branch_report.241.pass{{.*}}obelisk_sim.branching_antecedent_eos_result = "pass"{{.*}}obelisk_sim.branching_antecedent_eos_source_age = 2 : i64
// CHECK: obelisk_sim.spawn @unit_13.$concurrent_eos_branch_report.241.pass{{.*}}obelisk_sim.branching_antecedent_eos_result = "pass"{{.*}}obelisk_sim.branching_antecedent_eos_source_age = 1 : i64
// CHECK-NOT: obelisk_sim.spawn @unit_13.$concurrent_eos_branch_report.241.pass
// CHECK-NOT: @unit_13.$concurrent_eos_branch_report.241.fail
// CHECK-LABEL: obelisk_sim.func private @unit_13(
// CHECK-SAME: obelisk_sim.branching_antecedent_consequent_eos_strength = "strong"
// CHECK-SAME: obelisk_sim.branching_antecedent_eos_coalescer
// CHECK-SAME: obelisk_sim.branching_antecedent_result_horizon = 3 : i64
// CHECK-COUNT-3: obelisk_sim.ref.alloc
// CHECK-NOT: obelisk_sim.ref.alloc
// CHECK: obelisk_sim.spawn @unit_13.$concurrent_eos_branch.241

// Nonoverlapped followed-by owns two consequent handoff channels in addition
// to its two ranged-antecedent words and matched history. The weak EOS
// coordinator selects pass only where a consequent is pending; antecedent-only
// ages select the one default failure, with no per-channel duplication.
// CHECK-LABEL: obelisk_sim.func private @unit_14.$concurrent_eos_branch_report.261.pass(
// CHECK-SAME: obelisk_sim.branching_antecedent_eos_report
// CHECK: obelisk_sim.display
// CHECK-LABEL: obelisk_sim.func private @unit_14.$concurrent_eos_branch_report.261.fail(
// CHECK-SAME: obelisk_sim.branching_antecedent_eos_report
// CHECK: obelisk_sim.bytes.constant "ERROR:
// CHECK-LABEL: obelisk_sim.func private @unit_14.$concurrent_eos_branch.261(
// CHECK-SAME: obelisk_sim.branching_antecedent_eos_coalescer
// CHECK-COUNT-5: obelisk_sim.ref.load
// CHECK-NOT: obelisk_sim.ref.load
// CHECK: obelisk_sim.spawn @unit_14.$concurrent_eos_branch_report.261.pass{{.*}}obelisk_sim.branching_antecedent_eos_result = "pass"{{.*}}obelisk_sim.branching_antecedent_eos_source_age = 3 : i64
// CHECK: obelisk_sim.spawn @unit_14.$concurrent_eos_branch_report.261.fail{{.*}}obelisk_sim.branching_antecedent_eos_result = "fail"{{.*}}obelisk_sim.branching_antecedent_eos_source_age = 3 : i64
// CHECK: obelisk_sim.spawn @unit_14.$concurrent_eos_branch_report.261.pass{{.*}}obelisk_sim.branching_antecedent_eos_result = "pass"{{.*}}obelisk_sim.branching_antecedent_eos_source_age = 2 : i64
// CHECK: obelisk_sim.spawn @unit_14.$concurrent_eos_branch_report.261.fail{{.*}}obelisk_sim.branching_antecedent_eos_result = "fail"{{.*}}obelisk_sim.branching_antecedent_eos_source_age = 2 : i64
// CHECK: obelisk_sim.spawn @unit_14.$concurrent_eos_branch_report.261.fail{{.*}}obelisk_sim.branching_antecedent_eos_result = "fail"{{.*}}obelisk_sim.branching_antecedent_eos_source_age = 1 : i64
// CHECK-NOT: obelisk_sim.spawn @unit_14.$concurrent_eos_branch_report.261.
// CHECK-LABEL: obelisk_sim.func private @unit_14(
// CHECK-SAME: obelisk_sim.branching_antecedent_consequent_eos_strength = "weak"
// CHECK-SAME: obelisk_sim.branching_antecedent_eos_coalescer
// CHECK-SAME: obelisk_sim.branching_antecedent_result_horizon = 4 : i64
// CHECK-SAME: obelisk_sim.followed_by_monitor
// CHECK-COUNT-5: obelisk_sim.ref.alloc
// CHECK-NOT: obelisk_sim.ref.alloc
// CHECK: obelisk_sim.spawn @unit_14.$concurrent_eos_branch.261

// An intrinsically strong s_nexttime consequent overrides assert property's
// default weak sequence completion. At EOS, pending consequents fail while
// sources with only an unfinished antecedent still pass vacuously, once per
// source age and oldest first.
// CHECK-LABEL: obelisk_sim.func private @unit_15.$concurrent_eos_branch_report.281.pass(
// CHECK-SAME: obelisk_sim.branching_antecedent_eos_report
// CHECK-LABEL: obelisk_sim.func private @unit_15.$concurrent_eos_branch_report.281.fail(
// CHECK-SAME: obelisk_sim.branching_antecedent_eos_report
// CHECK-LABEL: obelisk_sim.func private @unit_15.$concurrent_eos_branch.281(
// CHECK-SAME: obelisk_sim.branching_antecedent_eos_coalescer
// CHECK-COUNT-5: obelisk_sim.ref.load
// CHECK-NOT: obelisk_sim.ref.load
// CHECK: obelisk_sim.spawn @unit_15.$concurrent_eos_branch_report.281.pass{{.*}}obelisk_sim.branching_antecedent_eos_result = "pass"{{.*}}obelisk_sim.branching_antecedent_eos_source_age = 3 : i64
// CHECK: obelisk_sim.spawn @unit_15.$concurrent_eos_branch_report.281.fail{{.*}}obelisk_sim.branching_antecedent_eos_result = "fail"{{.*}}obelisk_sim.branching_antecedent_eos_source_age = 3 : i64
// CHECK: obelisk_sim.spawn @unit_15.$concurrent_eos_branch_report.281.pass{{.*}}obelisk_sim.branching_antecedent_eos_result = "pass"{{.*}}obelisk_sim.branching_antecedent_eos_source_age = 2 : i64
// CHECK: obelisk_sim.spawn @unit_15.$concurrent_eos_branch_report.281.fail{{.*}}obelisk_sim.branching_antecedent_eos_result = "fail"{{.*}}obelisk_sim.branching_antecedent_eos_source_age = 2 : i64
// CHECK: obelisk_sim.spawn @unit_15.$concurrent_eos_branch_report.281.pass{{.*}}obelisk_sim.branching_antecedent_eos_result = "pass"{{.*}}obelisk_sim.branching_antecedent_eos_source_age = 1 : i64
// CHECK-NOT: obelisk_sim.spawn @unit_15.$concurrent_eos_branch_report.281.
// CHECK-LABEL: obelisk_sim.func private @unit_15(
// CHECK-SAME: obelisk_sim.branching_antecedent_consequent_eos_strength = "strong"
// CHECK-SAME: obelisk_sim.branching_antecedent_eos_coalescer
// CHECK-SAME: obelisk_sim.branching_antecedent_result_horizon = 4 : i64
// CHECK-COUNT-5: obelisk_sim.ref.alloc
// CHECK-NOT: obelisk_sim.ref.alloc
// CHECK: obelisk_sim.spawn @unit_15.$concurrent_eos_branch.281

// Conversely, intrinsically weak nexttime overrides cover property's default
// strong sequence completion. Every active source age succeeds at EOS, and
// no failure report actor or dispatch is emitted.
// CHECK-LABEL: obelisk_sim.func private @unit_16.$concurrent_eos_branch_report.301.pass(
// CHECK-SAME: obelisk_sim.branching_antecedent_eos_report
// CHECK-NOT: @unit_16.$concurrent_eos_branch_report.301.fail
// CHECK-LABEL: obelisk_sim.func private @unit_16.$concurrent_eos_branch.301(
// CHECK-SAME: obelisk_sim.branching_antecedent_eos_coalescer
// CHECK-COUNT-5: obelisk_sim.ref.load
// CHECK-NOT: obelisk_sim.ref.load
// CHECK: obelisk_sim.spawn @unit_16.$concurrent_eos_branch_report.301.pass{{.*}}obelisk_sim.branching_antecedent_eos_result = "pass"{{.*}}obelisk_sim.branching_antecedent_eos_source_age = 3 : i64
// CHECK: obelisk_sim.spawn @unit_16.$concurrent_eos_branch_report.301.pass{{.*}}obelisk_sim.branching_antecedent_eos_result = "pass"{{.*}}obelisk_sim.branching_antecedent_eos_source_age = 2 : i64
// CHECK: obelisk_sim.spawn @unit_16.$concurrent_eos_branch_report.301.pass{{.*}}obelisk_sim.branching_antecedent_eos_result = "pass"{{.*}}obelisk_sim.branching_antecedent_eos_source_age = 1 : i64
// CHECK-NOT: obelisk_sim.spawn @unit_16.$concurrent_eos_branch_report.301.
// CHECK-LABEL: obelisk_sim.func private @unit_16(
// CHECK-SAME: obelisk_sim.branching_antecedent_consequent_eos_strength = "weak"
// CHECK-SAME: obelisk_sim.branching_antecedent_eos_coalescer
// CHECK-SAME: obelisk_sim.branching_antecedent_result_horizon = 4 : i64
// CHECK-COUNT-5: obelisk_sim.ref.alloc
// CHECK-NOT: obelisk_sim.ref.alloc
// CHECK: obelisk_sim.spawn @unit_16.$concurrent_eos_branch.301
// CHECK-NOT: obelisk.sv.assertion
