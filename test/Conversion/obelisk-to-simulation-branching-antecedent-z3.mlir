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
        obelisk.sv.symbol.procedural_block attributes {hierarchical_name = "top", node_id = 9 : i64, procedure_kind = 2 : i32, sym_name = "s9", time_precision_fs = 1000000 : i64, time_unit_fs = 1000000 : i64} {
          // The antecedent consensus cube b&&c is redundant in
          // (a&&c)||(!a&&b)||(b&&c), but neither duplicate normalization nor
          // ordinary cube subsumption can remove it. Z3 proves the two-cube
          // equivalent before the source-attempt coalescer is materialized.
          obelisk.sv.statement.concurrent_assertion attributes {assertion_kind = 0 : i32, has_default_disable = false, has_fail_action = false, has_pass_action = false, node_id = 10 : i64} {
            obelisk.sv.assertion.clocking attributes {node_id = 11 : i64} {
              obelisk.sv.timing.signal_event attributes {edge_kind = 1 : i32, has_iff = false, node_id = 12 : i64} {
                obelisk.sv.expression.named_value attributes {node_id = 13 : i64, referenced_path = "top.clk", referenced_symbol = @s1.$root::@s3.top::@s4.top::@s5.clk, semantic_type = !obelisk.integral<1, false, true, 0 : 0, logic>} {
                }
              }
              obelisk.sv.assertion.binary attributes {node_id = 14 : i64, operator_kind = 11 : i32} {
                obelisk.sv.assertion.binary attributes {node_id = 15 : i64, operator_kind = 1 : i32} {
                  obelisk.sv.assertion.binary attributes {node_id = 16 : i64, operator_kind = 1 : i32} {
                    obelisk.sv.assertion.binary attributes {node_id = 17 : i64, operator_kind = 0 : i32} {
                      obelisk.sv.assertion.simple attributes {has_repetition = false, is_null = false, node_id = 18 : i64, repetition_is_unbounded = false} {
                        obelisk.sv.expression.named_value attributes {node_id = 19 : i64, referenced_path = "top.a", referenced_symbol = @s1.$root::@s3.top::@s4.top::@s6.a, semantic_type = !obelisk.integral<1, false, true, 0 : 0, logic>} {
                        }
                      }
                      obelisk.sv.assertion.simple attributes {has_repetition = false, is_null = false, node_id = 20 : i64, repetition_is_unbounded = false} {
                        obelisk.sv.expression.named_value attributes {node_id = 21 : i64, referenced_path = "top.c", referenced_symbol = @s1.$root::@s3.top::@s4.top::@s8.c, semantic_type = !obelisk.integral<1, false, true, 0 : 0, logic>} {
                        }
                      }
                    }
                    obelisk.sv.assertion.binary attributes {node_id = 22 : i64, operator_kind = 0 : i32} {
                      obelisk.sv.assertion.unary attributes {has_range = false, node_id = 23 : i64, operator_kind = 0 : i32, range_is_unbounded = false} {
                        obelisk.sv.assertion.simple attributes {has_repetition = false, is_null = false, node_id = 24 : i64, repetition_is_unbounded = false} {
                          obelisk.sv.expression.named_value attributes {node_id = 25 : i64, referenced_path = "top.a", referenced_symbol = @s1.$root::@s3.top::@s4.top::@s6.a, semantic_type = !obelisk.integral<1, false, true, 0 : 0, logic>} {
                          }
                        }
                      }
                      obelisk.sv.assertion.simple attributes {has_repetition = false, is_null = false, node_id = 26 : i64, repetition_is_unbounded = false} {
                        obelisk.sv.expression.named_value attributes {node_id = 27 : i64, referenced_path = "top.b", referenced_symbol = @s1.$root::@s3.top::@s4.top::@s7.b, semantic_type = !obelisk.integral<1, false, true, 0 : 0, logic>} {
                        }
                      }
                    }
                  }
                  obelisk.sv.assertion.binary attributes {node_id = 28 : i64, operator_kind = 0 : i32} {
                    obelisk.sv.assertion.simple attributes {has_repetition = false, is_null = false, node_id = 29 : i64, repetition_is_unbounded = false} {
                      obelisk.sv.expression.named_value attributes {node_id = 30 : i64, referenced_path = "top.b", referenced_symbol = @s1.$root::@s3.top::@s4.top::@s7.b, semantic_type = !obelisk.integral<1, false, true, 0 : 0, logic>} {
                      }
                    }
                    obelisk.sv.assertion.simple attributes {has_repetition = false, is_null = false, node_id = 31 : i64, repetition_is_unbounded = false} {
                      obelisk.sv.expression.named_value attributes {node_id = 32 : i64, referenced_path = "top.c", referenced_symbol = @s1.$root::@s3.top::@s4.top::@s8.c, semantic_type = !obelisk.integral<1, false, true, 0 : 0, logic>} {
                      }
                    }
                  }
                }
                obelisk.sv.assertion.simple attributes {has_repetition = false, is_null = false, node_id = 33 : i64, repetition_is_unbounded = false} {
                  obelisk.sv.expression.named_value attributes {node_id = 34 : i64, referenced_path = "top.a", referenced_symbol = @s1.$root::@s3.top::@s4.top::@s6.a, semantic_type = !obelisk.integral<1, false, true, 0 : 0, logic>} {
                  }
                }
              }
            }
          }
        }
        obelisk.sv.symbol.procedural_block attributes {hierarchical_name = "top", node_id = 40 : i64, procedure_kind = 2 : i32, sym_name = "s40", time_precision_fs = 1000000 : i64, time_unit_fs = 1000000 : i64} {
          // The raw 2x2 product remains subject to its admission cap, but Z3
          // may collapse one side before monitor-shape selection. Here
          // (a&&b)||(a&&!b) becomes a, leaving an ordinary two-alternative
          // consequent rather than an unsupported combined-branching shape.
          obelisk.sv.statement.concurrent_assertion attributes {assertion_kind = 0 : i32, has_default_disable = false, has_fail_action = false, has_pass_action = false, node_id = 41 : i64} {
            obelisk.sv.assertion.clocking attributes {node_id = 42 : i64} {
              obelisk.sv.timing.signal_event attributes {edge_kind = 1 : i32, has_iff = false, node_id = 43 : i64} {
                obelisk.sv.expression.named_value attributes {node_id = 44 : i64, referenced_path = "top.clk", referenced_symbol = @s1.$root::@s3.top::@s4.top::@s5.clk, semantic_type = !obelisk.integral<1, false, true, 0 : 0, logic>} {
                }
              }
              obelisk.sv.assertion.binary attributes {node_id = 45 : i64, operator_kind = 11 : i32} {
                obelisk.sv.assertion.binary attributes {node_id = 46 : i64, operator_kind = 1 : i32} {
                  obelisk.sv.assertion.binary attributes {node_id = 47 : i64, operator_kind = 0 : i32} {
                    obelisk.sv.assertion.simple attributes {has_repetition = false, is_null = false, node_id = 48 : i64, repetition_is_unbounded = false} {
                      obelisk.sv.expression.named_value attributes {node_id = 49 : i64, referenced_path = "top.a", referenced_symbol = @s1.$root::@s3.top::@s4.top::@s6.a, semantic_type = !obelisk.integral<1, false, true, 0 : 0, logic>} {
                      }
                    }
                    obelisk.sv.assertion.simple attributes {has_repetition = false, is_null = false, node_id = 50 : i64, repetition_is_unbounded = false} {
                      obelisk.sv.expression.named_value attributes {node_id = 51 : i64, referenced_path = "top.b", referenced_symbol = @s1.$root::@s3.top::@s4.top::@s7.b, semantic_type = !obelisk.integral<1, false, true, 0 : 0, logic>} {
                      }
                    }
                  }
                  obelisk.sv.assertion.binary attributes {node_id = 52 : i64, operator_kind = 0 : i32} {
                    obelisk.sv.assertion.simple attributes {has_repetition = false, is_null = false, node_id = 53 : i64, repetition_is_unbounded = false} {
                      obelisk.sv.expression.named_value attributes {node_id = 54 : i64, referenced_path = "top.a", referenced_symbol = @s1.$root::@s3.top::@s4.top::@s6.a, semantic_type = !obelisk.integral<1, false, true, 0 : 0, logic>} {
                      }
                    }
                    obelisk.sv.assertion.unary attributes {has_range = false, node_id = 55 : i64, operator_kind = 0 : i32, range_is_unbounded = false} {
                      obelisk.sv.assertion.simple attributes {has_repetition = false, is_null = false, node_id = 56 : i64, repetition_is_unbounded = false} {
                        obelisk.sv.expression.named_value attributes {node_id = 57 : i64, referenced_path = "top.b", referenced_symbol = @s1.$root::@s3.top::@s4.top::@s7.b, semantic_type = !obelisk.integral<1, false, true, 0 : 0, logic>} {
                        }
                      }
                    }
                  }
                }
                obelisk.sv.assertion.binary attributes {node_id = 58 : i64, operator_kind = 1 : i32} {
                  obelisk.sv.assertion.simple attributes {has_repetition = false, is_null = false, node_id = 59 : i64, repetition_is_unbounded = false} {
                    obelisk.sv.expression.named_value attributes {node_id = 60 : i64, referenced_path = "top.b", referenced_symbol = @s1.$root::@s3.top::@s4.top::@s7.b, semantic_type = !obelisk.integral<1, false, true, 0 : 0, logic>} {
                    }
                  }
                  obelisk.sv.assertion.simple attributes {has_repetition = false, is_null = false, node_id = 61 : i64, repetition_is_unbounded = false} {
                    obelisk.sv.expression.named_value attributes {node_id = 62 : i64, referenced_path = "top.c", referenced_symbol = @s1.$root::@s3.top::@s4.top::@s8.c, semantic_type = !obelisk.integral<1, false, true, 0 : 0, logic>} {
                    }
                  }
                }
              }
            }
          }
        }
        obelisk.sv.symbol.procedural_block attributes {hierarchical_name = "top", node_id = 70 : i64, procedure_kind = 2 : i32, sym_name = "s70", time_precision_fs = 1000000 : i64, time_unit_fs = 1000000 : i64} {
          // Even after Z3 reduces the raw branching antecedent to a, the
          // pending s_nexttime consequent needs the source-age EOS coalescer
          // so assert observes its intrinsic strong failure.
          obelisk.sv.statement.concurrent_assertion attributes {assertion_kind = 0 : i32, has_default_disable = false, has_fail_action = false, has_pass_action = true, node_id = 71 : i64} {
            obelisk.sv.assertion.clocking attributes {node_id = 72 : i64} {
              obelisk.sv.timing.signal_event attributes {edge_kind = 1 : i32, has_iff = false, node_id = 73 : i64} {
                obelisk.sv.expression.named_value attributes {node_id = 74 : i64, referenced_path = "top.clk", referenced_symbol = @s1.$root::@s3.top::@s4.top::@s5.clk, semantic_type = !obelisk.integral<1, false, true, 0 : 0, logic>} {
                }
              }
              obelisk.sv.assertion.binary attributes {node_id = 75 : i64, operator_kind = 11 : i32} {
                obelisk.sv.assertion.binary attributes {node_id = 76 : i64, operator_kind = 1 : i32} {
                  obelisk.sv.assertion.binary attributes {node_id = 77 : i64, operator_kind = 0 : i32} {
                    obelisk.sv.assertion.simple attributes {has_repetition = false, is_null = false, node_id = 78 : i64, repetition_is_unbounded = false} {
                      obelisk.sv.expression.named_value attributes {node_id = 79 : i64, referenced_path = "top.a", referenced_symbol = @s1.$root::@s3.top::@s4.top::@s6.a, semantic_type = !obelisk.integral<1, false, true, 0 : 0, logic>} {
                      }
                    }
                    obelisk.sv.assertion.simple attributes {has_repetition = false, is_null = false, node_id = 80 : i64, repetition_is_unbounded = false} {
                      obelisk.sv.expression.named_value attributes {node_id = 81 : i64, referenced_path = "top.b", referenced_symbol = @s1.$root::@s3.top::@s4.top::@s7.b, semantic_type = !obelisk.integral<1, false, true, 0 : 0, logic>} {
                      }
                    }
                  }
                  obelisk.sv.assertion.binary attributes {node_id = 82 : i64, operator_kind = 0 : i32} {
                    obelisk.sv.assertion.simple attributes {has_repetition = false, is_null = false, node_id = 83 : i64, repetition_is_unbounded = false} {
                      obelisk.sv.expression.named_value attributes {node_id = 84 : i64, referenced_path = "top.a", referenced_symbol = @s1.$root::@s3.top::@s4.top::@s6.a, semantic_type = !obelisk.integral<1, false, true, 0 : 0, logic>} {
                      }
                    }
                    obelisk.sv.assertion.unary attributes {has_range = false, node_id = 85 : i64, operator_kind = 0 : i32, range_is_unbounded = false} {
                      obelisk.sv.assertion.simple attributes {has_repetition = false, is_null = false, node_id = 86 : i64, repetition_is_unbounded = false} {
                        obelisk.sv.expression.named_value attributes {node_id = 87 : i64, referenced_path = "top.b", referenced_symbol = @s1.$root::@s3.top::@s4.top::@s7.b, semantic_type = !obelisk.integral<1, false, true, 0 : 0, logic>} {
                        }
                      }
                    }
                  }
                }
                obelisk.sv.assertion.unary attributes {has_range = false, node_id = 88 : i64, operator_kind = 2 : i32, range_is_unbounded = false} {
                  obelisk.sv.assertion.simple attributes {has_repetition = false, is_null = false, node_id = 89 : i64, repetition_is_unbounded = false} {
                    obelisk.sv.expression.named_value attributes {node_id = 90 : i64, referenced_path = "top.c", referenced_symbol = @s1.$root::@s3.top::@s4.top::@s8.c, semantic_type = !obelisk.integral<1, false, true, 0 : 0, logic>} {
                    }
                  }
                }
              }
            }
            obelisk.sv.statement.expression_statement attributes {node_id = 91 : i64} {
              obelisk.sv.expression.call attributes {argument_count = 0 : i64, callee_name = "$display", constraint_restrictions = [], defaulted_arguments = array<i64>, has_inline_constraints = false, has_iterator_expression = false, has_output_arguments = false, has_this_class = false, is_signed = false, is_super_class = false, is_system_call = true, node_id = 92 : i64, semantic_type = !obelisk.void, subroutine_kind = 1 : i32, system_library_cell = "work.top", system_scope_path = "top", system_scope_symbol = @s1.$root::@s3.top::@s4.top} {
              }
            }
          }
        }
        obelisk.sv.symbol.procedural_block attributes {hierarchical_name = "top", node_id = 100 : i64, procedure_kind = 2 : i32, sym_name = "s100", time_precision_fs = 1000000 : i64, time_unit_fs = 1000000 : i64} {
          // The same preserved one-channel coalescer makes a pending weak
          // nexttime consequence execute its cover-property pass at EOS.
          obelisk.sv.statement.concurrent_assertion attributes {assertion_kind = 2 : i32, has_default_disable = false, has_fail_action = false, has_pass_action = true, node_id = 101 : i64} {
            obelisk.sv.assertion.clocking attributes {node_id = 102 : i64} {
              obelisk.sv.timing.signal_event attributes {edge_kind = 1 : i32, has_iff = false, node_id = 103 : i64} {
                obelisk.sv.expression.named_value attributes {node_id = 104 : i64, referenced_path = "top.clk", referenced_symbol = @s1.$root::@s3.top::@s4.top::@s5.clk, semantic_type = !obelisk.integral<1, false, true, 0 : 0, logic>} {
                }
              }
              obelisk.sv.assertion.binary attributes {node_id = 105 : i64, operator_kind = 11 : i32} {
                obelisk.sv.assertion.binary attributes {node_id = 106 : i64, operator_kind = 1 : i32} {
                  obelisk.sv.assertion.binary attributes {node_id = 107 : i64, operator_kind = 0 : i32} {
                    obelisk.sv.assertion.simple attributes {has_repetition = false, is_null = false, node_id = 108 : i64, repetition_is_unbounded = false} {
                      obelisk.sv.expression.named_value attributes {node_id = 109 : i64, referenced_path = "top.a", referenced_symbol = @s1.$root::@s3.top::@s4.top::@s6.a, semantic_type = !obelisk.integral<1, false, true, 0 : 0, logic>} {
                      }
                    }
                    obelisk.sv.assertion.simple attributes {has_repetition = false, is_null = false, node_id = 110 : i64, repetition_is_unbounded = false} {
                      obelisk.sv.expression.named_value attributes {node_id = 111 : i64, referenced_path = "top.b", referenced_symbol = @s1.$root::@s3.top::@s4.top::@s7.b, semantic_type = !obelisk.integral<1, false, true, 0 : 0, logic>} {
                      }
                    }
                  }
                  obelisk.sv.assertion.binary attributes {node_id = 112 : i64, operator_kind = 0 : i32} {
                    obelisk.sv.assertion.simple attributes {has_repetition = false, is_null = false, node_id = 113 : i64, repetition_is_unbounded = false} {
                      obelisk.sv.expression.named_value attributes {node_id = 114 : i64, referenced_path = "top.a", referenced_symbol = @s1.$root::@s3.top::@s4.top::@s6.a, semantic_type = !obelisk.integral<1, false, true, 0 : 0, logic>} {
                      }
                    }
                    obelisk.sv.assertion.unary attributes {has_range = false, node_id = 115 : i64, operator_kind = 0 : i32, range_is_unbounded = false} {
                      obelisk.sv.assertion.simple attributes {has_repetition = false, is_null = false, node_id = 116 : i64, repetition_is_unbounded = false} {
                        obelisk.sv.expression.named_value attributes {node_id = 117 : i64, referenced_path = "top.b", referenced_symbol = @s1.$root::@s3.top::@s4.top::@s7.b, semantic_type = !obelisk.integral<1, false, true, 0 : 0, logic>} {
                        }
                      }
                    }
                  }
                }
                obelisk.sv.assertion.unary attributes {has_range = false, node_id = 118 : i64, operator_kind = 1 : i32, range_is_unbounded = false} {
                  obelisk.sv.assertion.simple attributes {has_repetition = false, is_null = false, node_id = 119 : i64, repetition_is_unbounded = false} {
                    obelisk.sv.expression.named_value attributes {node_id = 120 : i64, referenced_path = "top.c", referenced_symbol = @s1.$root::@s3.top::@s4.top::@s8.c, semantic_type = !obelisk.integral<1, false, true, 0 : 0, logic>} {
                    }
                  }
                }
              }
            }
            obelisk.sv.statement.expression_statement attributes {node_id = 121 : i64} {
              obelisk.sv.expression.call attributes {argument_count = 0 : i64, callee_name = "$display", constraint_restrictions = [], defaulted_arguments = array<i64>, has_inline_constraints = false, has_iterator_expression = false, has_output_arguments = false, has_this_class = false, is_signed = false, is_super_class = false, is_system_call = true, node_id = 122 : i64, semantic_type = !obelisk.void, subroutine_kind = 1 : i32, system_library_cell = "work.top", system_scope_path = "top", system_scope_symbol = @s1.$root::@s3.top::@s4.top} {
              }
            }
          }
        }
        obelisk.sv.symbol.procedural_block attributes {hierarchical_name = "top", node_id = 130 : i64, procedure_kind = 2 : i32, sym_name = "s130", time_precision_fs = 1000000 : i64, time_unit_fs = 1000000 : i64} {
          // Z3 also may collapse a raw branching antecedent to one multi-age
          // trace.  The unfinished a ##1 b trace still needs EOS finalization:
          // implication completes vacuously and executes the assert pass.
          obelisk.sv.statement.concurrent_assertion attributes {assertion_kind = 0 : i32, has_default_disable = false, has_fail_action = false, has_pass_action = true, node_id = 131 : i64} {
            obelisk.sv.assertion.clocking attributes {node_id = 132 : i64} {
              obelisk.sv.timing.signal_event attributes {edge_kind = 1 : i32, has_iff = false, node_id = 133 : i64} {
                obelisk.sv.expression.named_value attributes {node_id = 134 : i64, referenced_path = "top.clk", referenced_symbol = @s1.$root::@s3.top::@s4.top::@s5.clk, semantic_type = !obelisk.integral<1, false, true, 0 : 0, logic>} {
                }
              }
              obelisk.sv.assertion.binary attributes {node_id = 135 : i64, operator_kind = 11 : i32} {
                obelisk.sv.assertion.binary attributes {node_id = 136 : i64, operator_kind = 1 : i32} {
                  obelisk.sv.assertion.sequence_concat attributes {delays = [{is_unbounded = false, max = 0 : i64, min = 0 : i64}, {is_unbounded = false, max = 1 : i64, min = 1 : i64}], node_id = 137 : i64} {
                    obelisk.sv.assertion.simple attributes {has_repetition = false, is_null = false, node_id = 138 : i64, repetition_is_unbounded = false} {
                      obelisk.sv.expression.named_value attributes {node_id = 139 : i64, referenced_path = "top.a", referenced_symbol = @s1.$root::@s3.top::@s4.top::@s6.a, semantic_type = !obelisk.integral<1, false, true, 0 : 0, logic>} {
                      }
                    }
                    obelisk.sv.assertion.binary attributes {node_id = 140 : i64, operator_kind = 0 : i32} {
                      obelisk.sv.assertion.simple attributes {has_repetition = false, is_null = false, node_id = 141 : i64, repetition_is_unbounded = false} {
                        obelisk.sv.expression.named_value attributes {node_id = 142 : i64, referenced_path = "top.b", referenced_symbol = @s1.$root::@s3.top::@s4.top::@s7.b, semantic_type = !obelisk.integral<1, false, true, 0 : 0, logic>} {
                        }
                      }
                      obelisk.sv.assertion.simple attributes {has_repetition = false, is_null = false, node_id = 143 : i64, repetition_is_unbounded = false} {
                        obelisk.sv.expression.named_value attributes {node_id = 144 : i64, referenced_path = "top.c", referenced_symbol = @s1.$root::@s3.top::@s4.top::@s8.c, semantic_type = !obelisk.integral<1, false, true, 0 : 0, logic>} {
                        }
                      }
                    }
                  }
                  obelisk.sv.assertion.sequence_concat attributes {delays = [{is_unbounded = false, max = 0 : i64, min = 0 : i64}, {is_unbounded = false, max = 1 : i64, min = 1 : i64}], node_id = 145 : i64} {
                    obelisk.sv.assertion.simple attributes {has_repetition = false, is_null = false, node_id = 146 : i64, repetition_is_unbounded = false} {
                      obelisk.sv.expression.named_value attributes {node_id = 147 : i64, referenced_path = "top.a", referenced_symbol = @s1.$root::@s3.top::@s4.top::@s6.a, semantic_type = !obelisk.integral<1, false, true, 0 : 0, logic>} {
                      }
                    }
                    obelisk.sv.assertion.binary attributes {node_id = 148 : i64, operator_kind = 0 : i32} {
                      obelisk.sv.assertion.simple attributes {has_repetition = false, is_null = false, node_id = 149 : i64, repetition_is_unbounded = false} {
                        obelisk.sv.expression.named_value attributes {node_id = 150 : i64, referenced_path = "top.b", referenced_symbol = @s1.$root::@s3.top::@s4.top::@s7.b, semantic_type = !obelisk.integral<1, false, true, 0 : 0, logic>} {
                        }
                      }
                      obelisk.sv.assertion.unary attributes {has_range = false, node_id = 151 : i64, operator_kind = 0 : i32, range_is_unbounded = false} {
                        obelisk.sv.assertion.simple attributes {has_repetition = false, is_null = false, node_id = 152 : i64, repetition_is_unbounded = false} {
                          obelisk.sv.expression.named_value attributes {node_id = 153 : i64, referenced_path = "top.c", referenced_symbol = @s1.$root::@s3.top::@s4.top::@s8.c, semantic_type = !obelisk.integral<1, false, true, 0 : 0, logic>} {
                          }
                        }
                      }
                    }
                  }
                }
                obelisk.sv.assertion.simple attributes {has_repetition = false, is_null = false, node_id = 154 : i64, repetition_is_unbounded = false} {
                  obelisk.sv.expression.named_value attributes {node_id = 155 : i64, referenced_path = "top.a", referenced_symbol = @s1.$root::@s3.top::@s4.top::@s6.a, semantic_type = !obelisk.integral<1, false, true, 0 : 0, logic>} {
                  }
                }
              }
            }
            obelisk.sv.statement.expression_statement attributes {node_id = 156 : i64} {
              obelisk.sv.expression.call attributes {argument_count = 0 : i64, callee_name = "$display", constraint_restrictions = [], defaulted_arguments = array<i64>, has_inline_constraints = false, has_iterator_expression = false, has_output_arguments = false, has_this_class = false, is_signed = false, is_super_class = false, is_system_call = true, node_id = 157 : i64, semantic_type = !obelisk.void, subroutine_kind = 1 : i32, system_library_cell = "work.top", system_scope_path = "top", system_scope_symbol = @s1.$root::@s3.top::@s4.top} {
              }
            }
          }
        }
      }
    }
  }
}

