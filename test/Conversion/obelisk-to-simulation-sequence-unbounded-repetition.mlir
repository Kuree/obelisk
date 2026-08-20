// RUN: obelisk-opt %s '--lower-obelisk-to-sim=opt-level=0' | FileCheck %s
// RUN: obelisk-opt %s '--lower-obelisk-to-sim=opt-level=3' -o /dev/null
// RUN: obelisk-opt %s '--lower-obelisk-to-sim=opt-level=3' '--encode-obelisk-sim-to-bytecode=vpi=off' -o /dev/null
// RUN: obelisk-opt %s '--lower-obelisk-to-sim=opt-level=3' --convert-obelisk-sim-processes-to-llvm-coroutines -o /dev/null
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
        obelisk.sv.symbol.port attributes {direction = 1 : i32, hierarchical_name = "top.hit", name = "hit", node_id = 11 : i64, semantic_type = !obelisk.integral<32, true, false, 31 : 0, int>, sym_name = "s11.hit"} {
        }
        obelisk.sv.symbol.variable attributes {hierarchical_name = "top.hit", lifetime = 1 : i32, name = "hit", node_id = 12 : i64, semantic_type = !obelisk.integral<32, true, false, 31 : 0, int>, sym_name = "s12.hit"} {
        }
        obelisk.sv.symbol.statement_block attributes {block_kind = 0 : i32, hierarchical_name = "top.c0", name = "c0", node_id = 13 : i64, sym_name = "s13.c0"} {
        }
        obelisk.sv.symbol.procedural_block attributes {hierarchical_name = "top", node_id = 14 : i64, procedure_kind = 2 : i32, sym_name = "s14", time_precision_fs = 1000000 : i64, time_unit_fs = 1000000 : i64} {
          obelisk.sv.statement.block attributes {block_path = "top.c0", block_symbol = @s1.$root::@s3.top::@s4.top::@s13.c0, node_id = 15 : i64} {
            obelisk.sv.statement.concurrent_assertion attributes {assertion_kind = 0 : i32, has_default_disable = false, has_fail_action = true, has_pass_action = true, node_id = 16 : i64} {
              obelisk.sv.assertion.clocking attributes {node_id = 17 : i64} {
                obelisk.sv.timing.signal_event attributes {edge_kind = 1 : i32, has_iff = false, node_id = 18 : i64} {
                  obelisk.sv.expression.named_value attributes {is_signed = false, node_id = 19 : i64, referenced_path = "top.clk", referenced_symbol = @s1.$root::@s3.top::@s4.top::@s6.clk, semantic_type = !obelisk.integral<1, false, true, 0 : 0, logic>} {
                  }
                }
                obelisk.sv.assertion.strong_weak attributes {node_id = 20 : i64, strength = 0 : i32} {
                  obelisk.sv.assertion.simple attributes {has_repetition = true, is_null = false, node_id = 21 : i64, repetition_is_unbounded = true, repetition_kind = 0 : i32, repetition_min = 2 : i64} {
                    obelisk.sv.expression.named_value attributes {is_signed = false, node_id = 22 : i64, referenced_path = "top.a", referenced_symbol = @s1.$root::@s3.top::@s4.top::@s8.a, semantic_type = !obelisk.integral<1, false, true, 0 : 0, logic>} {
                    }
                  }
                }
              }
              obelisk.sv.statement.expression_statement attributes {node_id = 23 : i64} {
                obelisk.sv.expression.unary_op attributes {is_signed = true, node_id = 24 : i64, operator_kind = 12 : i32, semantic_type = !obelisk.integral<32, true, false, 31 : 0, int>} {
                  obelisk.sv.expression.named_value attributes {is_signed = true, node_id = 25 : i64, referenced_path = "top.hit", referenced_symbol = @s1.$root::@s3.top::@s4.top::@s12.hit, semantic_type = !obelisk.integral<32, true, false, 31 : 0, int>} {
                  }
                }
              }
              obelisk.sv.statement.expression_statement attributes {node_id = 26 : i64} {
                obelisk.sv.expression.unary_op attributes {is_signed = true, node_id = 27 : i64, operator_kind = 13 : i32, semantic_type = !obelisk.integral<32, true, false, 31 : 0, int>} {
                  obelisk.sv.expression.named_value attributes {is_signed = true, node_id = 28 : i64, referenced_path = "top.hit", referenced_symbol = @s1.$root::@s3.top::@s4.top::@s12.hit, semantic_type = !obelisk.integral<32, true, false, 31 : 0, int>} {
                  }
                }
              }
            }
          }
        }
        obelisk.sv.symbol.statement_block attributes {block_kind = 0 : i32, hierarchical_name = "top.c1", name = "c1", node_id = 29 : i64, sym_name = "s15.c1"} {
        }
        obelisk.sv.symbol.procedural_block attributes {hierarchical_name = "top", node_id = 30 : i64, procedure_kind = 2 : i32, sym_name = "s16", time_precision_fs = 1000000 : i64, time_unit_fs = 1000000 : i64} {
          obelisk.sv.statement.block attributes {block_path = "top.c1", block_symbol = @s1.$root::@s3.top::@s4.top::@s15.c1, node_id = 31 : i64} {
            obelisk.sv.statement.concurrent_assertion attributes {assertion_kind = 0 : i32, has_default_disable = false, has_fail_action = true, has_pass_action = true, node_id = 32 : i64} {
              obelisk.sv.assertion.clocking attributes {node_id = 33 : i64} {
                obelisk.sv.timing.signal_event attributes {edge_kind = 1 : i32, has_iff = false, node_id = 34 : i64} {
                  obelisk.sv.expression.named_value attributes {is_signed = false, node_id = 35 : i64, referenced_path = "top.clk", referenced_symbol = @s1.$root::@s3.top::@s4.top::@s6.clk, semantic_type = !obelisk.integral<1, false, true, 0 : 0, logic>} {
                  }
                }
                obelisk.sv.assertion.sequence_concat attributes {delays = [{is_unbounded = false, max = 0 : i64, min = 0 : i64}, {is_unbounded = false, max = 1 : i64, min = 1 : i64}], node_id = 36 : i64} {
                  obelisk.sv.assertion.simple attributes {has_repetition = true, is_null = false, node_id = 37 : i64, repetition_is_unbounded = true, repetition_kind = 0 : i32, repetition_min = 2 : i64} {
                    obelisk.sv.expression.named_value attributes {is_signed = false, node_id = 38 : i64, referenced_path = "top.a", referenced_symbol = @s1.$root::@s3.top::@s4.top::@s8.a, semantic_type = !obelisk.integral<1, false, true, 0 : 0, logic>} {
                    }
                  }
                  obelisk.sv.assertion.simple attributes {has_repetition = false, is_null = false, node_id = 39 : i64, repetition_is_unbounded = false} {
                    obelisk.sv.expression.named_value attributes {is_signed = false, node_id = 40 : i64, referenced_path = "top.b", referenced_symbol = @s1.$root::@s3.top::@s4.top::@s10.b, semantic_type = !obelisk.integral<1, false, true, 0 : 0, logic>} {
                    }
                  }
                }
              }
              obelisk.sv.statement.expression_statement attributes {node_id = 41 : i64} {
                obelisk.sv.expression.unary_op attributes {is_signed = true, node_id = 42 : i64, operator_kind = 12 : i32, semantic_type = !obelisk.integral<32, true, false, 31 : 0, int>} {
                  obelisk.sv.expression.named_value attributes {is_signed = true, node_id = 43 : i64, referenced_path = "top.hit", referenced_symbol = @s1.$root::@s3.top::@s4.top::@s12.hit, semantic_type = !obelisk.integral<32, true, false, 31 : 0, int>} {
                  }
                }
              }
              obelisk.sv.statement.expression_statement attributes {node_id = 44 : i64} {
                obelisk.sv.expression.unary_op attributes {is_signed = true, node_id = 45 : i64, operator_kind = 13 : i32, semantic_type = !obelisk.integral<32, true, false, 31 : 0, int>} {
                  obelisk.sv.expression.named_value attributes {is_signed = true, node_id = 46 : i64, referenced_path = "top.hit", referenced_symbol = @s1.$root::@s3.top::@s4.top::@s12.hit, semantic_type = !obelisk.integral<32, true, false, 31 : 0, int>} {
                  }
                }
              }
            }
          }
        }
        obelisk.sv.symbol.statement_block attributes {block_kind = 0 : i32, hierarchical_name = "top.g0", name = "g0", node_id = 47 : i64, sym_name = "s17.g0"} {
        }
        obelisk.sv.symbol.procedural_block attributes {hierarchical_name = "top", node_id = 48 : i64, procedure_kind = 2 : i32, sym_name = "s18", time_precision_fs = 1000000 : i64, time_unit_fs = 1000000 : i64} {
          obelisk.sv.statement.block attributes {block_path = "top.g0", block_symbol = @s1.$root::@s3.top::@s4.top::@s17.g0, node_id = 49 : i64} {
            obelisk.sv.statement.concurrent_assertion attributes {assertion_kind = 0 : i32, has_default_disable = false, has_fail_action = true, has_pass_action = true, node_id = 50 : i64} {
              obelisk.sv.assertion.clocking attributes {node_id = 51 : i64} {
                obelisk.sv.timing.signal_event attributes {edge_kind = 1 : i32, has_iff = false, node_id = 52 : i64} {
                  obelisk.sv.expression.named_value attributes {is_signed = false, node_id = 53 : i64, referenced_path = "top.clk", referenced_symbol = @s1.$root::@s3.top::@s4.top::@s6.clk, semantic_type = !obelisk.integral<1, false, true, 0 : 0, logic>} {
                  }
                }
                obelisk.sv.assertion.strong_weak attributes {node_id = 54 : i64, strength = 1 : i32} {
                  obelisk.sv.assertion.simple attributes {has_repetition = true, is_null = false, node_id = 55 : i64, repetition_is_unbounded = true, repetition_kind = 2 : i32, repetition_min = 2 : i64} {
                    obelisk.sv.expression.named_value attributes {is_signed = false, node_id = 56 : i64, referenced_path = "top.a", referenced_symbol = @s1.$root::@s3.top::@s4.top::@s8.a, semantic_type = !obelisk.integral<1, false, true, 0 : 0, logic>} {
                    }
                  }
                }
              }
              obelisk.sv.statement.expression_statement attributes {node_id = 57 : i64} {
                obelisk.sv.expression.unary_op attributes {is_signed = true, node_id = 58 : i64, operator_kind = 12 : i32, semantic_type = !obelisk.integral<32, true, false, 31 : 0, int>} {
                  obelisk.sv.expression.named_value attributes {is_signed = true, node_id = 59 : i64, referenced_path = "top.hit", referenced_symbol = @s1.$root::@s3.top::@s4.top::@s12.hit, semantic_type = !obelisk.integral<32, true, false, 31 : 0, int>} {
                  }
                }
              }
              obelisk.sv.statement.expression_statement attributes {node_id = 60 : i64} {
                obelisk.sv.expression.unary_op attributes {is_signed = true, node_id = 61 : i64, operator_kind = 13 : i32, semantic_type = !obelisk.integral<32, true, false, 31 : 0, int>} {
                  obelisk.sv.expression.named_value attributes {is_signed = true, node_id = 62 : i64, referenced_path = "top.hit", referenced_symbol = @s1.$root::@s3.top::@s4.top::@s12.hit, semantic_type = !obelisk.integral<32, true, false, 31 : 0, int>} {
                  }
                }
              }
            }
          }
        }
        obelisk.sv.symbol.statement_block attributes {block_kind = 0 : i32, hierarchical_name = "top.g1", name = "g1", node_id = 63 : i64, sym_name = "s19.g1"} {
        }
        obelisk.sv.symbol.procedural_block attributes {hierarchical_name = "top", node_id = 64 : i64, procedure_kind = 2 : i32, sym_name = "s20", time_precision_fs = 1000000 : i64, time_unit_fs = 1000000 : i64} {
          obelisk.sv.statement.block attributes {block_path = "top.g1", block_symbol = @s1.$root::@s3.top::@s4.top::@s19.g1, node_id = 65 : i64} {
            obelisk.sv.statement.concurrent_assertion attributes {assertion_kind = 0 : i32, has_default_disable = false, has_fail_action = true, has_pass_action = true, node_id = 66 : i64} {
              obelisk.sv.assertion.clocking attributes {node_id = 67 : i64} {
                obelisk.sv.timing.signal_event attributes {edge_kind = 1 : i32, has_iff = false, node_id = 68 : i64} {
                  obelisk.sv.expression.named_value attributes {is_signed = false, node_id = 69 : i64, referenced_path = "top.clk", referenced_symbol = @s1.$root::@s3.top::@s4.top::@s6.clk, semantic_type = !obelisk.integral<1, false, true, 0 : 0, logic>} {
                  }
                }
                obelisk.sv.assertion.sequence_concat attributes {delays = [{is_unbounded = false, max = 0 : i64, min = 0 : i64}, {is_unbounded = false, max = 1 : i64, min = 1 : i64}], node_id = 70 : i64} {
                  obelisk.sv.assertion.simple attributes {has_repetition = true, is_null = false, node_id = 71 : i64, repetition_is_unbounded = true, repetition_kind = 2 : i32, repetition_min = 2 : i64} {
                    obelisk.sv.expression.named_value attributes {is_signed = false, node_id = 72 : i64, referenced_path = "top.a", referenced_symbol = @s1.$root::@s3.top::@s4.top::@s8.a, semantic_type = !obelisk.integral<1, false, true, 0 : 0, logic>} {
                    }
                  }
                  obelisk.sv.assertion.simple attributes {has_repetition = false, is_null = false, node_id = 73 : i64, repetition_is_unbounded = false} {
                    obelisk.sv.expression.named_value attributes {is_signed = false, node_id = 74 : i64, referenced_path = "top.b", referenced_symbol = @s1.$root::@s3.top::@s4.top::@s10.b, semantic_type = !obelisk.integral<1, false, true, 0 : 0, logic>} {
                    }
                  }
                }
              }
              obelisk.sv.statement.expression_statement attributes {node_id = 75 : i64} {
                obelisk.sv.expression.unary_op attributes {is_signed = true, node_id = 76 : i64, operator_kind = 12 : i32, semantic_type = !obelisk.integral<32, true, false, 31 : 0, int>} {
                  obelisk.sv.expression.named_value attributes {is_signed = true, node_id = 77 : i64, referenced_path = "top.hit", referenced_symbol = @s1.$root::@s3.top::@s4.top::@s12.hit, semantic_type = !obelisk.integral<32, true, false, 31 : 0, int>} {
                  }
                }
              }
              obelisk.sv.statement.expression_statement attributes {node_id = 78 : i64} {
                obelisk.sv.expression.unary_op attributes {is_signed = true, node_id = 79 : i64, operator_kind = 13 : i32, semantic_type = !obelisk.integral<32, true, false, 31 : 0, int>} {
                  obelisk.sv.expression.named_value attributes {is_signed = true, node_id = 80 : i64, referenced_path = "top.hit", referenced_symbol = @s1.$root::@s3.top::@s4.top::@s12.hit, semantic_type = !obelisk.integral<32, true, false, 31 : 0, int>} {
                  }
                }
              }
            }
          }
        }
        obelisk.sv.symbol.statement_block attributes {block_kind = 0 : i32, hierarchical_name = "top.n0", name = "n0", node_id = 81 : i64, sym_name = "s21.n0"} {
        }
        obelisk.sv.symbol.procedural_block attributes {hierarchical_name = "top", node_id = 82 : i64, procedure_kind = 2 : i32, sym_name = "s22", time_precision_fs = 1000000 : i64, time_unit_fs = 1000000 : i64} {
          obelisk.sv.statement.block attributes {block_path = "top.n0", block_symbol = @s1.$root::@s3.top::@s4.top::@s21.n0, node_id = 83 : i64} {
            obelisk.sv.statement.concurrent_assertion attributes {assertion_kind = 0 : i32, has_default_disable = false, has_fail_action = true, has_pass_action = true, node_id = 84 : i64} {
              obelisk.sv.assertion.clocking attributes {node_id = 85 : i64} {
                obelisk.sv.timing.signal_event attributes {edge_kind = 1 : i32, has_iff = false, node_id = 86 : i64} {
                  obelisk.sv.expression.named_value attributes {is_signed = false, node_id = 87 : i64, referenced_path = "top.clk", referenced_symbol = @s1.$root::@s3.top::@s4.top::@s6.clk, semantic_type = !obelisk.integral<1, false, true, 0 : 0, logic>} {
                  }
                }
                obelisk.sv.assertion.simple attributes {has_repetition = true, is_null = false, node_id = 88 : i64, repetition_is_unbounded = true, repetition_kind = 1 : i32, repetition_min = 2 : i64} {
                  obelisk.sv.expression.named_value attributes {is_signed = false, node_id = 89 : i64, referenced_path = "top.a", referenced_symbol = @s1.$root::@s3.top::@s4.top::@s8.a, semantic_type = !obelisk.integral<1, false, true, 0 : 0, logic>} {
                  }
                }
              }
              obelisk.sv.statement.expression_statement attributes {node_id = 90 : i64} {
                obelisk.sv.expression.unary_op attributes {is_signed = true, node_id = 91 : i64, operator_kind = 12 : i32, semantic_type = !obelisk.integral<32, true, false, 31 : 0, int>} {
                  obelisk.sv.expression.named_value attributes {is_signed = true, node_id = 92 : i64, referenced_path = "top.hit", referenced_symbol = @s1.$root::@s3.top::@s4.top::@s12.hit, semantic_type = !obelisk.integral<32, true, false, 31 : 0, int>} {
                  }
                }
              }
              obelisk.sv.statement.expression_statement attributes {node_id = 93 : i64} {
                obelisk.sv.expression.unary_op attributes {is_signed = true, node_id = 94 : i64, operator_kind = 13 : i32, semantic_type = !obelisk.integral<32, true, false, 31 : 0, int>} {
                  obelisk.sv.expression.named_value attributes {is_signed = true, node_id = 95 : i64, referenced_path = "top.hit", referenced_symbol = @s1.$root::@s3.top::@s4.top::@s12.hit, semantic_type = !obelisk.integral<32, true, false, 31 : 0, int>} {
                  }
                }
              }
            }
          }
        }
        obelisk.sv.symbol.statement_block attributes {block_kind = 0 : i32, hierarchical_name = "top.n1", name = "n1", node_id = 96 : i64, sym_name = "s23.n1"} {
        }
        obelisk.sv.symbol.procedural_block attributes {hierarchical_name = "top", node_id = 97 : i64, procedure_kind = 2 : i32, sym_name = "s24", time_precision_fs = 1000000 : i64, time_unit_fs = 1000000 : i64} {
          obelisk.sv.statement.block attributes {block_path = "top.n1", block_symbol = @s1.$root::@s3.top::@s4.top::@s23.n1, node_id = 98 : i64} {
            obelisk.sv.statement.concurrent_assertion attributes {assertion_kind = 0 : i32, has_default_disable = false, has_fail_action = true, has_pass_action = true, node_id = 99 : i64} {
              obelisk.sv.assertion.clocking attributes {node_id = 100 : i64} {
                obelisk.sv.timing.signal_event attributes {edge_kind = 1 : i32, has_iff = false, node_id = 101 : i64} {
                  obelisk.sv.expression.named_value attributes {is_signed = false, node_id = 102 : i64, referenced_path = "top.clk", referenced_symbol = @s1.$root::@s3.top::@s4.top::@s6.clk, semantic_type = !obelisk.integral<1, false, true, 0 : 0, logic>} {
                  }
                }
                obelisk.sv.assertion.sequence_concat attributes {delays = [{is_unbounded = false, max = 0 : i64, min = 0 : i64}, {is_unbounded = false, max = 1 : i64, min = 1 : i64}], node_id = 103 : i64} {
                  obelisk.sv.assertion.simple attributes {has_repetition = true, is_null = false, node_id = 104 : i64, repetition_is_unbounded = true, repetition_kind = 1 : i32, repetition_min = 2 : i64} {
                    obelisk.sv.expression.named_value attributes {is_signed = false, node_id = 105 : i64, referenced_path = "top.a", referenced_symbol = @s1.$root::@s3.top::@s4.top::@s8.a, semantic_type = !obelisk.integral<1, false, true, 0 : 0, logic>} {
                    }
                  }
                  obelisk.sv.assertion.simple attributes {has_repetition = false, is_null = false, node_id = 106 : i64, repetition_is_unbounded = false} {
                    obelisk.sv.expression.named_value attributes {is_signed = false, node_id = 107 : i64, referenced_path = "top.b", referenced_symbol = @s1.$root::@s3.top::@s4.top::@s10.b, semantic_type = !obelisk.integral<1, false, true, 0 : 0, logic>} {
                    }
                  }
                }
              }
              obelisk.sv.statement.expression_statement attributes {node_id = 108 : i64} {
                obelisk.sv.expression.unary_op attributes {is_signed = true, node_id = 109 : i64, operator_kind = 12 : i32, semantic_type = !obelisk.integral<32, true, false, 31 : 0, int>} {
                  obelisk.sv.expression.named_value attributes {is_signed = true, node_id = 110 : i64, referenced_path = "top.hit", referenced_symbol = @s1.$root::@s3.top::@s4.top::@s12.hit, semantic_type = !obelisk.integral<32, true, false, 31 : 0, int>} {
                  }
                }
              }
              obelisk.sv.statement.expression_statement attributes {node_id = 111 : i64} {
                obelisk.sv.expression.unary_op attributes {is_signed = true, node_id = 112 : i64, operator_kind = 13 : i32, semantic_type = !obelisk.integral<32, true, false, 31 : 0, int>} {
                  obelisk.sv.expression.named_value attributes {is_signed = true, node_id = 113 : i64, referenced_path = "top.hit", referenced_symbol = @s1.$root::@s3.top::@s4.top::@s12.hit, semantic_type = !obelisk.integral<32, true, false, 31 : 0, int>} {
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

// Strong consecutive [*2:$] has two incomplete run states. A false sample
// kills/fails a run; EOS counts both states and reports every survivor.
// CHECK-LABEL: obelisk_sim.func private @unit_0.$concurrent_eos_count.16.repetition_strong(
// CHECK-COUNT-2: obelisk_sim.ref.load
// CHECK-LABEL: obelisk_sim.func private @unit_0(
// CHECK-SAME: obelisk_sim.end_of_simulation_strength = "strong"
// CHECK-SAME: obelisk_sim.persistent_repetition_kind = "consecutive"
// CHECK-SAME: obelisk_sim.persistent_repetition_min = 2 : i64
// CHECK-SAME: obelisk_sim.persistent_repetition_states = 2 : i64
// CHECK-SAME: obelisk_sim.persistent_repetition_unbounded
// CHECK-NOT: obelisk_sim.persistent_repetition_max
// CHECK: obelisk_sim.assert.sampled_read
// CHECK-COUNT-4: arith.select
// CHECK: obelisk_sim.spawn @unit_0.fork.16.1.1

// Consecutive [*2:$] ##1 b adds one saturated pending-terminal state.
// Bare assertion completion is weak and retains all three live counts.
// CHECK-LABEL: obelisk_sim.func private @unit_1.$concurrent_eos_count.32.repetition_weak(
// CHECK-COUNT-3: obelisk_sim.ref.load
// CHECK-LABEL: obelisk_sim.func private @unit_1(
// CHECK-SAME: obelisk_sim.persistent_repetition_kind = "consecutive"
// CHECK-SAME: obelisk_sim.persistent_repetition_states = 3 : i64
// CHECK-SAME: obelisk_sim.persistent_repetition_unbounded
// CHECK-COUNT-2: obelisk_sim.assert.sampled_read
// CHECK: arith.andi

// Goto [->2:$] without a continuation needs only the two pre-minimum counts.
// False samples retain rather than fail those states.
// CHECK-LABEL: obelisk_sim.func private @unit_2.$concurrent_eos_count.50.repetition_weak(
// CHECK-COUNT-2: obelisk_sim.ref.load
// CHECK-LABEL: obelisk_sim.func private @unit_2(
// CHECK-SAME: obelisk_sim.persistent_repetition_kind = "goto"
// CHECK-SAME: obelisk_sim.persistent_repetition_states = 2 : i64
// CHECK-SAME: obelisk_sim.persistent_repetition_unbounded

// Goto [->2:$] ##1 b has distinct saturated eligible and pending states.
// CHECK-LABEL: obelisk_sim.func private @unit_3.$concurrent_eos_count.66.repetition_weak(
// CHECK-COUNT-4: obelisk_sim.ref.load
// CHECK-LABEL: obelisk_sim.func private @unit_3(
// CHECK-SAME: obelisk_sim.persistent_repetition_kind = "goto"
// CHECK-SAME: obelisk_sim.persistent_repetition_states = 4 : i64
// CHECK-SAME: obelisk_sim.persistent_repetition_unbounded
// CHECK-COUNT-2: obelisk_sim.assert.sampled_read

// Nonconsecutive [=2:$] shares the two pre-minimum states when no terminal is
// present, and one saturated eligible state when ##1 b follows.
// CHECK-LABEL: obelisk_sim.func private @unit_4.$concurrent_eos_count.84.repetition_weak(
// CHECK-COUNT-2: obelisk_sim.ref.load
// CHECK-LABEL: obelisk_sim.func private @unit_4(
// CHECK-SAME: obelisk_sim.persistent_repetition_kind = "nonconsecutive"
// CHECK-SAME: obelisk_sim.persistent_repetition_states = 2 : i64
// CHECK-SAME: obelisk_sim.persistent_repetition_unbounded
// CHECK-LABEL: obelisk_sim.func private @unit_5.$concurrent_eos_count.99.repetition_weak(
// CHECK-COUNT-3: obelisk_sim.ref.load
// CHECK-LABEL: obelisk_sim.func private @unit_5(
// CHECK-SAME: obelisk_sim.persistent_repetition_kind = "nonconsecutive"
// CHECK-SAME: obelisk_sim.persistent_repetition_states = 3 : i64
// CHECK-SAME: obelisk_sim.persistent_repetition_unbounded
// CHECK-COUNT-2: obelisk_sim.assert.sampled_read
