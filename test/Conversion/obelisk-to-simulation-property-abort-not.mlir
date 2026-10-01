// RUN: obelisk-opt %s '--lower-obelisk-to-sim=opt-level=0' | FileCheck %s
// RUN: obelisk-opt %s '--lower-obelisk-to-sim=opt-level=3' -o /dev/null
// RUN: obelisk-opt %s '--lower-obelisk-to-sim=opt-level=3' '--encode-obelisk-sim-to-bytecode=vpi=off' -o /dev/null
// RUN: obelisk-opt %s '--lower-obelisk-to-sim=opt-level=0' -o %t.threaded
// RUN: obelisk-opt %s '--lower-obelisk-to-sim=opt-level=0' --mlir-disable-threading -o %t.serial
// RUN: diff %t.threaded %t.serial

module attributes {llvm.data_layout = "e-m:e-p:64:64-i64:64-n8:16:32:64-S128", llvm.target_triple = "x86_64-unknown-linux-gnu"} {
  obelisk.sv.symbol.definition @s0.top attributes {definition_kind = 0 : i32, hierarchical_name = "top", name = "top", node_id = 0 : i64} {
  }
  obelisk.sv.symbol.root @s1.$root attributes {hierarchical_name = "\\$root ", name = "$root", node_id = 1 : i64} {
    obelisk.sv.symbol.compilation_unit @s2 attributes {hierarchical_name = "$unit", node_id = 2 : i64} {
    }
    obelisk.sv.symbol.instance @s3.top attributes {hierarchical_name = "top", is_uninstantiated = false, name = "top", node_id = 3 : i64, referenced_path = "top", referenced_symbol = @s0.top} {
      obelisk.sv.symbol.instance_body @s4.top attributes {hierarchical_name = "top", name = "top", node_id = 4 : i64, time_precision_fs = 1000000 : i64, time_unit_fs = 1000000 : i64} {
        obelisk.sv.symbol.port @s5.clk attributes {direction = 0 : i32, hierarchical_name = "top.clk", name = "clk", node_id = 5 : i64, semantic_type = !obelisk.integral<1, false, true, 0 : 0, logic>} {
        }
        obelisk.sv.symbol.variable @s6.clk attributes {hierarchical_name = "top.clk", lifetime = 1 : i32, name = "clk", node_id = 6 : i64, semantic_type = !obelisk.integral<1, false, true, 0 : 0, logic>} {
        }
        obelisk.sv.symbol.port @s7.a attributes {direction = 0 : i32, hierarchical_name = "top.a", name = "a", node_id = 7 : i64, semantic_type = !obelisk.integral<1, false, true, 0 : 0, logic>} {
        }
        obelisk.sv.symbol.variable @s8.a attributes {hierarchical_name = "top.a", lifetime = 1 : i32, name = "a", node_id = 8 : i64, semantic_type = !obelisk.integral<1, false, true, 0 : 0, logic>} {
        }
        obelisk.sv.symbol.port @s9.b attributes {direction = 0 : i32, hierarchical_name = "top.b", name = "b", node_id = 9 : i64, semantic_type = !obelisk.integral<1, false, true, 0 : 0, logic>} {
        }
        obelisk.sv.symbol.variable @s10.b attributes {hierarchical_name = "top.b", lifetime = 1 : i32, name = "b", node_id = 10 : i64, semantic_type = !obelisk.integral<1, false, true, 0 : 0, logic>} {
        }
        obelisk.sv.symbol.port @s11.reset attributes {direction = 0 : i32, hierarchical_name = "top.reset", name = "reset", node_id = 11 : i64, semantic_type = !obelisk.integral<1, false, true, 0 : 0, logic>} {
        }
        obelisk.sv.symbol.variable @s12.reset attributes {hierarchical_name = "top.reset", lifetime = 1 : i32, name = "reset", node_id = 12 : i64, semantic_type = !obelisk.integral<1, false, true, 0 : 0, logic>} {
        }
        obelisk.sv.symbol.port @s13.hit attributes {direction = 1 : i32, hierarchical_name = "top.hit", name = "hit", node_id = 13 : i64, semantic_type = !obelisk.integral<32, true, false, 31 : 0, int>} {
        }
        obelisk.sv.symbol.variable @s14.hit attributes {hierarchical_name = "top.hit", lifetime = 1 : i32, name = "hit", node_id = 14 : i64, semantic_type = !obelisk.integral<32, true, false, 31 : 0, int>} {
        }
        obelisk.sv.symbol.procedural_block @s15 attributes {hierarchical_name = "top", node_id = 15 : i64, procedure_kind = 2 : i32, time_precision_fs = 1000000 : i64, time_unit_fs = 1000000 : i64} {
          obelisk.sv.statement.concurrent_assertion attributes {assertion_kind = 0 : i32, has_default_disable = false, has_fail_action = true, has_pass_action = true, node_id = 16 : i64} {
            obelisk.sv.assertion.clocking attributes {node_id = 17 : i64} {
              obelisk.sv.timing.signal_event attributes {edge_kind = 1 : i32, has_iff = false, node_id = 18 : i64} {
                obelisk.sv.expression.named_value attributes {is_signed = false, node_id = 19 : i64, referenced_path = "top.clk", referenced_symbol = @s1.$root::@s3.top::@s4.top::@s6.clk, semantic_type = !obelisk.integral<1, false, true, 0 : 0, logic>} {
                }
              }
              obelisk.sv.assertion.abort attributes {action = 0 : i32, is_synchronous = false, node_id = 20 : i64} {
                obelisk.sv.expression.named_value attributes {is_signed = false, node_id = 21 : i64, referenced_path = "top.reset", referenced_symbol = @s1.$root::@s3.top::@s4.top::@s12.reset, semantic_type = !obelisk.integral<1, false, true, 0 : 0, logic>} {
                }
                obelisk.sv.assertion.unary attributes {has_range = false, node_id = 22 : i64, operator_kind = 0 : i32, range_is_unbounded = false} {
                  obelisk.sv.assertion.sequence_concat attributes {delays = [{is_unbounded = false, max = 0 : i64, min = 0 : i64}, {is_unbounded = false, max = 2 : i64, min = 2 : i64}], node_id = 23 : i64} {
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
            }
            obelisk.sv.statement.expression_statement attributes {node_id = 28 : i64} {
              obelisk.sv.expression.unary_op attributes {is_signed = true, node_id = 29 : i64, operator_kind = 12 : i32, semantic_type = !obelisk.integral<32, true, false, 31 : 0, int>} {
                obelisk.sv.expression.named_value attributes {is_signed = true, node_id = 30 : i64, referenced_path = "top.hit", referenced_symbol = @s1.$root::@s3.top::@s4.top::@s14.hit, semantic_type = !obelisk.integral<32, true, false, 31 : 0, int>} {
                }
              }
            }
            obelisk.sv.statement.expression_statement attributes {node_id = 31 : i64} {
              obelisk.sv.expression.unary_op attributes {is_signed = true, node_id = 32 : i64, operator_kind = 13 : i32, semantic_type = !obelisk.integral<32, true, false, 31 : 0, int>} {
                obelisk.sv.expression.named_value attributes {is_signed = true, node_id = 33 : i64, referenced_path = "top.hit", referenced_symbol = @s1.$root::@s3.top::@s4.top::@s14.hit, semantic_type = !obelisk.integral<32, true, false, 31 : 0, int>} {
                }
              }
            }
          }
        }
        obelisk.sv.symbol.procedural_block @s16 attributes {hierarchical_name = "top", node_id = 34 : i64, procedure_kind = 2 : i32, time_precision_fs = 1000000 : i64, time_unit_fs = 1000000 : i64} {
          obelisk.sv.statement.concurrent_assertion attributes {assertion_kind = 0 : i32, has_default_disable = false, has_fail_action = true, has_pass_action = true, node_id = 35 : i64} {
            obelisk.sv.assertion.clocking attributes {node_id = 36 : i64} {
              obelisk.sv.timing.signal_event attributes {edge_kind = 1 : i32, has_iff = false, node_id = 37 : i64} {
                obelisk.sv.expression.named_value attributes {is_signed = false, node_id = 38 : i64, referenced_path = "top.clk", referenced_symbol = @s1.$root::@s3.top::@s4.top::@s6.clk, semantic_type = !obelisk.integral<1, false, true, 0 : 0, logic>} {
                }
              }
              obelisk.sv.assertion.unary attributes {has_range = false, node_id = 39 : i64, operator_kind = 0 : i32, range_is_unbounded = false} {
                obelisk.sv.assertion.abort attributes {action = 0 : i32, is_synchronous = false, node_id = 40 : i64} {
                  obelisk.sv.expression.named_value attributes {is_signed = false, node_id = 41 : i64, referenced_path = "top.reset", referenced_symbol = @s1.$root::@s3.top::@s4.top::@s12.reset, semantic_type = !obelisk.integral<1, false, true, 0 : 0, logic>} {
                  }
                  obelisk.sv.assertion.sequence_concat attributes {delays = [{is_unbounded = false, max = 0 : i64, min = 0 : i64}, {is_unbounded = false, max = 2 : i64, min = 2 : i64}], node_id = 42 : i64} {
                    obelisk.sv.assertion.simple attributes {has_repetition = false, is_null = false, node_id = 43 : i64, repetition_is_unbounded = false} {
                      obelisk.sv.expression.named_value attributes {is_signed = false, node_id = 44 : i64, referenced_path = "top.a", referenced_symbol = @s1.$root::@s3.top::@s4.top::@s8.a, semantic_type = !obelisk.integral<1, false, true, 0 : 0, logic>} {
                      }
                    }
                    obelisk.sv.assertion.simple attributes {has_repetition = false, is_null = false, node_id = 45 : i64, repetition_is_unbounded = false} {
                      obelisk.sv.expression.named_value attributes {is_signed = false, node_id = 46 : i64, referenced_path = "top.b", referenced_symbol = @s1.$root::@s3.top::@s4.top::@s10.b, semantic_type = !obelisk.integral<1, false, true, 0 : 0, logic>} {
                      }
                    }
                  }
                }
              }
            }
            obelisk.sv.statement.expression_statement attributes {node_id = 47 : i64} {
              obelisk.sv.expression.unary_op attributes {is_signed = true, node_id = 48 : i64, operator_kind = 12 : i32, semantic_type = !obelisk.integral<32, true, false, 31 : 0, int>} {
                obelisk.sv.expression.named_value attributes {is_signed = true, node_id = 49 : i64, referenced_path = "top.hit", referenced_symbol = @s1.$root::@s3.top::@s4.top::@s14.hit, semantic_type = !obelisk.integral<32, true, false, 31 : 0, int>} {
                }
              }
            }
            obelisk.sv.statement.expression_statement attributes {node_id = 50 : i64} {
              obelisk.sv.expression.unary_op attributes {is_signed = true, node_id = 51 : i64, operator_kind = 13 : i32, semantic_type = !obelisk.integral<32, true, false, 31 : 0, int>} {
                obelisk.sv.expression.named_value attributes {is_signed = true, node_id = 52 : i64, referenced_path = "top.hit", referenced_symbol = @s1.$root::@s3.top::@s4.top::@s14.hit, semantic_type = !obelisk.integral<32, true, false, 31 : 0, int>} {
                }
              }
            }
          }
        }
        obelisk.sv.symbol.procedural_block @s17 attributes {hierarchical_name = "top", node_id = 53 : i64, procedure_kind = 2 : i32, time_precision_fs = 1000000 : i64, time_unit_fs = 1000000 : i64} {
          obelisk.sv.statement.concurrent_assertion attributes {assertion_kind = 2 : i32, has_default_disable = false, has_fail_action = false, has_pass_action = true, node_id = 54 : i64} {
            obelisk.sv.assertion.clocking attributes {node_id = 55 : i64} {
              obelisk.sv.timing.signal_event attributes {edge_kind = 1 : i32, has_iff = false, node_id = 56 : i64} {
                obelisk.sv.expression.named_value attributes {is_signed = false, node_id = 57 : i64, referenced_path = "top.clk", referenced_symbol = @s1.$root::@s3.top::@s4.top::@s6.clk, semantic_type = !obelisk.integral<1, false, true, 0 : 0, logic>} {
                }
              }
              obelisk.sv.assertion.abort attributes {action = 1 : i32, is_synchronous = false, node_id = 58 : i64} {
                obelisk.sv.expression.named_value attributes {is_signed = false, node_id = 59 : i64, referenced_path = "top.reset", referenced_symbol = @s1.$root::@s3.top::@s4.top::@s12.reset, semantic_type = !obelisk.integral<1, false, true, 0 : 0, logic>} {
                }
                obelisk.sv.assertion.unary attributes {has_range = false, node_id = 60 : i64, operator_kind = 0 : i32, range_is_unbounded = false} {
                  obelisk.sv.assertion.sequence_concat attributes {delays = [{is_unbounded = false, max = 0 : i64, min = 0 : i64}, {is_unbounded = false, max = 2 : i64, min = 2 : i64}], node_id = 61 : i64} {
                    obelisk.sv.assertion.simple attributes {has_repetition = false, is_null = false, node_id = 62 : i64, repetition_is_unbounded = false} {
                      obelisk.sv.expression.named_value attributes {is_signed = false, node_id = 63 : i64, referenced_path = "top.a", referenced_symbol = @s1.$root::@s3.top::@s4.top::@s8.a, semantic_type = !obelisk.integral<1, false, true, 0 : 0, logic>} {
                      }
                    }
                    obelisk.sv.assertion.simple attributes {has_repetition = false, is_null = false, node_id = 64 : i64, repetition_is_unbounded = false} {
                      obelisk.sv.expression.named_value attributes {is_signed = false, node_id = 65 : i64, referenced_path = "top.b", referenced_symbol = @s1.$root::@s3.top::@s4.top::@s10.b, semantic_type = !obelisk.integral<1, false, true, 0 : 0, logic>} {
                      }
                    }
                  }
                }
              }
            }
            obelisk.sv.statement.expression_statement attributes {node_id = 66 : i64} {
              obelisk.sv.expression.unary_op attributes {is_signed = true, node_id = 67 : i64, operator_kind = 12 : i32, semantic_type = !obelisk.integral<32, true, false, 31 : 0, int>} {
                obelisk.sv.expression.named_value attributes {is_signed = true, node_id = 68 : i64, referenced_path = "top.hit", referenced_symbol = @s1.$root::@s3.top::@s4.top::@s14.hit, semantic_type = !obelisk.integral<32, true, false, 31 : 0, int>} {
                }
              }
            }
          }
        }
        obelisk.sv.symbol.procedural_block @s18 attributes {hierarchical_name = "top", node_id = 69 : i64, procedure_kind = 2 : i32, time_precision_fs = 1000000 : i64, time_unit_fs = 1000000 : i64} {
          obelisk.sv.statement.concurrent_assertion attributes {assertion_kind = 2 : i32, has_default_disable = false, has_fail_action = false, has_pass_action = true, node_id = 70 : i64} {
            obelisk.sv.assertion.clocking attributes {node_id = 71 : i64} {
              obelisk.sv.timing.signal_event attributes {edge_kind = 1 : i32, has_iff = false, node_id = 72 : i64} {
                obelisk.sv.expression.named_value attributes {is_signed = false, node_id = 73 : i64, referenced_path = "top.clk", referenced_symbol = @s1.$root::@s3.top::@s4.top::@s6.clk, semantic_type = !obelisk.integral<1, false, true, 0 : 0, logic>} {
                }
              }
              obelisk.sv.assertion.unary attributes {has_range = false, node_id = 74 : i64, operator_kind = 0 : i32, range_is_unbounded = false} {
                obelisk.sv.assertion.abort attributes {action = 1 : i32, is_synchronous = false, node_id = 75 : i64} {
                  obelisk.sv.expression.named_value attributes {is_signed = false, node_id = 76 : i64, referenced_path = "top.reset", referenced_symbol = @s1.$root::@s3.top::@s4.top::@s12.reset, semantic_type = !obelisk.integral<1, false, true, 0 : 0, logic>} {
                  }
                  obelisk.sv.assertion.simple attributes {has_repetition = false, is_null = false, node_id = 77 : i64, repetition_is_unbounded = false} {
                    obelisk.sv.expression.named_value attributes {is_signed = false, node_id = 78 : i64, referenced_path = "top.a", referenced_symbol = @s1.$root::@s3.top::@s4.top::@s8.a, semantic_type = !obelisk.integral<1, false, true, 0 : 0, logic>} {
                    }
                  }
                }
              }
            }
            obelisk.sv.statement.expression_statement attributes {node_id = 82 : i64} {
              obelisk.sv.expression.unary_op attributes {is_signed = true, node_id = 83 : i64, operator_kind = 12 : i32, semantic_type = !obelisk.integral<32, true, false, 31 : 0, int>} {
                obelisk.sv.expression.named_value attributes {is_signed = true, node_id = 84 : i64, referenced_path = "top.hit", referenced_symbol = @s1.$root::@s3.top::@s4.top::@s14.hit, semantic_type = !obelisk.integral<32, true, false, 31 : 0, int>} {
                }
              }
            }
          }
        }
        obelisk.sv.symbol.procedural_block @s19 attributes {hierarchical_name = "top", node_id = 85 : i64, procedure_kind = 2 : i32, time_precision_fs = 1000000 : i64, time_unit_fs = 1000000 : i64} {
          obelisk.sv.statement.concurrent_assertion attributes {assertion_kind = 0 : i32, has_default_disable = false, has_fail_action = true, has_pass_action = true, node_id = 86 : i64} {
            obelisk.sv.assertion.clocking attributes {node_id = 87 : i64} {
              obelisk.sv.timing.signal_event attributes {edge_kind = 1 : i32, has_iff = false, node_id = 88 : i64} {
                obelisk.sv.expression.named_value attributes {is_signed = false, node_id = 89 : i64, referenced_path = "top.clk", referenced_symbol = @s1.$root::@s3.top::@s4.top::@s6.clk, semantic_type = !obelisk.integral<1, false, true, 0 : 0, logic>} {
                }
              }
              obelisk.sv.assertion.abort attributes {action = 0 : i32, is_synchronous = true, node_id = 90 : i64} {
                obelisk.sv.expression.named_value attributes {is_signed = false, node_id = 91 : i64, referenced_path = "top.reset", referenced_symbol = @s1.$root::@s3.top::@s4.top::@s12.reset, semantic_type = !obelisk.integral<1, false, true, 0 : 0, logic>} {
                }
                obelisk.sv.assertion.unary attributes {has_range = false, node_id = 92 : i64, operator_kind = 0 : i32, range_is_unbounded = false} {
                  obelisk.sv.assertion.sequence_concat attributes {delays = [{is_unbounded = false, max = 0 : i64, min = 0 : i64}, {is_unbounded = false, max = 2 : i64, min = 2 : i64}], node_id = 93 : i64} {
                    obelisk.sv.assertion.simple attributes {has_repetition = false, is_null = false, node_id = 94 : i64, repetition_is_unbounded = false} {
                      obelisk.sv.expression.named_value attributes {is_signed = false, node_id = 95 : i64, referenced_path = "top.a", referenced_symbol = @s1.$root::@s3.top::@s4.top::@s8.a, semantic_type = !obelisk.integral<1, false, true, 0 : 0, logic>} {
                      }
                    }
                    obelisk.sv.assertion.simple attributes {has_repetition = false, is_null = false, node_id = 96 : i64, repetition_is_unbounded = false} {
                      obelisk.sv.expression.named_value attributes {is_signed = false, node_id = 97 : i64, referenced_path = "top.b", referenced_symbol = @s1.$root::@s3.top::@s4.top::@s10.b, semantic_type = !obelisk.integral<1, false, true, 0 : 0, logic>} {
                      }
                    }
                  }
                }
              }
            }
            obelisk.sv.statement.expression_statement attributes {node_id = 98 : i64} {
              obelisk.sv.expression.unary_op attributes {is_signed = true, node_id = 99 : i64, operator_kind = 12 : i32, semantic_type = !obelisk.integral<32, true, false, 31 : 0, int>} {
                obelisk.sv.expression.named_value attributes {is_signed = true, node_id = 100 : i64, referenced_path = "top.hit", referenced_symbol = @s1.$root::@s3.top::@s4.top::@s14.hit, semantic_type = !obelisk.integral<32, true, false, 31 : 0, int>} {
                }
              }
            }
            obelisk.sv.statement.expression_statement attributes {node_id = 101 : i64} {
              obelisk.sv.expression.unary_op attributes {is_signed = true, node_id = 102 : i64, operator_kind = 13 : i32, semantic_type = !obelisk.integral<32, true, false, 31 : 0, int>} {
                obelisk.sv.expression.named_value attributes {is_signed = true, node_id = 103 : i64, referenced_path = "top.hit", referenced_symbol = @s1.$root::@s3.top::@s4.top::@s14.hit, semantic_type = !obelisk.integral<32, true, false, 31 : 0, int>} {
                }
              }
            }
          }
        }
        obelisk.sv.symbol.procedural_block @s20 attributes {hierarchical_name = "top", node_id = 104 : i64, procedure_kind = 2 : i32, time_precision_fs = 1000000 : i64, time_unit_fs = 1000000 : i64} {
          obelisk.sv.statement.concurrent_assertion attributes {assertion_kind = 0 : i32, has_default_disable = false, has_fail_action = true, has_pass_action = true, node_id = 105 : i64} {
            obelisk.sv.assertion.clocking attributes {node_id = 106 : i64} {
              obelisk.sv.timing.signal_event attributes {edge_kind = 1 : i32, has_iff = false, node_id = 107 : i64} {
                obelisk.sv.expression.named_value attributes {is_signed = false, node_id = 108 : i64, referenced_path = "top.clk", referenced_symbol = @s1.$root::@s3.top::@s4.top::@s6.clk, semantic_type = !obelisk.integral<1, false, true, 0 : 0, logic>} {
                }
              }
              obelisk.sv.assertion.unary attributes {has_range = false, node_id = 109 : i64, operator_kind = 0 : i32, range_is_unbounded = false} {
                obelisk.sv.assertion.abort attributes {action = 0 : i32, is_synchronous = true, node_id = 110 : i64} {
                  obelisk.sv.expression.named_value attributes {is_signed = false, node_id = 111 : i64, referenced_path = "top.reset", referenced_symbol = @s1.$root::@s3.top::@s4.top::@s12.reset, semantic_type = !obelisk.integral<1, false, true, 0 : 0, logic>} {
                  }
                  obelisk.sv.assertion.sequence_concat attributes {delays = [{is_unbounded = false, max = 0 : i64, min = 0 : i64}, {is_unbounded = false, max = 2 : i64, min = 2 : i64}], node_id = 112 : i64} {
                    obelisk.sv.assertion.simple attributes {has_repetition = false, is_null = false, node_id = 113 : i64, repetition_is_unbounded = false} {
                      obelisk.sv.expression.named_value attributes {is_signed = false, node_id = 114 : i64, referenced_path = "top.a", referenced_symbol = @s1.$root::@s3.top::@s4.top::@s8.a, semantic_type = !obelisk.integral<1, false, true, 0 : 0, logic>} {
                      }
                    }
                    obelisk.sv.assertion.simple attributes {has_repetition = false, is_null = false, node_id = 115 : i64, repetition_is_unbounded = false} {
                      obelisk.sv.expression.named_value attributes {is_signed = false, node_id = 116 : i64, referenced_path = "top.b", referenced_symbol = @s1.$root::@s3.top::@s4.top::@s10.b, semantic_type = !obelisk.integral<1, false, true, 0 : 0, logic>} {
                      }
                    }
                  }
                }
              }
            }
            obelisk.sv.statement.expression_statement attributes {node_id = 117 : i64} {
              obelisk.sv.expression.unary_op attributes {is_signed = true, node_id = 118 : i64, operator_kind = 12 : i32, semantic_type = !obelisk.integral<32, true, false, 31 : 0, int>} {
                obelisk.sv.expression.named_value attributes {is_signed = true, node_id = 119 : i64, referenced_path = "top.hit", referenced_symbol = @s1.$root::@s3.top::@s4.top::@s14.hit, semantic_type = !obelisk.integral<32, true, false, 31 : 0, int>} {
                }
              }
            }
            obelisk.sv.statement.expression_statement attributes {node_id = 120 : i64} {
              obelisk.sv.expression.unary_op attributes {is_signed = true, node_id = 121 : i64, operator_kind = 13 : i32, semantic_type = !obelisk.integral<32, true, false, 31 : 0, int>} {
                obelisk.sv.expression.named_value attributes {is_signed = true, node_id = 122 : i64, referenced_path = "top.hit", referenced_symbol = @s1.$root::@s3.top::@s4.top::@s14.hit, semantic_type = !obelisk.integral<32, true, false, 31 : 0, int>} {
                }
              }
            }
          }
        }
        obelisk.sv.symbol.procedural_block @s21 attributes {hierarchical_name = "top", node_id = 123 : i64, procedure_kind = 2 : i32, time_precision_fs = 1000000 : i64, time_unit_fs = 1000000 : i64} {
          obelisk.sv.statement.concurrent_assertion attributes {assertion_kind = 2 : i32, has_default_disable = false, has_fail_action = false, has_pass_action = true, node_id = 124 : i64} {
            obelisk.sv.assertion.clocking attributes {node_id = 125 : i64} {
              obelisk.sv.timing.signal_event attributes {edge_kind = 1 : i32, has_iff = false, node_id = 126 : i64} {
                obelisk.sv.expression.named_value attributes {is_signed = false, node_id = 127 : i64, referenced_path = "top.clk", referenced_symbol = @s1.$root::@s3.top::@s4.top::@s6.clk, semantic_type = !obelisk.integral<1, false, true, 0 : 0, logic>} {
                }
              }
              obelisk.sv.assertion.abort attributes {action = 1 : i32, is_synchronous = true, node_id = 128 : i64} {
                obelisk.sv.expression.named_value attributes {is_signed = false, node_id = 129 : i64, referenced_path = "top.reset", referenced_symbol = @s1.$root::@s3.top::@s4.top::@s12.reset, semantic_type = !obelisk.integral<1, false, true, 0 : 0, logic>} {
                }
                obelisk.sv.assertion.unary attributes {has_range = false, node_id = 130 : i64, operator_kind = 0 : i32, range_is_unbounded = false} {
                  obelisk.sv.assertion.sequence_concat attributes {delays = [{is_unbounded = false, max = 0 : i64, min = 0 : i64}, {is_unbounded = true, min = 1 : i64}], node_id = 131 : i64} {
                    obelisk.sv.assertion.simple attributes {has_repetition = false, is_null = false, node_id = 132 : i64, repetition_is_unbounded = false} {
                      obelisk.sv.expression.named_value attributes {is_signed = false, node_id = 133 : i64, referenced_path = "top.a", referenced_symbol = @s1.$root::@s3.top::@s4.top::@s8.a, semantic_type = !obelisk.integral<1, false, true, 0 : 0, logic>} {
                      }
                    }
                    obelisk.sv.assertion.simple attributes {has_repetition = false, is_null = false, node_id = 134 : i64, repetition_is_unbounded = false} {
                      obelisk.sv.expression.named_value attributes {is_signed = false, node_id = 135 : i64, referenced_path = "top.b", referenced_symbol = @s1.$root::@s3.top::@s4.top::@s10.b, semantic_type = !obelisk.integral<1, false, true, 0 : 0, logic>} {
                      }
                    }
                  }
                }
              }
            }
            obelisk.sv.statement.expression_statement attributes {node_id = 136 : i64} {
              obelisk.sv.expression.unary_op attributes {is_signed = true, node_id = 137 : i64, operator_kind = 12 : i32, semantic_type = !obelisk.integral<32, true, false, 31 : 0, int>} {
                obelisk.sv.expression.named_value attributes {is_signed = true, node_id = 138 : i64, referenced_path = "top.hit", referenced_symbol = @s1.$root::@s3.top::@s4.top::@s14.hit, semantic_type = !obelisk.integral<32, true, false, 31 : 0, int>} {
                }
              }
            }
          }
        }
        obelisk.sv.symbol.procedural_block @s22 attributes {hierarchical_name = "top", node_id = 139 : i64, procedure_kind = 2 : i32, time_precision_fs = 1000000 : i64, time_unit_fs = 1000000 : i64} {
          obelisk.sv.statement.concurrent_assertion attributes {assertion_kind = 2 : i32, has_default_disable = false, has_fail_action = false, has_pass_action = true, node_id = 140 : i64} {
            obelisk.sv.assertion.clocking attributes {node_id = 141 : i64} {
              obelisk.sv.timing.signal_event attributes {edge_kind = 1 : i32, has_iff = false, node_id = 142 : i64} {
                obelisk.sv.expression.named_value attributes {is_signed = false, node_id = 143 : i64, referenced_path = "top.clk", referenced_symbol = @s1.$root::@s3.top::@s4.top::@s6.clk, semantic_type = !obelisk.integral<1, false, true, 0 : 0, logic>} {
                }
              }
              obelisk.sv.assertion.unary attributes {has_range = false, node_id = 144 : i64, operator_kind = 0 : i32, range_is_unbounded = false} {
                obelisk.sv.assertion.abort attributes {action = 1 : i32, is_synchronous = true, node_id = 145 : i64} {
                  obelisk.sv.expression.named_value attributes {is_signed = false, node_id = 146 : i64, referenced_path = "top.reset", referenced_symbol = @s1.$root::@s3.top::@s4.top::@s12.reset, semantic_type = !obelisk.integral<1, false, true, 0 : 0, logic>} {
                  }
                  obelisk.sv.assertion.sequence_concat attributes {delays = [{is_unbounded = false, max = 0 : i64, min = 0 : i64}, {is_unbounded = true, min = 1 : i64}], node_id = 147 : i64} {
                    obelisk.sv.assertion.simple attributes {has_repetition = false, is_null = false, node_id = 148 : i64, repetition_is_unbounded = false} {
                      obelisk.sv.expression.named_value attributes {is_signed = false, node_id = 149 : i64, referenced_path = "top.a", referenced_symbol = @s1.$root::@s3.top::@s4.top::@s8.a, semantic_type = !obelisk.integral<1, false, true, 0 : 0, logic>} {
                      }
                    }
                    obelisk.sv.assertion.simple attributes {has_repetition = false, is_null = false, node_id = 150 : i64, repetition_is_unbounded = false} {
                      obelisk.sv.expression.named_value attributes {is_signed = false, node_id = 151 : i64, referenced_path = "top.b", referenced_symbol = @s1.$root::@s3.top::@s4.top::@s10.b, semantic_type = !obelisk.integral<1, false, true, 0 : 0, logic>} {
                      }
                    }
                  }
                }
              }
            }
            obelisk.sv.statement.expression_statement attributes {node_id = 152 : i64} {
              obelisk.sv.expression.unary_op attributes {is_signed = true, node_id = 153 : i64, operator_kind = 12 : i32, semantic_type = !obelisk.integral<32, true, false, 31 : 0, int>} {
                obelisk.sv.expression.named_value attributes {is_signed = true, node_id = 154 : i64, referenced_path = "top.hit", referenced_symbol = @s1.$root::@s3.top::@s4.top::@s14.hit, semantic_type = !obelisk.integral<32, true, false, 31 : 0, int>} {
                }
              }
            }
          }
        }
        obelisk.sv.symbol.procedural_block @s23 attributes {hierarchical_name = "top", node_id = 155 : i64, procedure_kind = 2 : i32, time_precision_fs = 1000000 : i64, time_unit_fs = 1000000 : i64} {
          obelisk.sv.statement.concurrent_assertion attributes {assertion_kind = 0 : i32, has_default_disable = false, has_fail_action = true, has_pass_action = true, node_id = 156 : i64} {
            obelisk.sv.assertion.clocking attributes {node_id = 157 : i64} {
              obelisk.sv.timing.signal_event attributes {edge_kind = 1 : i32, has_iff = false, node_id = 158 : i64} {
                obelisk.sv.expression.named_value attributes {is_signed = false, node_id = 159 : i64, referenced_path = "top.clk", referenced_symbol = @s1.$root::@s3.top::@s4.top::@s6.clk, semantic_type = !obelisk.integral<1, false, true, 0 : 0, logic>} {
                }
              }
              obelisk.sv.assertion.abort attributes {action = 0 : i32, is_synchronous = false, node_id = 160 : i64} {
                obelisk.sv.expression.named_value attributes {is_signed = false, node_id = 161 : i64, referenced_path = "top.reset", referenced_symbol = @s1.$root::@s3.top::@s4.top::@s12.reset, semantic_type = !obelisk.integral<1, false, true, 0 : 0, logic>} {
                }
                obelisk.sv.assertion.unary attributes {has_range = false, node_id = 162 : i64, operator_kind = 0 : i32, range_is_unbounded = false} {
                  obelisk.sv.assertion.sequence_concat attributes {delays = [{is_unbounded = false, max = 0 : i64, min = 0 : i64}, {is_unbounded = true, min = 1 : i64}], node_id = 163 : i64} {
                    obelisk.sv.assertion.simple attributes {has_repetition = false, is_null = false, node_id = 164 : i64, repetition_is_unbounded = false} {
                      obelisk.sv.expression.named_value attributes {is_signed = false, node_id = 165 : i64, referenced_path = "top.a", referenced_symbol = @s1.$root::@s3.top::@s4.top::@s8.a, semantic_type = !obelisk.integral<1, false, true, 0 : 0, logic>} {
                      }
                    }
                    obelisk.sv.assertion.simple attributes {has_repetition = false, is_null = false, node_id = 166 : i64, repetition_is_unbounded = false} {
                      obelisk.sv.expression.named_value attributes {is_signed = false, node_id = 167 : i64, referenced_path = "top.b", referenced_symbol = @s1.$root::@s3.top::@s4.top::@s10.b, semantic_type = !obelisk.integral<1, false, true, 0 : 0, logic>} {
                      }
                    }
                  }
                }
              }
            }
            obelisk.sv.statement.expression_statement attributes {node_id = 168 : i64} {
              obelisk.sv.expression.unary_op attributes {is_signed = true, node_id = 169 : i64, operator_kind = 12 : i32, semantic_type = !obelisk.integral<32, true, false, 31 : 0, int>} {
                obelisk.sv.expression.named_value attributes {is_signed = true, node_id = 170 : i64, referenced_path = "top.hit", referenced_symbol = @s1.$root::@s3.top::@s4.top::@s14.hit, semantic_type = !obelisk.integral<32, true, false, 31 : 0, int>} {
                }
              }
            }
            obelisk.sv.statement.expression_statement attributes {node_id = 171 : i64} {
              obelisk.sv.expression.unary_op attributes {is_signed = true, node_id = 172 : i64, operator_kind = 13 : i32, semantic_type = !obelisk.integral<32, true, false, 31 : 0, int>} {
                obelisk.sv.expression.named_value attributes {is_signed = true, node_id = 173 : i64, referenced_path = "top.hit", referenced_symbol = @s1.$root::@s3.top::@s4.top::@s14.hit, semantic_type = !obelisk.integral<32, true, false, 31 : 0, int>} {
                }
              }
            }
          }
        }
        obelisk.sv.symbol.procedural_block @s24 attributes {hierarchical_name = "top", node_id = 174 : i64, procedure_kind = 2 : i32, time_precision_fs = 1000000 : i64, time_unit_fs = 1000000 : i64} {
          obelisk.sv.statement.concurrent_assertion attributes {assertion_kind = 0 : i32, has_default_disable = false, has_fail_action = true, has_pass_action = true, node_id = 175 : i64} {
            obelisk.sv.assertion.clocking attributes {node_id = 176 : i64} {
              obelisk.sv.timing.signal_event attributes {edge_kind = 1 : i32, has_iff = false, node_id = 177 : i64} {
                obelisk.sv.expression.named_value attributes {is_signed = false, node_id = 178 : i64, referenced_path = "top.clk", referenced_symbol = @s1.$root::@s3.top::@s4.top::@s6.clk, semantic_type = !obelisk.integral<1, false, true, 0 : 0, logic>} {
                }
              }
              obelisk.sv.assertion.unary attributes {has_range = false, node_id = 179 : i64, operator_kind = 0 : i32, range_is_unbounded = false} {
                obelisk.sv.assertion.abort attributes {action = 0 : i32, is_synchronous = false, node_id = 180 : i64} {
                  obelisk.sv.expression.named_value attributes {is_signed = false, node_id = 181 : i64, referenced_path = "top.reset", referenced_symbol = @s1.$root::@s3.top::@s4.top::@s12.reset, semantic_type = !obelisk.integral<1, false, true, 0 : 0, logic>} {
                  }
                  obelisk.sv.assertion.sequence_concat attributes {delays = [{is_unbounded = false, max = 0 : i64, min = 0 : i64}, {is_unbounded = true, min = 1 : i64}], node_id = 182 : i64} {
                    obelisk.sv.assertion.simple attributes {has_repetition = false, is_null = false, node_id = 183 : i64, repetition_is_unbounded = false} {
                      obelisk.sv.expression.named_value attributes {is_signed = false, node_id = 184 : i64, referenced_path = "top.a", referenced_symbol = @s1.$root::@s3.top::@s4.top::@s8.a, semantic_type = !obelisk.integral<1, false, true, 0 : 0, logic>} {
                      }
                    }
                    obelisk.sv.assertion.simple attributes {has_repetition = false, is_null = false, node_id = 185 : i64, repetition_is_unbounded = false} {
                      obelisk.sv.expression.named_value attributes {is_signed = false, node_id = 186 : i64, referenced_path = "top.b", referenced_symbol = @s1.$root::@s3.top::@s4.top::@s10.b, semantic_type = !obelisk.integral<1, false, true, 0 : 0, logic>} {
                      }
                    }
                  }
                }
              }
            }
            obelisk.sv.statement.expression_statement attributes {node_id = 187 : i64} {
              obelisk.sv.expression.unary_op attributes {is_signed = true, node_id = 188 : i64, operator_kind = 12 : i32, semantic_type = !obelisk.integral<32, true, false, 31 : 0, int>} {
                obelisk.sv.expression.named_value attributes {is_signed = true, node_id = 189 : i64, referenced_path = "top.hit", referenced_symbol = @s1.$root::@s3.top::@s4.top::@s14.hit, semantic_type = !obelisk.integral<32, true, false, 31 : 0, int>} {
                }
              }
            }
            obelisk.sv.statement.expression_statement attributes {node_id = 190 : i64} {
              obelisk.sv.expression.unary_op attributes {is_signed = true, node_id = 191 : i64, operator_kind = 13 : i32, semantic_type = !obelisk.integral<32, true, false, 31 : 0, int>} {
                obelisk.sv.expression.named_value attributes {is_signed = true, node_id = 192 : i64, referenced_path = "top.hit", referenced_symbol = @s1.$root::@s3.top::@s4.top::@s14.hit, semantic_type = !obelisk.integral<32, true, false, 31 : 0, int>} {
                }
              }
            }
          }
        }
      }
    }
  }
}

// abort(not(P)) retains the abort operator's forced truth: asynchronous
// accept_on dispatches the pass callback both from its detached observer and
// from the clocked priority path. Ordinary P completion and EOS are still
// inverted by the inner temporal not.
// CHECK-LABEL: simulation.func private @unit_0.$concurrent_eos_report.16.strong(
// CHECK: arith.subi
// CHECK-LABEL: simulation.func private @unit_0.$concurrent_abort.16(
// CHECK-SAME: schedule.concurrent_abort
// CHECK: simulation.observer.bind {{.*}} values(%arg1, %arg2 : !simulation.ref<!simulation.logic<1>>, !simulation.event) captures 1
// CHECK: simulation.suspend.observe
// CHECK: simulation.ref.load
// CHECK: simulation.spawn @unit_0.fork.16.0.0
// CHECK-NOT: simulation.spawn @unit_0.fork.16.1.1
// CHECK: simulation.ref.store
// CHECK-LABEL: simulation.func private @unit_0(
// CHECK-SAME: simulation.asynchronous_property_abort
// CHECK-SAME: simulation.end_of_simulation_strength = "strong"
// CHECK-SAME: simulation.negated_operand_end_of_simulation_strength = "weak"
// CHECK-SAME: simulation.property_abort_action = "accept"
// CHECK-SAME: simulation.temporal_property_negation
// CHECK-NOT: simulation.temporal_property_negation_outside_abort
// CHECK: simulation.context.event %arg0[2305843009213693952]
// CHECK: simulation.suspend.edge
// CHECK: [[U0_RESET:%.*]] = simulation.assert.sampled_read %arg0 from %arg4
// CHECK: [[U0_ABORT:%.*]] = simulation.logic.is_true [[U0_RESET]]
// CHECK: cf.cond_br [[U0_ABORT]]
// CHECK: simulation.spawn @unit_0.fork.16.0.0
// CHECK: simulation.assert.sampled_read

// not(accept_on(P)) instead flips the vacuous forced accept result. The same
// live bits therefore dispatch only the failure callback, while the abort
// operator keeps its lexical accept identity.
// CHECK-LABEL: simulation.func private @unit_1.$concurrent_abort.35(
// CHECK: simulation.observer.bind {{.*}} values(%arg1, %arg2 : !simulation.ref<!simulation.logic<1>>, !simulation.event) captures 1
// CHECK: simulation.suspend.observe
// CHECK: simulation.spawn @unit_1.fork.35.1.1
// CHECK-NOT: simulation.spawn @unit_1.fork.35.0.0
// CHECK: simulation.ref.store
// CHECK-LABEL: simulation.func private @unit_1(
// CHECK-SAME: simulation.asynchronous_property_abort
// CHECK-SAME: simulation.property_abort_action = "accept"
// CHECK-SAME: simulation.temporal_property_negation
// CHECK-SAME: simulation.temporal_property_negation_outside_abort
// CHECK: simulation.context.event %arg0[2305843009213693952]
// CHECK: simulation.suspend.edge
// CHECK: [[U1_RESET:%.*]] = simulation.assert.sampled_read %arg0 from %arg4
// CHECK: [[U1_ABORT:%.*]] = simulation.logic.is_true [[U1_RESET]]
// CHECK: cf.cond_br [[U1_ABORT]]
// CHECK: simulation.spawn @unit_1.fork.35.1.1
// CHECK: simulation.assert.sampled_read

// With cover property, reject_on outside not forces false and has no pass
// callback to dispatch. Moving not outside reject_on turns that same vacuous
// abort into a successful cover evaluation and invokes the pass callback.
// Unit 3 deliberately uses an unqualified one-clock operand: the retained
// abort wrapper, rather than operand horizon or explicit strength, must select
// the temporal route.
// CHECK-LABEL: simulation.func private @unit_2.$concurrent_abort.54(
// CHECK: simulation.observer.bind {{.*}} values(%arg1, %arg2 : !simulation.ref<!simulation.logic<1>>, !simulation.event) captures 1
// CHECK: simulation.suspend.observe
// CHECK-NOT: simulation.spawn
// CHECK: simulation.ref.store
// CHECK-LABEL: simulation.func private @unit_2(
// CHECK-SAME: simulation.property_abort_action = "reject"
// CHECK-SAME: simulation.temporal_property_negation
// CHECK: simulation.context.event %arg0[2305843009213693952]
// CHECK: simulation.suspend.edge
// CHECK: cf.cond_br
// CHECK-NOT: simulation.spawn @unit_2.fork.54.0.0
// CHECK: simulation.assert.sampled_read
// CHECK-NOT: @unit_3.$concurrent_eos
// CHECK-LABEL: simulation.func private @unit_3.$concurrent_abort.70(
// CHECK: simulation.observer.bind {{.*}} values(%arg1, %arg2 : !simulation.ref<!simulation.logic<1>>, !simulation.event) captures 1
// CHECK: simulation.suspend.observe
// CHECK-NOT: simulation.spawn @unit_3.fork.70.0.0
// CHECK: simulation.ref.store
// CHECK-LABEL: simulation.func private @unit_3(
// CHECK-SAME: simulation.property_abort_action = "reject"
// CHECK-SAME: simulation.temporal_property_negation
// CHECK-SAME: simulation.temporal_property_negation_outside_abort
// CHECK-NOT: @unit_3.$concurrent_eos
// CHECK: simulation.context.event %arg0[2305843009213693952]
// CHECK: simulation.suspend.edge
// CHECK: cf.cond_br
// CHECK: simulation.spawn @unit_3.fork.70.0.0
// CHECK: simulation.assert.sampled_read
// CHECK-NOT: @unit_3.$concurrent_eos

// Synchronous accept_on has no detached observer. Its sampled clock-tick
// priority path makes the same lexical distinction: inner not keeps accept as
// pass, outer not changes it to fail.
// CHECK-LABEL: simulation.func private @unit_4(
// CHECK-SAME: simulation.property_abort_action = "accept"
// CHECK-SAME: simulation.synchronous_property_abort
// CHECK-SAME: simulation.temporal_property_negation
// CHECK: simulation.suspend.edge
// CHECK: [[U4_RESET:%.*]] = simulation.assert.sampled_read %arg0 from %arg4
// CHECK: [[U4_ABORT:%.*]] = simulation.logic.is_true [[U4_RESET]]
// CHECK: cf.cond_br [[U4_ABORT]]
// CHECK: simulation.spawn @unit_4.fork.86.0.0
// CHECK: simulation.assert.sampled_read
// CHECK-LABEL: simulation.func private @unit_5(
// CHECK-SAME: simulation.property_abort_action = "accept"
// CHECK-SAME: simulation.synchronous_property_abort
// CHECK-SAME: simulation.temporal_property_negation
// CHECK-SAME: simulation.temporal_property_negation_outside_abort
// CHECK: simulation.suspend.edge
// CHECK: [[U5_RESET:%.*]] = simulation.assert.sampled_read %arg0 from %arg4
// CHECK: [[U5_ABORT:%.*]] = simulation.logic.is_true [[U5_RESET]]
// CHECK: cf.cond_br [[U5_ABORT]]
// CHECK: simulation.spawn @unit_5.fork.105.1.1
// CHECK: simulation.assert.sampled_read

// Persistent sync_reject_on uses the counted dispatcher. Inner not leaves the
// forced rejection false, so a cover has no callback; outer not makes every
// cleared token a vacuous cover success and emits one pass per count.
// CHECK-LABEL: simulation.func private @unit_6.$concurrent_abort_count.124.reject(
// CHECK-SAME: simulation.concurrent_abort_counted
// CHECK: [[U6_ZERO:%.*]] = arith.constant 0 : i64
// CHECK: simulation.ref.store [[U6_ZERO]] to %arg1
// CHECK: simulation.ref.load %arg2
// CHECK: simulation.ref.store [[U6_ZERO]] to %arg2
// CHECK-NOT: simulation.ref.load
// CHECK-NOT: simulation.ref.store
// CHECK-NOT: simulation.spawn
// CHECK: simulation.return
// CHECK-LABEL: simulation.func private @unit_6(
// CHECK-SAME: simulation.persistent_delay_monitor
// CHECK-SAME: simulation.property_abort_action = "reject"
// CHECK-SAME: simulation.synchronous_property_abort
// CHECK-SAME: simulation.temporal_property_negation
// CHECK: simulation.call @unit_6.$concurrent_abort_count.124.reject
// CHECK-LABEL: simulation.func private @unit_7.$concurrent_abort_count.140.reject(
// CHECK-SAME: simulation.concurrent_abort_counted
// CHECK: [[U7_ZERO:%.*]] = arith.constant 0 : i64
// CHECK: simulation.ref.load %arg1
// CHECK: simulation.ref.store [[U7_ZERO]] to %arg1
// CHECK: simulation.ref.load %arg2
// CHECK: simulation.ref.store [[U7_ZERO]] to %arg2
// CHECK-NOT: simulation.ref.load
// CHECK-NOT: simulation.ref.store
// CHECK: arith.cmpi ne
// CHECK: simulation.spawn @unit_7.fork.140.0.0
// CHECK-LABEL: simulation.func private @unit_7(
// CHECK-SAME: simulation.persistent_delay_monitor
// CHECK-SAME: simulation.property_abort_action = "reject"
// CHECK-SAME: simulation.synchronous_property_abort
// CHECK-SAME: simulation.temporal_property_negation
// CHECK-SAME: simulation.temporal_property_negation_outside_abort
// CHECK: simulation.call @unit_7.$concurrent_abort_count.140.reject

// The asynchronous counted path makes the complementary accept decision in a
// shared dispatcher used by both the observer and already-true clock path.
// abort(not(P)) dispatches pass; not(abort(P)) dispatches fail.
// CHECK-LABEL: simulation.func private @unit_8.$concurrent_abort_count.156.accept(
// CHECK-SAME: simulation.concurrent_abort_counted
// CHECK: simulation.ref.load %arg1
// CHECK: simulation.ref.store {{.*}} to %arg1
// CHECK: simulation.ref.load %arg2
// CHECK: simulation.ref.store {{.*}} to %arg2
// CHECK-NOT: simulation.ref.load
// CHECK-NOT: simulation.ref.store
// CHECK: simulation.spawn @unit_8.fork.156.0.0
// CHECK-LABEL: simulation.func private @unit_8.$concurrent_abort.156(
// CHECK-SAME: simulation.concurrent_abort_counted
// CHECK: simulation.observer.bind {{.*}} values(%arg1, %arg2 : !simulation.ref<!simulation.logic<1>>, !simulation.event) captures 1
// CHECK: [[U8_OBSERVER_ZERO:%.*]] = arith.constant {{.*}}0 : i64
// CHECK: simulation.call @unit_8.$concurrent_abort_count.156.accept({{.*}}, [[U8_OBSERVER_ZERO]])
// CHECK-LABEL: simulation.func private @unit_8(
// CHECK-SAME: simulation.asynchronous_property_abort
// CHECK-SAME: simulation.property_abort_action = "accept"
// CHECK-SAME: simulation.temporal_property_negation
// CHECK: simulation.context.event %arg0[2305843009213693952]
// CHECK: simulation.spawn @unit_8.$concurrent_abort.156
// CHECK: [[U8_CLOCK_ONE:%.*]] = arith.constant {{.*}}1 : i64
// CHECK: simulation.call @unit_8.$concurrent_abort_count.156.accept({{.*}}, [[U8_CLOCK_ONE]])
// CHECK-LABEL: simulation.func private @unit_9.$concurrent_abort_count.175.accept(
// CHECK-SAME: simulation.concurrent_abort_counted
// CHECK: simulation.ref.load %arg1
// CHECK: simulation.ref.store {{.*}} to %arg1
// CHECK: simulation.ref.load %arg2
// CHECK: simulation.ref.store {{.*}} to %arg2
// CHECK-NOT: simulation.ref.load
// CHECK-NOT: simulation.ref.store
// CHECK: simulation.spawn @unit_9.fork.175.1.1
// CHECK-LABEL: simulation.func private @unit_9.$concurrent_abort.175(
// CHECK: simulation.observer.bind {{.*}} values(%arg1, %arg2 : !simulation.ref<!simulation.logic<1>>, !simulation.event) captures 1
// CHECK: [[U9_OBSERVER_ZERO:%.*]] = arith.constant {{.*}}0 : i64
// CHECK: simulation.call @unit_9.$concurrent_abort_count.175.accept({{.*}}, [[U9_OBSERVER_ZERO]])
// CHECK-LABEL: simulation.func private @unit_9(
// CHECK-SAME: simulation.asynchronous_property_abort
// CHECK-SAME: simulation.property_abort_action = "accept"
// CHECK-SAME: simulation.temporal_property_negation
// CHECK-SAME: simulation.temporal_property_negation_outside_abort
// CHECK: simulation.context.event %arg0[2305843009213693952]
// CHECK: simulation.spawn @unit_9.$concurrent_abort.175
// CHECK: [[U9_CLOCK_ONE:%.*]] = arith.constant {{.*}}1 : i64
// CHECK: simulation.call @unit_9.$concurrent_abort_count.175.accept({{.*}}, [[U9_CLOCK_ONE]])

// Exactly the six asynchronous compositions have observer evaluators. Every
// one reads reset from the canonical Preponed plane; no raw condition load is
// used by the detached evaluator. The four synchronous compositions sample
// only in their assertion-clock monitors.
// CHECK-COUNT-6: simulation.assert.sampled_read %arg0 from %arg1
// CHECK-NOT: simulation.assert.sampled_read %arg0 from %arg1