// CHECK-LABEL: obelisk_sim.func private @unit_0(
// CHECK-SAME: obelisk_sim.branching_antecedent_alternatives = 2 : i64
// CHECK-SAME: obelisk_sim.branching_antecedent_match_channels = 2 : i64
// CHECK-SAME: obelisk_sim.branching_antecedent_result_coalescer
// CHECK-SAME: obelisk_sim.sva_boolean_alternatives_after = 2 : i64
// CHECK-SAME: obelisk_sim.sva_boolean_alternatives_before = 3 : i64
// CHECK-SAME: obelisk_sim.sva_boolean_antecedent_alternatives_after = 2 : i64
// CHECK-SAME: obelisk_sim.sva_boolean_antecedent_alternatives_before = 3 : i64
// CHECK-SAME: obelisk_sim.sva_boolean_antecedent_literals_after = 4 : i64
// CHECK-SAME: obelisk_sim.sva_boolean_antecedent_literals_before = 6 : i64
// CHECK-SAME: obelisk_sim.sva_boolean_antecedent_solver = "z3"
// CHECK-SAME: obelisk_sim.sva_boolean_solver = "z3"
// CHECK-COUNT-3: obelisk_sim.assert.sampled_read
// CHECK-NOT: obelisk_sim.assert.sampled_read
// CHECK-COUNT-1: obelisk_sim.spawn @unit_0.fork.10.0.2
// CHECK-NOT: obelisk_sim.spawn @unit_0.fork.10.0.2

// CHECK-LABEL: obelisk_sim.func private @unit_1(
// CHECK-SAME: obelisk_sim.branching_consequent_alternatives = 2 : i64
// CHECK-SAME: obelisk_sim.branching_consequent_monitor
// CHECK-NOT: obelisk_sim.branching_antecedent_monitor
// CHECK-SAME: obelisk_sim.sva_boolean_antecedent_alternatives_after = 1 : i64
// CHECK-SAME: obelisk_sim.sva_boolean_antecedent_alternatives_before = 2 : i64
// CHECK-SAME: obelisk_sim.sva_boolean_antecedent_literals_after = 1 : i64
// CHECK-SAME: obelisk_sim.sva_boolean_antecedent_literals_before = 4 : i64
// CHECK-SAME: obelisk_sim.sva_boolean_antecedent_solver = "z3"
// CHECK-COUNT-3: obelisk_sim.assert.sampled_read
// CHECK-NOT: obelisk_sim.assert.sampled_read
// CHECK-NOT: obelisk.sv.assertion

// The raw two-alternative antecedent of unit_2 minimizes to one cube, but its
// strong two-age consequent must retain the source-age coalescer.  At EOS only
// the live consequent obligation fails; the no-match implication result was
// already dispatched vacuously on its sampling clock.
// CHECK-NOT: @unit_2.$concurrent_eos_branch_report.71.pass
// CHECK-LABEL: obelisk_sim.func private @unit_2.$concurrent_eos_branch_report.71.fail(
// CHECK-SAME: obelisk_sim.branching_antecedent_eos_report
// CHECK-SAME: obelisk_sim.concurrent_eos_report
// CHECK-NOT: @unit_2.$concurrent_eos_branch_report.71.pass
// CHECK-LABEL: obelisk_sim.func private @unit_2.$concurrent_eos_branch.71(
// CHECK-SAME: obelisk_sim.branching_antecedent_eos_coalescer
// CHECK-SAME: obelisk_sim.concurrent_eos_coordinator
// CHECK-COUNT-2: obelisk_sim.ref.load
// CHECK-NOT: obelisk_sim.ref.load
// CHECK: cf.cond_br {{.*}} {obelisk_sim.branching_antecedent_eos_result = "fail", obelisk_sim.branching_antecedent_eos_source_age = 1 : i64}
// CHECK: obelisk_sim.spawn @unit_2.$concurrent_eos_branch_report.71.fail{{.*}} {obelisk_sim.branching_antecedent_eos_result = "fail", obelisk_sim.branching_antecedent_eos_source_age = 1 : i64}
// CHECK-NOT: @unit_2.$concurrent_eos_branch_report.71.pass
// CHECK-LABEL: obelisk_sim.func private @unit_2(
// CHECK-SAME: obelisk_sim.branching_antecedent_alternatives = 1 : i64
// CHECK-SAME: obelisk_sim.branching_antecedent_consequent_eos_strength = "strong"
// CHECK-SAME: obelisk_sim.branching_antecedent_eos_coalescer
// CHECK-SAME: obelisk_sim.branching_antecedent_match_channels = 1 : i64
// CHECK-SAME: obelisk_sim.branching_antecedent_result_coalescer
// CHECK-SAME: obelisk_sim.branching_antecedent_result_horizon = 2 : i64
// CHECK-SAME: obelisk_sim.sva_boolean_antecedent_alternatives_after = 1 : i64
// CHECK-SAME: obelisk_sim.sva_boolean_antecedent_alternatives_before = 2 : i64
// CHECK-SAME: obelisk_sim.sva_boolean_antecedent_literals_after = 1 : i64
// CHECK-SAME: obelisk_sim.sva_boolean_antecedent_literals_before = 4 : i64
// CHECK-SAME: obelisk_sim.sva_boolean_antecedent_solver = "z3"
// CHECK-COUNT-2: obelisk_sim.ref.alloc
// CHECK-NOT: obelisk_sim.ref.alloc
// CHECK: obelisk_sim.spawn @unit_2.$concurrent_eos_branch.71
// CHECK-COUNT-2: obelisk_sim.assert.sampled_read
// CHECK-NOT: obelisk_sim.assert.sampled_read
// CHECK-NOT: @unit_2.$concurrent_eos_branch_report.71.pass

// Unit_3 proves the dual weak completion: the reduced one-channel monitor
// keeps its EOS state and executes exactly the cover pass for a pending
// nexttime obligation, without manufacturing a failure actor.
// CHECK-NOT: @unit_3.$concurrent_eos_branch_report.101.fail
// CHECK-LABEL: obelisk_sim.func private @unit_3.$concurrent_eos_branch_report.101.pass(
// CHECK-SAME: obelisk_sim.branching_antecedent_eos_report
// CHECK-SAME: obelisk_sim.concurrent_eos_report
// CHECK-NOT: @unit_3.$concurrent_eos_branch_report.101.fail
// CHECK-LABEL: obelisk_sim.func private @unit_3.$concurrent_eos_branch.101(
// CHECK-SAME: obelisk_sim.branching_antecedent_eos_coalescer
// CHECK-SAME: obelisk_sim.concurrent_eos_coordinator
// CHECK-COUNT-2: obelisk_sim.ref.load
// CHECK-NOT: obelisk_sim.ref.load
// CHECK: cf.cond_br {{.*}} {obelisk_sim.branching_antecedent_eos_result = "pass", obelisk_sim.branching_antecedent_eos_source_age = 1 : i64}
// CHECK: obelisk_sim.spawn @unit_3.$concurrent_eos_branch_report.101.pass{{.*}} {obelisk_sim.branching_antecedent_eos_result = "pass", obelisk_sim.branching_antecedent_eos_source_age = 1 : i64}
// CHECK-NOT: @unit_3.$concurrent_eos_branch_report.101.fail
// CHECK-LABEL: obelisk_sim.func private @unit_3(
// CHECK-SAME: obelisk_sim.branching_antecedent_alternatives = 1 : i64
// CHECK-SAME: obelisk_sim.branching_antecedent_consequent_eos_strength = "weak"
// CHECK-SAME: obelisk_sim.branching_antecedent_eos_coalescer
// CHECK-SAME: obelisk_sim.branching_antecedent_match_channels = 1 : i64
// CHECK-SAME: obelisk_sim.branching_antecedent_result_coalescer
// CHECK-SAME: obelisk_sim.branching_antecedent_result_horizon = 2 : i64
// CHECK-SAME: obelisk_sim.sva_boolean_antecedent_alternatives_after = 1 : i64
// CHECK-SAME: obelisk_sim.sva_boolean_antecedent_alternatives_before = 2 : i64
// CHECK-SAME: obelisk_sim.sva_boolean_antecedent_literals_after = 1 : i64
// CHECK-SAME: obelisk_sim.sva_boolean_antecedent_literals_before = 4 : i64
// CHECK-SAME: obelisk_sim.sva_boolean_antecedent_solver = "z3"
// CHECK-COUNT-2: obelisk_sim.ref.alloc
// CHECK-NOT: obelisk_sim.ref.alloc
// CHECK: obelisk_sim.spawn @unit_3.$concurrent_eos_branch.101
// CHECK-COUNT-2: obelisk_sim.assert.sampled_read
// CHECK-NOT: obelisk_sim.assert.sampled_read
// CHECK-NOT: @unit_3.$concurrent_eos_branch_report.101.fail

// Unit_4 closes the other solver-dependent hole: a post-minimized single
// antecedent may itself remain multi-age.  EOS must turn its unfinished
// a ##1 b attempt into one vacuous implication pass.
// CHECK-NOT: @unit_4.$concurrent_eos_branch_report.131.fail
// CHECK-LABEL: obelisk_sim.func private @unit_4.$concurrent_eos_branch_report.131.pass(
// CHECK-SAME: obelisk_sim.branching_antecedent_eos_report
// CHECK-SAME: obelisk_sim.concurrent_eos_report
// CHECK-NOT: @unit_4.$concurrent_eos_branch_report.131.fail
// CHECK-LABEL: obelisk_sim.func private @unit_4.$concurrent_eos_branch.131(
// CHECK-SAME: obelisk_sim.branching_antecedent_eos_coalescer
// CHECK-SAME: obelisk_sim.concurrent_eos_coordinator
// CHECK-COUNT-2: obelisk_sim.ref.load
// CHECK-NOT: obelisk_sim.ref.load
// CHECK: cf.cond_br {{.*}} {obelisk_sim.branching_antecedent_eos_result = "pass", obelisk_sim.branching_antecedent_eos_source_age = 1 : i64}
// CHECK: obelisk_sim.spawn @unit_4.$concurrent_eos_branch_report.131.pass{{.*}} {obelisk_sim.branching_antecedent_eos_result = "pass", obelisk_sim.branching_antecedent_eos_source_age = 1 : i64}
// CHECK-NOT: @unit_4.$concurrent_eos_branch_report.131.fail
// CHECK-LABEL: obelisk_sim.func private @unit_4(
// CHECK-SAME: obelisk_sim.bounded_antecedent_horizon = 2 : i64
// CHECK-SAME: obelisk_sim.branching_antecedent_alternatives = 1 : i64
// CHECK-SAME: obelisk_sim.branching_antecedent_consequent_eos_strength = "weak"
// CHECK-SAME: obelisk_sim.branching_antecedent_eos_coalescer
// CHECK-SAME: obelisk_sim.branching_antecedent_match_channels = 1 : i64
// CHECK-SAME: obelisk_sim.branching_antecedent_result_coalescer
// CHECK-SAME: obelisk_sim.branching_antecedent_result_horizon = 2 : i64
// CHECK-SAME: obelisk_sim.sva_boolean_antecedent_alternatives_after = 1 : i64
// CHECK-SAME: obelisk_sim.sva_boolean_antecedent_alternatives_before = 2 : i64
// CHECK-SAME: obelisk_sim.sva_boolean_antecedent_literals_after = 2 : i64
// CHECK-SAME: obelisk_sim.sva_boolean_antecedent_literals_before = 6 : i64
// CHECK-SAME: obelisk_sim.sva_boolean_antecedent_solver = "z3"
// CHECK-COUNT-2: obelisk_sim.ref.alloc
// CHECK-NOT: obelisk_sim.ref.alloc
// CHECK: obelisk_sim.spawn @unit_4.$concurrent_eos_branch.131
// CHECK-COUNT-2: obelisk_sim.assert.sampled_read
// CHECK-NOT: obelisk_sim.assert.sampled_read
// CHECK-NOT: @unit_4.$concurrent_eos_branch_report.131.fail
