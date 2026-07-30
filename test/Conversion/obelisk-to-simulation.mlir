// RUN: obelisk-opt %s '--lower-obelisk-to-sim=opt-level=0' > %t.threaded.mlir
// RUN: obelisk-opt %s '--lower-obelisk-to-sim=opt-level=0' --mlir-disable-threading > %t.single.mlir
// RUN: diff -u %t.single.mlir %t.threaded.mlir
// RUN: obelisk-opt %s '--lower-obelisk-to-sim=workers=2 vpi=read' | FileCheck %s --check-prefix=OPTIONS
// RUN: FileCheck %s --check-prefix=SIM < %t.threaded.mlir
// RUN: FileCheck %s --check-prefix=FINAL < %t.threaded.mlir
// RUN: FileCheck %s --check-prefix=SCCP --implicit-check-not=123456789 < %t.threaded.mlir
// RUN: FileCheck %s --check-prefix=COPYBACK < %t.threaded.mlir

module {
  obelisk.sv.symbol.definition attributes {definition_kind = 0 : i32, hierarchical_name = "sim_child", name = "sim_child", node_id = 0 : i64, sym_name = "s0.sim_child"} {
  }
  obelisk.sv.symbol.definition attributes {definition_kind = 0 : i32, hierarchical_name = "sim_e2e", name = "sim_e2e", node_id = 1 : i64, sym_name = "s1.sim_e2e"} {
  }
  obelisk.sv.symbol.definition attributes {definition_kind = 0 : i32, hierarchical_name = "sim_expression_and_arguments", name = "sim_expression_and_arguments", node_id = 2 : i64, sym_name = "s2.sim_expression_and_arguments"} {
  }
  obelisk.sv.symbol.definition attributes {definition_kind = 0 : i32, hierarchical_name = "sim_fast_timescale", name = "sim_fast_timescale", node_id = 3 : i64, sym_name = "s3.sim_fast_timescale"} {
  }
  obelisk.sv.symbol.definition attributes {definition_kind = 0 : i32, hierarchical_name = "sim_sccp_pipeline", name = "sim_sccp_pipeline", node_id = 4 : i64, sym_name = "s4.sim_sccp_pipeline"} {
  }
  obelisk.sv.symbol.root attributes {hierarchical_name = "\\$root ", name = "$root", node_id = 5 : i64, sym_name = "s5.$root"} {
    obelisk.sv.symbol.compilation_unit attributes {hierarchical_name = "$unit", node_id = 6 : i64, sym_name = "s6"} {
    }
    obelisk.sv.symbol.instance attributes {hierarchical_name = "sim_e2e", is_uninstantiated = false, name = "sim_e2e", node_id = 7 : i64, referenced_path = "sim_e2e", referenced_symbol = @s1.sim_e2e, sym_name = "s7.sim_e2e"} {
      obelisk.sv.symbol.instance_body attributes {hierarchical_name = "sim_e2e", name = "sim_e2e", node_id = 8 : i64, sym_name = "s8.sim_e2e"} {
        obelisk.sv.symbol.variable attributes {hierarchical_name = "sim_e2e.clk", lifetime = 1 : i32, name = "clk", node_id = 9 : i64, semantic_type = !obelisk.integral<1, false, true, 0 : 0, logic>, sym_name = "s9.clk"} {
        }
        obelisk.sv.symbol.variable attributes {hierarchical_name = "sim_e2e.ready", lifetime = 1 : i32, name = "ready", node_id = 10 : i64, semantic_type = !obelisk.integral<1, false, true, 0 : 0, logic>, sym_name = "s10.ready"} {
        }
        obelisk.sv.symbol.variable attributes {hierarchical_name = "sim_e2e.input_value", lifetime = 1 : i32, name = "input_value", node_id = 11 : i64, semantic_type = !obelisk.ranged_packed_array<7 : 0 x !obelisk.integral<1, false, true, 0 : 0, logic>>, sym_name = "s11.input_value"} {
        }
        obelisk.sv.symbol.variable attributes {hierarchical_name = "sim_e2e.input_b", lifetime = 1 : i32, name = "input_b", node_id = 12 : i64, semantic_type = !obelisk.ranged_packed_array<7 : 0 x !obelisk.integral<1, false, true, 0 : 0, logic>>, sym_name = "s12.input_b"} {
        }
        obelisk.sv.symbol.variable attributes {hierarchical_name = "sim_e2e.state", lifetime = 1 : i32, name = "state", node_id = 13 : i64, semantic_type = !obelisk.ranged_packed_array<7 : 0 x !obelisk.integral<1, false, true, 0 : 0, logic>>, sym_name = "s13.state"} {
        }
        obelisk.sv.symbol.variable attributes {hierarchical_name = "sim_e2e.signed_a", lifetime = 1 : i32, name = "signed_a", node_id = 14 : i64, semantic_type = !obelisk.ranged_packed_array<7 : 0 x !obelisk.integral<1, true, true, 0 : 0, logic>>, sym_name = "s14.signed_a"} {
        }
        obelisk.sv.symbol.variable attributes {hierarchical_name = "sim_e2e.signed_b", lifetime = 1 : i32, name = "signed_b", node_id = 15 : i64, semantic_type = !obelisk.ranged_packed_array<7 : 0 x !obelisk.integral<1, true, true, 0 : 0, logic>>, sym_name = "s15.signed_b"} {
        }
        obelisk.sv.symbol.variable attributes {hierarchical_name = "sim_e2e.signed_lt", lifetime = 1 : i32, name = "signed_lt", node_id = 16 : i64, semantic_type = !obelisk.integral<1, false, true, 0 : 0, logic>, sym_name = "s16.signed_lt"} {
        }
        obelisk.sv.symbol.variable attributes {hierarchical_name = "sim_e2e.mirror", lifetime = 1 : i32, name = "mirror", node_id = 17 : i64, semantic_type = !obelisk.integral<1, false, true, 0 : 0, logic>, sym_name = "s17.mirror"} {
        }
        obelisk.sv.symbol.net attributes {hierarchical_name = "sim_e2e.driven", is_implicit = false, name = "driven", net_kind = 1 : i32, node_id = 18 : i64, semantic_type = !obelisk.ranged_packed_array<7 : 0 x !obelisk.integral<1, false, true, 0 : 0, logic>>, sym_name = "s18.driven"} {
        }
        obelisk.sv.symbol.instance attributes {hierarchical_name = "sim_e2e.child", is_uninstantiated = false, name = "child", node_id = 19 : i64, referenced_path = "sim_child", referenced_symbol = @s0.sim_child, sym_name = "s19.child"} {
          obelisk.sv.port.connection attributes {actual_is_constant = false, direction = 1 : i32, formal_name = "ready", formal_ordinal = 0 : i64, formal_path = "sim_e2e.child.ready", formal_symbol = @s5.$root::@s7.sim_e2e::@s8.sim_e2e::@s19.child::@s20.sim_child::@s21.ready, formal_type = !obelisk.integral<1, false, true, 0 : 0, logic>, internal_path = "sim_e2e.child.ready", internal_symbol = @s5.$root::@s7.sim_e2e::@s8.sim_e2e::@s19.child::@s20.sim_child::@s22.ready, is_ansi = true, is_net = false, node_id = 20 : i64, provenance = 0 : i32} {
          } {
            obelisk.sv.expression.assignment attributes {assignment_kind = 0 : i32, node_id = 21 : i64, semantic_type = !obelisk.integral<1, false, true, 0 : 0, logic>} {
              obelisk.sv.expression.named_value attributes {node_id = 22 : i64, referenced_path = "sim_e2e.ready", referenced_symbol = @s5.$root::@s7.sim_e2e::@s8.sim_e2e::@s10.ready, semantic_type = !obelisk.integral<1, false, true, 0 : 0, logic>} {
              }
              obelisk.sv.expression.empty_argument attributes {node_id = 23 : i64, semantic_type = !obelisk.integral<1, false, true, 0 : 0, logic>} {
              }
            }
          }
          obelisk.sv.symbol.instance_body attributes {hierarchical_name = "sim_e2e.child", name = "sim_child", node_id = 24 : i64, sym_name = "s20.sim_child"} {
            obelisk.sv.symbol.port attributes {direction = 1 : i32, hierarchical_name = "sim_e2e.child.ready", name = "ready", node_id = 25 : i64, semantic_type = !obelisk.integral<1, false, true, 0 : 0, logic>, sym_name = "s21.ready"} {
            }
            obelisk.sv.symbol.variable attributes {hierarchical_name = "sim_e2e.child.ready", lifetime = 1 : i32, name = "ready", node_id = 26 : i64, semantic_type = !obelisk.integral<1, false, true, 0 : 0, logic>, sym_name = "s22.ready"} {
            }
            obelisk.sv.symbol.procedural_block attributes {hierarchical_name = "sim_e2e.child", node_id = 27 : i64, procedure_kind = 0 : i32, sym_name = "s23", time_precision_fs = 1000 : i64, time_unit_fs = 1000000 : i64} {
              obelisk.sv.statement.expression_statement attributes {node_id = 28 : i64} {
                obelisk.sv.expression.assignment attributes {assignment_kind = 0 : i32, node_id = 29 : i64, semantic_type = !obelisk.integral<1, false, true, 0 : 0, logic>} {
                  obelisk.sv.expression.named_value attributes {node_id = 30 : i64, referenced_path = "sim_e2e.child.ready", referenced_symbol = @s5.$root::@s7.sim_e2e::@s8.sim_e2e::@s19.child::@s20.sim_child::@s22.ready, semantic_type = !obelisk.integral<1, false, true, 0 : 0, logic>} {
                  }
                  obelisk.sv.expression.conversion attributes {node_id = 31 : i64, semantic_type = !obelisk.integral<1, false, true, 0 : 0, logic>} {
                    obelisk.sv.expression.integer_literal attributes {constant_value = "1'b1", node_id = 32 : i64, semantic_type = !obelisk.ranged_packed_array<0 : 0 x !obelisk.integral<1, false, false, 0 : 0, bit>>} {
                    }
                  }
                }
              }
            }
          }
        }
        obelisk.sv.symbol.continuous_assign attributes {hierarchical_name = "sim_e2e", node_id = 33 : i64, sym_name = "s24", time_precision_fs = 1000 : i64, time_unit_fs = 1000000 : i64} {
          obelisk.sv.expression.assignment attributes {assignment_kind = 0 : i32, node_id = 34 : i64, semantic_type = !obelisk.ranged_packed_array<7 : 0 x !obelisk.integral<1, false, true, 0 : 0, logic>>} {
            obelisk.sv.expression.named_value attributes {node_id = 35 : i64, referenced_path = "sim_e2e.driven", referenced_symbol = @s5.$root::@s7.sim_e2e::@s8.sim_e2e::@s18.driven, semantic_type = !obelisk.ranged_packed_array<7 : 0 x !obelisk.integral<1, false, true, 0 : 0, logic>>} {
            }
            obelisk.sv.expression.binary_op attributes {node_id = 36 : i64, operator_kind = 0 : i32, semantic_type = !obelisk.ranged_packed_array<7 : 0 x !obelisk.integral<1, false, true, 0 : 0, logic>>} {
              obelisk.sv.expression.named_value attributes {node_id = 37 : i64, referenced_path = "sim_e2e.input_value", referenced_symbol = @s5.$root::@s7.sim_e2e::@s8.sim_e2e::@s11.input_value, semantic_type = !obelisk.ranged_packed_array<7 : 0 x !obelisk.integral<1, false, true, 0 : 0, logic>>} {
              }
              obelisk.sv.expression.named_value attributes {node_id = 38 : i64, referenced_path = "sim_e2e.input_b", referenced_symbol = @s5.$root::@s7.sim_e2e::@s8.sim_e2e::@s12.input_b, semantic_type = !obelisk.ranged_packed_array<7 : 0 x !obelisk.integral<1, false, true, 0 : 0, logic>>} {
              }
            }
          }
        }
        obelisk.sv.symbol.subroutine attributes {hierarchical_name = "sim_e2e.increment", name = "increment", node_id = 39 : i64, return_variable_path = "sim_e2e.increment.increment", return_variable_symbol = @s5.$root::@s7.sim_e2e::@s8.sim_e2e::@s25.increment::@s28.increment, semantic_type = !obelisk.subroutine<(!obelisk.ranged_packed_array<7 : 0 x !obelisk.integral<1, false, true, 0 : 0, logic>>, !obelisk.integral<1, false, true, 0 : 0, logic>) -> !obelisk.ranged_packed_array<7 : 0 x !obelisk.integral<1, false, true, 0 : 0, logic>>, false>, subroutine_kind = 0 : i32, sym_name = "s25.increment", time_precision_fs = 1000 : i64, time_unit_fs = 1000000 : i64} {
          obelisk.sv.statement.list attributes {node_id = 40 : i64} {
            obelisk.sv.statement.expression_statement attributes {node_id = 41 : i64} {
              obelisk.sv.expression.assignment attributes {assignment_kind = 0 : i32, node_id = 42 : i64, semantic_type = !obelisk.integral<1, false, true, 0 : 0, logic>} {
                obelisk.sv.expression.named_value attributes {node_id = 43 : i64, referenced_path = "sim_e2e.increment.echoed", referenced_symbol = @s5.$root::@s7.sim_e2e::@s8.sim_e2e::@s25.increment::@s27.echoed, semantic_type = !obelisk.integral<1, false, true, 0 : 0, logic>} {
                }
                obelisk.sv.expression.element_select attributes {node_id = 44 : i64, semantic_type = !obelisk.integral<1, false, true, 0 : 0, logic>} {
                  obelisk.sv.expression.named_value attributes {node_id = 45 : i64, referenced_path = "sim_e2e.increment.value", referenced_symbol = @s5.$root::@s7.sim_e2e::@s8.sim_e2e::@s25.increment::@s26.value, semantic_type = !obelisk.ranged_packed_array<7 : 0 x !obelisk.integral<1, false, true, 0 : 0, logic>>} {
                  }
                  obelisk.sv.expression.integer_literal attributes {constant_value = "0", node_id = 46 : i64, semantic_type = !obelisk.integral<32, true, false, 31 : 0, int>} {
                  }
                }
              }
            }
            obelisk.sv.statement.expression_statement attributes {node_id = 47 : i64} {
              obelisk.sv.expression.assignment attributes {assignment_kind = 0 : i32, node_id = 48 : i64, semantic_type = !obelisk.ranged_packed_array<7 : 0 x !obelisk.integral<1, false, true, 0 : 0, logic>>} {
                obelisk.sv.expression.named_value attributes {node_id = 49 : i64, referenced_path = "sim_e2e.increment.increment", referenced_symbol = @s5.$root::@s7.sim_e2e::@s8.sim_e2e::@s25.increment::@s28.increment, semantic_type = !obelisk.ranged_packed_array<7 : 0 x !obelisk.integral<1, false, true, 0 : 0, logic>>} {
                }
                obelisk.sv.expression.conversion attributes {node_id = 50 : i64, semantic_type = !obelisk.ranged_packed_array<7 : 0 x !obelisk.integral<1, false, true, 0 : 0, logic>>} {
                  obelisk.sv.expression.binary_op attributes {node_id = 51 : i64, operator_kind = 0 : i32, semantic_type = !obelisk.ranged_packed_array<31 : 0 x !obelisk.integral<1, false, true, 0 : 0, logic>>} {
                    obelisk.sv.expression.conversion attributes {node_id = 52 : i64, semantic_type = !obelisk.ranged_packed_array<31 : 0 x !obelisk.integral<1, false, true, 0 : 0, logic>>} {
                      obelisk.sv.expression.named_value attributes {node_id = 53 : i64, referenced_path = "sim_e2e.increment.value", referenced_symbol = @s5.$root::@s7.sim_e2e::@s8.sim_e2e::@s25.increment::@s26.value, semantic_type = !obelisk.ranged_packed_array<7 : 0 x !obelisk.integral<1, false, true, 0 : 0, logic>>} {
                      }
                    }
                    obelisk.sv.expression.conversion attributes {node_id = 54 : i64, semantic_type = !obelisk.ranged_packed_array<31 : 0 x !obelisk.integral<1, false, true, 0 : 0, logic>>} {
                      obelisk.sv.expression.integer_literal attributes {constant_value = "1", node_id = 55 : i64, semantic_type = !obelisk.integral<32, true, false, 31 : 0, int>} {
                      }
                    }
                  }
                }
              }
            }
          }
          obelisk.sv.symbol.formal_argument attributes {direction = 0 : i32, hierarchical_name = "sim_e2e.increment.value", name = "value", node_id = 56 : i64, semantic_type = !obelisk.ranged_packed_array<7 : 0 x !obelisk.integral<1, false, true, 0 : 0, logic>>, sym_name = "s26.value"} {
          }
          obelisk.sv.symbol.formal_argument attributes {direction = 1 : i32, hierarchical_name = "sim_e2e.increment.echoed", name = "echoed", node_id = 57 : i64, semantic_type = !obelisk.integral<1, false, true, 0 : 0, logic>, sym_name = "s27.echoed"} {
          }
          obelisk.sv.symbol.variable attributes {hierarchical_name = "sim_e2e.increment.increment", is_compiler_generated, name = "increment", node_id = 58 : i64, semantic_type = !obelisk.ranged_packed_array<7 : 0 x !obelisk.integral<1, false, true, 0 : 0, logic>>, sym_name = "s28.increment"} {
          }
        }
        obelisk.sv.symbol.statement_block attributes {block_kind = 0 : i32, hierarchical_name = "sim_e2e", node_id = 59 : i64, sym_name = "s29"} {
          obelisk.sv.symbol.variable attributes {hierarchical_name = "sim_e2e.local_value", name = "local_value", node_id = 60 : i64, semantic_type = !obelisk.ranged_packed_array<7 : 0 x !obelisk.integral<1, false, true, 0 : 0, logic>>, sym_name = "s30.local_value"} {
            obelisk.sv.expression.named_value attributes {node_id = 61 : i64, referenced_path = "sim_e2e.driven", referenced_symbol = @s5.$root::@s7.sim_e2e::@s8.sim_e2e::@s18.driven, semantic_type = !obelisk.ranged_packed_array<7 : 0 x !obelisk.integral<1, false, true, 0 : 0, logic>>} {
            }
          }
        }
        obelisk.sv.symbol.procedural_block attributes {hierarchical_name = "sim_e2e", node_id = 62 : i64, procedure_kind = 3 : i32, sym_name = "s31", time_precision_fs = 1000 : i64, time_unit_fs = 1000000 : i64} {
          obelisk.sv.statement.block attributes {node_id = 63 : i64} {
            obelisk.sv.statement.list attributes {node_id = 64 : i64} {
              obelisk.sv.statement.variable_declaration attributes {node_id = 65 : i64, referenced_path = "sim_e2e.local_value", referenced_symbol = @s5.$root::@s7.sim_e2e::@s8.sim_e2e::@s29::@s30.local_value} {
              }
              obelisk.sv.statement.expression_statement attributes {node_id = 66 : i64} {
                obelisk.sv.expression.assignment attributes {assignment_kind = 0 : i32, node_id = 67 : i64, semantic_type = !obelisk.ranged_packed_array<7 : 0 x !obelisk.integral<1, false, true, 0 : 0, logic>>} {
                  obelisk.sv.expression.named_value attributes {node_id = 68 : i64, referenced_path = "sim_e2e.state", referenced_symbol = @s5.$root::@s7.sim_e2e::@s8.sim_e2e::@s13.state, semantic_type = !obelisk.ranged_packed_array<7 : 0 x !obelisk.integral<1, false, true, 0 : 0, logic>>} {
                  }
                  obelisk.sv.expression.binary_op attributes {node_id = 69 : i64, operator_kind = 0 : i32, semantic_type = !obelisk.ranged_packed_array<7 : 0 x !obelisk.integral<1, false, true, 0 : 0, logic>>} {
                    obelisk.sv.expression.named_value attributes {node_id = 70 : i64, referenced_path = "sim_e2e.local_value", referenced_symbol = @s5.$root::@s7.sim_e2e::@s8.sim_e2e::@s29::@s30.local_value, semantic_type = !obelisk.ranged_packed_array<7 : 0 x !obelisk.integral<1, false, true, 0 : 0, logic>>} {
                    }
                    obelisk.sv.expression.named_value attributes {node_id = 71 : i64, referenced_path = "sim_e2e.input_b", referenced_symbol = @s5.$root::@s7.sim_e2e::@s8.sim_e2e::@s12.input_b, semantic_type = !obelisk.ranged_packed_array<7 : 0 x !obelisk.integral<1, false, true, 0 : 0, logic>>} {
                    }
                  }
                }
              }
            }
          }
        }
        obelisk.sv.symbol.procedural_block attributes {hierarchical_name = "sim_e2e", node_id = 72 : i64, procedure_kind = 3 : i32, sym_name = "s32", time_precision_fs = 1000 : i64, time_unit_fs = 1000000 : i64} {
          obelisk.sv.statement.expression_statement attributes {node_id = 73 : i64} {
            obelisk.sv.expression.assignment attributes {assignment_kind = 0 : i32, node_id = 74 : i64, semantic_type = !obelisk.integral<1, false, true, 0 : 0, logic>} {
              obelisk.sv.expression.named_value attributes {node_id = 75 : i64, referenced_path = "sim_e2e.signed_lt", referenced_symbol = @s5.$root::@s7.sim_e2e::@s8.sim_e2e::@s16.signed_lt, semantic_type = !obelisk.integral<1, false, true, 0 : 0, logic>} {
              }
              obelisk.sv.expression.binary_op attributes {node_id = 76 : i64, operator_kind = 16 : i32, semantic_type = !obelisk.integral<1, false, true, 0 : 0, logic>} {
                obelisk.sv.expression.named_value attributes {node_id = 77 : i64, referenced_path = "sim_e2e.signed_a", referenced_symbol = @s5.$root::@s7.sim_e2e::@s8.sim_e2e::@s14.signed_a, semantic_type = !obelisk.ranged_packed_array<7 : 0 x !obelisk.integral<1, true, true, 0 : 0, logic>>} {
                }
                obelisk.sv.expression.named_value attributes {node_id = 78 : i64, referenced_path = "sim_e2e.signed_b", referenced_symbol = @s5.$root::@s7.sim_e2e::@s8.sim_e2e::@s15.signed_b, semantic_type = !obelisk.ranged_packed_array<7 : 0 x !obelisk.integral<1, true, true, 0 : 0, logic>>} {
                }
              }
            }
          }
        }
        obelisk.sv.symbol.procedural_block attributes {hierarchical_name = "sim_e2e", node_id = 79 : i64, procedure_kind = 3 : i32, sym_name = "s33", time_precision_fs = 1000 : i64, time_unit_fs = 1000000 : i64} {
          obelisk.sv.statement.expression_statement attributes {node_id = 80 : i64} {
            obelisk.sv.expression.assignment attributes {assignment_kind = 0 : i32, node_id = 81 : i64, semantic_type = !obelisk.integral<1, false, true, 0 : 0, logic>} {
              obelisk.sv.expression.named_value attributes {node_id = 82 : i64, referenced_path = "sim_e2e.mirror", referenced_symbol = @s5.$root::@s7.sim_e2e::@s8.sim_e2e::@s17.mirror, semantic_type = !obelisk.integral<1, false, true, 0 : 0, logic>} {
              }
              obelisk.sv.expression.named_value attributes {node_id = 83 : i64, referenced_path = "sim_e2e.ready", referenced_symbol = @s5.$root::@s7.sim_e2e::@s8.sim_e2e::@s10.ready, semantic_type = !obelisk.integral<1, false, true, 0 : 0, logic>} {
              }
            }
          }
        }
        obelisk.sv.symbol.procedural_block attributes {hierarchical_name = "sim_e2e", node_id = 84 : i64, procedure_kind = 5 : i32, sym_name = "s34", time_precision_fs = 1000 : i64, time_unit_fs = 1000000 : i64} {
          obelisk.sv.statement.timed attributes {node_id = 85 : i64} {
            obelisk.sv.timing.signal_event attributes {edge_kind = 1 : i32, has_iff = false, node_id = 86 : i64} {
              obelisk.sv.expression.named_value attributes {node_id = 87 : i64, referenced_path = "sim_e2e.clk", referenced_symbol = @s5.$root::@s7.sim_e2e::@s8.sim_e2e::@s9.clk, semantic_type = !obelisk.integral<1, false, true, 0 : 0, logic>} {
              }
            }
            obelisk.sv.statement.block attributes {node_id = 88 : i64} {
              obelisk.sv.statement.conditional attributes {check_kind = 0 : i32, condition_count = 1 : i64, condition_pattern_flags = array<i64: 0>, has_else = true, node_id = 89 : i64} {
                obelisk.sv.expression.named_value attributes {node_id = 90 : i64, referenced_path = "sim_e2e.ready", referenced_symbol = @s5.$root::@s7.sim_e2e::@s8.sim_e2e::@s10.ready, semantic_type = !obelisk.integral<1, false, true, 0 : 0, logic>} {
                }
                obelisk.sv.statement.expression_statement attributes {node_id = 91 : i64} {
                  obelisk.sv.expression.assignment attributes {assignment_kind = 1 : i32, node_id = 92 : i64, semantic_type = !obelisk.ranged_packed_array<7 : 0 x !obelisk.integral<1, false, true, 0 : 0, logic>>} {
                    obelisk.sv.expression.named_value attributes {node_id = 93 : i64, referenced_path = "sim_e2e.state", referenced_symbol = @s5.$root::@s7.sim_e2e::@s8.sim_e2e::@s13.state, semantic_type = !obelisk.ranged_packed_array<7 : 0 x !obelisk.integral<1, false, true, 0 : 0, logic>>} {
                    }
                    obelisk.sv.expression.call attributes {argument_count = 2 : i64, callee_name = "increment", constraint_restrictions = [], defaulted_arguments = array<i64: 0, 0>, has_inline_constraints = false, has_iterator_expression = false, has_output_arguments = true, has_this_class = false, is_super_class = false, is_system_call = false, node_id = 94 : i64, referenced_path = "sim_e2e.increment", referenced_symbol = @s5.$root::@s7.sim_e2e::@s8.sim_e2e::@s25.increment, semantic_type = !obelisk.ranged_packed_array<7 : 0 x !obelisk.integral<1, false, true, 0 : 0, logic>>, subroutine_kind = 0 : i32} {
                      obelisk.sv.expression.named_value attributes {node_id = 95 : i64, referenced_path = "sim_e2e.state", referenced_symbol = @s5.$root::@s7.sim_e2e::@s8.sim_e2e::@s13.state, semantic_type = !obelisk.ranged_packed_array<7 : 0 x !obelisk.integral<1, false, true, 0 : 0, logic>>} {
                      }
                      obelisk.sv.expression.assignment attributes {assignment_kind = 0 : i32, node_id = 96 : i64, semantic_type = !obelisk.integral<1, false, true, 0 : 0, logic>} {
                        obelisk.sv.expression.named_value attributes {node_id = 97 : i64, referenced_path = "sim_e2e.ready", referenced_symbol = @s5.$root::@s7.sim_e2e::@s8.sim_e2e::@s10.ready, semantic_type = !obelisk.integral<1, false, true, 0 : 0, logic>} {
                        }
                        obelisk.sv.expression.empty_argument attributes {node_id = 98 : i64, semantic_type = !obelisk.integral<1, false, true, 0 : 0, logic>} {
                        }
                      }
                    }
                  }
                }
                obelisk.sv.statement.expression_statement attributes {node_id = 99 : i64} {
                  obelisk.sv.expression.assignment attributes {assignment_kind = 1 : i32, node_id = 100 : i64, semantic_type = !obelisk.ranged_packed_array<7 : 0 x !obelisk.integral<1, false, true, 0 : 0, logic>>} {
                    obelisk.sv.expression.named_value attributes {node_id = 101 : i64, referenced_path = "sim_e2e.state", referenced_symbol = @s5.$root::@s7.sim_e2e::@s8.sim_e2e::@s13.state, semantic_type = !obelisk.ranged_packed_array<7 : 0 x !obelisk.integral<1, false, true, 0 : 0, logic>>} {
                    }
                    obelisk.sv.expression.conversion attributes {node_id = 102 : i64, semantic_type = !obelisk.ranged_packed_array<7 : 0 x !obelisk.integral<1, false, true, 0 : 0, logic>>} {
                      obelisk.sv.expression.conversion attributes {node_id = 103 : i64, semantic_type = !obelisk.ranged_packed_array<31 : 0 x !obelisk.integral<1, true, true, 0 : 0, logic>>} {
                        obelisk.sv.expression.integer_literal attributes {constant_value = "0", node_id = 104 : i64, semantic_type = !obelisk.integral<32, true, false, 31 : 0, int>} {
                        }
                      }
                    }
                  }
                }
              }
            }
          }
        }
        obelisk.sv.symbol.statement_block attributes {block_kind = 0 : i32, hierarchical_name = "sim_e2e", node_id = 105 : i64, sym_name = "s35"} {
          obelisk.sv.symbol.variable attributes {hierarchical_name = "sim_e2e.delayed", name = "delayed", node_id = 106 : i64, semantic_type = !obelisk.ranged_packed_array<7 : 0 x !obelisk.integral<1, false, true, 0 : 0, logic>>, sym_name = "s36.delayed"} {
            obelisk.sv.expression.named_value attributes {node_id = 107 : i64, referenced_path = "sim_e2e.input_value", referenced_symbol = @s5.$root::@s7.sim_e2e::@s8.sim_e2e::@s11.input_value, semantic_type = !obelisk.ranged_packed_array<7 : 0 x !obelisk.integral<1, false, true, 0 : 0, logic>>} {
            }
          }
          obelisk.sv.symbol.variable attributes {hierarchical_name = "sim_e2e.index", name = "index", node_id = 108 : i64, semantic_type = !obelisk.integral<32, true, false, 31 : 0, int>, sym_name = "s37.index"} {
            obelisk.sv.expression.integer_literal attributes {constant_value = "3", node_id = 109 : i64, semantic_type = !obelisk.integral<32, true, false, 31 : 0, int>} {
            }
          }
        }
        obelisk.sv.symbol.procedural_block attributes {hierarchical_name = "sim_e2e", node_id = 110 : i64, procedure_kind = 0 : i32, sym_name = "s38", time_precision_fs = 1000 : i64, time_unit_fs = 1000000 : i64} {
          obelisk.sv.statement.block attributes {node_id = 111 : i64} {
            obelisk.sv.statement.list attributes {node_id = 112 : i64} {
              obelisk.sv.statement.variable_declaration attributes {node_id = 113 : i64, referenced_path = "sim_e2e.delayed", referenced_symbol = @s5.$root::@s7.sim_e2e::@s8.sim_e2e::@s35::@s36.delayed} {
              }
              obelisk.sv.statement.variable_declaration attributes {node_id = 114 : i64, referenced_path = "sim_e2e.index", referenced_symbol = @s5.$root::@s7.sim_e2e::@s8.sim_e2e::@s35::@s37.index} {
              }
              obelisk.sv.statement.expression_statement attributes {node_id = 115 : i64} {
                obelisk.sv.expression.assignment attributes {assignment_kind = 0 : i32, node_id = 116 : i64, semantic_type = !obelisk.ranged_packed_array<3 : 0 x !obelisk.integral<1, false, true, 0 : 0, logic>>} {
                  obelisk.sv.expression.range_select attributes {node_id = 117 : i64, selection_kind = 0 : i32, semantic_type = !obelisk.ranged_packed_array<3 : 0 x !obelisk.integral<1, false, true, 0 : 0, logic>>} {
                    obelisk.sv.expression.named_value attributes {node_id = 118 : i64, referenced_path = "sim_e2e.state", referenced_symbol = @s5.$root::@s7.sim_e2e::@s8.sim_e2e::@s13.state, semantic_type = !obelisk.ranged_packed_array<7 : 0 x !obelisk.integral<1, false, true, 0 : 0, logic>>} {
                    }
                    obelisk.sv.expression.integer_literal attributes {constant_value = "3", node_id = 119 : i64, semantic_type = !obelisk.integral<32, true, false, 31 : 0, int>} {
                    }
                    obelisk.sv.expression.integer_literal attributes {constant_value = "0", node_id = 120 : i64, semantic_type = !obelisk.integral<32, true, false, 31 : 0, int>} {
                    }
                  }
                  obelisk.sv.expression.replication attributes {node_id = 121 : i64, semantic_type = !obelisk.ranged_packed_array<3 : 0 x !obelisk.integral<1, false, true, 0 : 0, logic>>} {
                    obelisk.sv.expression.integer_literal attributes {constant_value = "2", node_id = 122 : i64, semantic_type = !obelisk.integral<32, true, false, 31 : 0, int>} {
                    }
                    obelisk.sv.expression.concatenation attributes {node_id = 123 : i64, semantic_type = !obelisk.ranged_packed_array<1 : 0 x !obelisk.integral<1, false, true, 0 : 0, logic>>} {
                      obelisk.sv.expression.range_select attributes {node_id = 124 : i64, selection_kind = 0 : i32, semantic_type = !obelisk.ranged_packed_array<1 : 0 x !obelisk.integral<1, false, true, 0 : 0, logic>>} {
                        obelisk.sv.expression.named_value attributes {node_id = 125 : i64, referenced_path = "sim_e2e.input_value", referenced_symbol = @s5.$root::@s7.sim_e2e::@s8.sim_e2e::@s11.input_value, semantic_type = !obelisk.ranged_packed_array<7 : 0 x !obelisk.integral<1, false, true, 0 : 0, logic>>} {
                        }
                        obelisk.sv.expression.integer_literal attributes {constant_value = "1", node_id = 126 : i64, semantic_type = !obelisk.integral<32, true, false, 31 : 0, int>} {
                        }
                        obelisk.sv.expression.integer_literal attributes {constant_value = "0", node_id = 127 : i64, semantic_type = !obelisk.integral<32, true, false, 31 : 0, int>} {
                        }
                      }
                    }
                  }
                }
              }
              obelisk.sv.statement.expression_statement attributes {node_id = 128 : i64} {
                obelisk.sv.expression.assignment attributes {assignment_kind = 0 : i32, node_id = 129 : i64, semantic_type = !obelisk.ranged_packed_array<1 : 0 x !obelisk.integral<1, false, true, 0 : 0, logic>>} {
                  obelisk.sv.expression.range_select attributes {node_id = 130 : i64, selection_kind = 1 : i32, semantic_type = !obelisk.ranged_packed_array<1 : 0 x !obelisk.integral<1, false, true, 0 : 0, logic>>} {
                    obelisk.sv.expression.named_value attributes {node_id = 131 : i64, referenced_path = "sim_e2e.state", referenced_symbol = @s5.$root::@s7.sim_e2e::@s8.sim_e2e::@s13.state, semantic_type = !obelisk.ranged_packed_array<7 : 0 x !obelisk.integral<1, false, true, 0 : 0, logic>>} {
                    }
                    obelisk.sv.expression.named_value attributes {node_id = 132 : i64, referenced_path = "sim_e2e.index", referenced_symbol = @s5.$root::@s7.sim_e2e::@s8.sim_e2e::@s35::@s37.index, semantic_type = !obelisk.integral<32, true, false, 31 : 0, int>} {
                    }
                    obelisk.sv.expression.integer_literal attributes {constant_value = "2", node_id = 133 : i64, semantic_type = !obelisk.integral<32, true, false, 31 : 0, int>} {
                    }
                  }
                  obelisk.sv.expression.range_select attributes {node_id = 134 : i64, selection_kind = 2 : i32, semantic_type = !obelisk.ranged_packed_array<7 : 6 x !obelisk.integral<1, false, true, 0 : 0, logic>>} {
                    obelisk.sv.expression.named_value attributes {node_id = 135 : i64, referenced_path = "sim_e2e.input_value", referenced_symbol = @s5.$root::@s7.sim_e2e::@s8.sim_e2e::@s11.input_value, semantic_type = !obelisk.ranged_packed_array<7 : 0 x !obelisk.integral<1, false, true, 0 : 0, logic>>} {
                    }
                    obelisk.sv.expression.named_value attributes {node_id = 136 : i64, referenced_path = "sim_e2e.index", referenced_symbol = @s5.$root::@s7.sim_e2e::@s8.sim_e2e::@s35::@s37.index, semantic_type = !obelisk.integral<32, true, false, 31 : 0, int>} {
                    }
                    obelisk.sv.expression.integer_literal attributes {constant_value = "2", node_id = 137 : i64, semantic_type = !obelisk.integral<32, true, false, 31 : 0, int>} {
                    }
                  }
                }
              }
              obelisk.sv.statement.while_loop attributes {node_id = 138 : i64} {
                obelisk.sv.expression.binary_op attributes {node_id = 139 : i64, operator_kind = 16 : i32, semantic_type = !obelisk.integral<1, false, true, 0 : 0, logic>} {
                  obelisk.sv.expression.conversion attributes {node_id = 140 : i64, semantic_type = !obelisk.ranged_packed_array<31 : 0 x !obelisk.integral<1, false, true, 0 : 0, logic>>} {
                    obelisk.sv.expression.named_value attributes {node_id = 141 : i64, referenced_path = "sim_e2e.state", referenced_symbol = @s5.$root::@s7.sim_e2e::@s8.sim_e2e::@s13.state, semantic_type = !obelisk.ranged_packed_array<7 : 0 x !obelisk.integral<1, false, true, 0 : 0, logic>>} {
                    }
                  }
                  obelisk.sv.expression.conversion attributes {node_id = 142 : i64, semantic_type = !obelisk.ranged_packed_array<31 : 0 x !obelisk.integral<1, false, true, 0 : 0, logic>>} {
                    obelisk.sv.expression.integer_literal attributes {constant_value = "4", node_id = 143 : i64, semantic_type = !obelisk.integral<32, true, false, 31 : 0, int>} {
                    }
                  }
                }
                obelisk.sv.statement.block attributes {node_id = 144 : i64} {
                  obelisk.sv.statement.list attributes {node_id = 145 : i64} {
                    obelisk.sv.statement.expression_statement attributes {node_id = 146 : i64} {
                      obelisk.sv.expression.assignment attributes {assignment_kind = 0 : i32, node_id = 147 : i64, semantic_type = !obelisk.ranged_packed_array<7 : 0 x !obelisk.integral<1, false, true, 0 : 0, logic>>} {
                        obelisk.sv.expression.named_value attributes {node_id = 148 : i64, referenced_path = "sim_e2e.state", referenced_symbol = @s5.$root::@s7.sim_e2e::@s8.sim_e2e::@s13.state, semantic_type = !obelisk.ranged_packed_array<7 : 0 x !obelisk.integral<1, false, true, 0 : 0, logic>>} {
                        }
                        obelisk.sv.expression.conversion attributes {node_id = 149 : i64, semantic_type = !obelisk.ranged_packed_array<7 : 0 x !obelisk.integral<1, false, true, 0 : 0, logic>>} {
                          obelisk.sv.expression.binary_op attributes {node_id = 150 : i64, operator_kind = 0 : i32, semantic_type = !obelisk.ranged_packed_array<31 : 0 x !obelisk.integral<1, false, true, 0 : 0, logic>>} {
                            obelisk.sv.expression.conversion attributes {node_id = 151 : i64, semantic_type = !obelisk.ranged_packed_array<31 : 0 x !obelisk.integral<1, false, true, 0 : 0, logic>>} {
                              obelisk.sv.expression.named_value attributes {node_id = 152 : i64, referenced_path = "sim_e2e.state", referenced_symbol = @s5.$root::@s7.sim_e2e::@s8.sim_e2e::@s13.state, semantic_type = !obelisk.ranged_packed_array<7 : 0 x !obelisk.integral<1, false, true, 0 : 0, logic>>} {
                              }
                            }
                            obelisk.sv.expression.conversion attributes {node_id = 153 : i64, semantic_type = !obelisk.ranged_packed_array<31 : 0 x !obelisk.integral<1, false, true, 0 : 0, logic>>} {
                              obelisk.sv.expression.integer_literal attributes {constant_value = "1", node_id = 154 : i64, semantic_type = !obelisk.integral<32, true, false, 31 : 0, int>} {
                              }
                            }
                          }
                        }
                      }
                    }
                    obelisk.sv.statement.conditional attributes {check_kind = 0 : i32, condition_count = 1 : i64, condition_pattern_flags = array<i64: 0>, has_else = false, node_id = 155 : i64} {
                      obelisk.sv.expression.binary_op attributes {node_id = 156 : i64, operator_kind = 9 : i32, semantic_type = !obelisk.integral<1, false, true, 0 : 0, logic>} {
                        obelisk.sv.expression.conversion attributes {node_id = 157 : i64, semantic_type = !obelisk.ranged_packed_array<31 : 0 x !obelisk.integral<1, false, true, 0 : 0, logic>>} {
                          obelisk.sv.expression.named_value attributes {node_id = 158 : i64, referenced_path = "sim_e2e.state", referenced_symbol = @s5.$root::@s7.sim_e2e::@s8.sim_e2e::@s13.state, semantic_type = !obelisk.ranged_packed_array<7 : 0 x !obelisk.integral<1, false, true, 0 : 0, logic>>} {
                          }
                        }
                        obelisk.sv.expression.conversion attributes {node_id = 159 : i64, semantic_type = !obelisk.ranged_packed_array<31 : 0 x !obelisk.integral<1, false, true, 0 : 0, logic>>} {
                          obelisk.sv.expression.integer_literal attributes {constant_value = "2", node_id = 160 : i64, semantic_type = !obelisk.integral<32, true, false, 31 : 0, int>} {
                          }
                        }
                      }
                      obelisk.sv.statement.continue attributes {node_id = 161 : i64} {
                      }
                    }
                    obelisk.sv.statement.conditional attributes {check_kind = 0 : i32, condition_count = 1 : i64, condition_pattern_flags = array<i64: 0>, has_else = false, node_id = 162 : i64} {
                      obelisk.sv.expression.binary_op attributes {node_id = 163 : i64, operator_kind = 9 : i32, semantic_type = !obelisk.integral<1, false, true, 0 : 0, logic>} {
                        obelisk.sv.expression.conversion attributes {node_id = 164 : i64, semantic_type = !obelisk.ranged_packed_array<31 : 0 x !obelisk.integral<1, false, true, 0 : 0, logic>>} {
                          obelisk.sv.expression.named_value attributes {node_id = 165 : i64, referenced_path = "sim_e2e.state", referenced_symbol = @s5.$root::@s7.sim_e2e::@s8.sim_e2e::@s13.state, semantic_type = !obelisk.ranged_packed_array<7 : 0 x !obelisk.integral<1, false, true, 0 : 0, logic>>} {
                          }
                        }
                        obelisk.sv.expression.conversion attributes {node_id = 166 : i64, semantic_type = !obelisk.ranged_packed_array<31 : 0 x !obelisk.integral<1, false, true, 0 : 0, logic>>} {
                          obelisk.sv.expression.integer_literal attributes {constant_value = "3", node_id = 167 : i64, semantic_type = !obelisk.integral<32, true, false, 31 : 0, int>} {
                          }
                        }
                      }
                      obelisk.sv.statement.break attributes {node_id = 168 : i64} {
                      }
                    }
                  }
                }
              }
              obelisk.sv.statement.case attributes {check_kind = 0 : i32, condition_kind = 0 : i32, has_default = true, item_count = 2 : i64, item_label_counts = array<i64: 1, 2>, node_id = 169 : i64} {
                obelisk.sv.expression.conversion attributes {node_id = 170 : i64, semantic_type = !obelisk.ranged_packed_array<31 : 0 x !obelisk.integral<1, false, true, 0 : 0, logic>>} {
                  obelisk.sv.expression.named_value attributes {node_id = 171 : i64, referenced_path = "sim_e2e.state", referenced_symbol = @s5.$root::@s7.sim_e2e::@s8.sim_e2e::@s13.state, semantic_type = !obelisk.ranged_packed_array<7 : 0 x !obelisk.integral<1, false, true, 0 : 0, logic>>} {
                  }
                }
                obelisk.sv.expression.conversion attributes {node_id = 172 : i64, semantic_type = !obelisk.ranged_packed_array<31 : 0 x !obelisk.integral<1, false, true, 0 : 0, logic>>} {
                  obelisk.sv.expression.integer_literal attributes {constant_value = "0", node_id = 173 : i64, semantic_type = !obelisk.integral<32, true, false, 31 : 0, int>} {
                  }
                }
                obelisk.sv.expression.conversion attributes {node_id = 174 : i64, semantic_type = !obelisk.ranged_packed_array<31 : 0 x !obelisk.integral<1, false, true, 0 : 0, logic>>} {
                  obelisk.sv.expression.integer_literal attributes {constant_value = "1", node_id = 175 : i64, semantic_type = !obelisk.integral<32, true, false, 31 : 0, int>} {
                  }
                }
                obelisk.sv.expression.conversion attributes {node_id = 176 : i64, semantic_type = !obelisk.ranged_packed_array<31 : 0 x !obelisk.integral<1, false, true, 0 : 0, logic>>} {
                  obelisk.sv.expression.integer_literal attributes {constant_value = "2", node_id = 177 : i64, semantic_type = !obelisk.integral<32, true, false, 31 : 0, int>} {
                  }
                }
                obelisk.sv.statement.expression_statement attributes {node_id = 178 : i64} {
                  obelisk.sv.expression.assignment attributes {assignment_kind = 0 : i32, node_id = 179 : i64, semantic_type = !obelisk.ranged_packed_array<7 : 0 x !obelisk.integral<1, false, true, 0 : 0, logic>>} {
                    obelisk.sv.expression.named_value attributes {node_id = 180 : i64, referenced_path = "sim_e2e.state", referenced_symbol = @s5.$root::@s7.sim_e2e::@s8.sim_e2e::@s13.state, semantic_type = !obelisk.ranged_packed_array<7 : 0 x !obelisk.integral<1, false, true, 0 : 0, logic>>} {
                    }
                    obelisk.sv.expression.conversion attributes {node_id = 181 : i64, semantic_type = !obelisk.ranged_packed_array<7 : 0 x !obelisk.integral<1, false, true, 0 : 0, logic>>} {
                      obelisk.sv.expression.conversion attributes {node_id = 182 : i64, semantic_type = !obelisk.ranged_packed_array<31 : 0 x !obelisk.integral<1, true, true, 0 : 0, logic>>} {
                        obelisk.sv.expression.integer_literal attributes {constant_value = "1", node_id = 183 : i64, semantic_type = !obelisk.integral<32, true, false, 31 : 0, int>} {
                        }
                      }
                    }
                  }
                }
                obelisk.sv.statement.expression_statement attributes {node_id = 184 : i64} {
                  obelisk.sv.expression.assignment attributes {assignment_kind = 0 : i32, node_id = 185 : i64, semantic_type = !obelisk.ranged_packed_array<7 : 0 x !obelisk.integral<1, false, true, 0 : 0, logic>>} {
                    obelisk.sv.expression.named_value attributes {node_id = 186 : i64, referenced_path = "sim_e2e.state", referenced_symbol = @s5.$root::@s7.sim_e2e::@s8.sim_e2e::@s13.state, semantic_type = !obelisk.ranged_packed_array<7 : 0 x !obelisk.integral<1, false, true, 0 : 0, logic>>} {
                    }
                    obelisk.sv.expression.conversion attributes {node_id = 187 : i64, semantic_type = !obelisk.ranged_packed_array<7 : 0 x !obelisk.integral<1, false, true, 0 : 0, logic>>} {
                      obelisk.sv.expression.conversion attributes {node_id = 188 : i64, semantic_type = !obelisk.ranged_packed_array<31 : 0 x !obelisk.integral<1, true, true, 0 : 0, logic>>} {
                        obelisk.sv.expression.integer_literal attributes {constant_value = "3", node_id = 189 : i64, semantic_type = !obelisk.integral<32, true, false, 31 : 0, int>} {
                        }
                      }
                    }
                  }
                }
                obelisk.sv.statement.expression_statement attributes {node_id = 190 : i64} {
                  obelisk.sv.expression.assignment attributes {assignment_kind = 0 : i32, node_id = 191 : i64, semantic_type = !obelisk.ranged_packed_array<7 : 0 x !obelisk.integral<1, false, true, 0 : 0, logic>>} {
                    obelisk.sv.expression.named_value attributes {node_id = 192 : i64, referenced_path = "sim_e2e.state", referenced_symbol = @s5.$root::@s7.sim_e2e::@s8.sim_e2e::@s13.state, semantic_type = !obelisk.ranged_packed_array<7 : 0 x !obelisk.integral<1, false, true, 0 : 0, logic>>} {
                    }
                    obelisk.sv.expression.conversion attributes {node_id = 193 : i64, semantic_type = !obelisk.ranged_packed_array<7 : 0 x !obelisk.integral<1, false, true, 0 : 0, logic>>} {
                      obelisk.sv.expression.conversion attributes {node_id = 194 : i64, semantic_type = !obelisk.ranged_packed_array<31 : 0 x !obelisk.integral<1, true, true, 0 : 0, logic>>} {
                        obelisk.sv.expression.integer_literal attributes {constant_value = "0", node_id = 195 : i64, semantic_type = !obelisk.integral<32, true, false, 31 : 0, int>} {
                        }
                      }
                    }
                  }
                }
              }
              obelisk.sv.statement.timed attributes {node_id = 196 : i64} {
                obelisk.sv.timing.delay attributes {node_id = 197 : i64} {
                  obelisk.sv.expression.integer_literal attributes {constant_value = "5", node_id = 198 : i64, semantic_type = !obelisk.integral<32, true, false, 31 : 0, int>} {
                  }
                }
                obelisk.sv.statement.empty attributes {node_id = 199 : i64} {
                }
              }
              obelisk.sv.statement.expression_statement attributes {node_id = 200 : i64} {
                obelisk.sv.expression.assignment attributes {assignment_kind = 0 : i32, node_id = 201 : i64, semantic_type = !obelisk.ranged_packed_array<7 : 0 x !obelisk.integral<1, false, true, 0 : 0, logic>>} {
                  obelisk.sv.expression.named_value attributes {node_id = 202 : i64, referenced_path = "sim_e2e.state", referenced_symbol = @s5.$root::@s7.sim_e2e::@s8.sim_e2e::@s13.state, semantic_type = !obelisk.ranged_packed_array<7 : 0 x !obelisk.integral<1, false, true, 0 : 0, logic>>} {
                  }
                  obelisk.sv.expression.call attributes {argument_count = 2 : i64, callee_name = "increment", constraint_restrictions = [], defaulted_arguments = array<i64: 0, 0>, has_inline_constraints = false, has_iterator_expression = false, has_output_arguments = true, has_this_class = false, is_super_class = false, is_system_call = false, node_id = 203 : i64, referenced_path = "sim_e2e.increment", referenced_symbol = @s5.$root::@s7.sim_e2e::@s8.sim_e2e::@s25.increment, semantic_type = !obelisk.ranged_packed_array<7 : 0 x !obelisk.integral<1, false, true, 0 : 0, logic>>, subroutine_kind = 0 : i32} {
                    obelisk.sv.expression.named_value attributes {node_id = 204 : i64, referenced_path = "sim_e2e.delayed", referenced_symbol = @s5.$root::@s7.sim_e2e::@s8.sim_e2e::@s35::@s36.delayed, semantic_type = !obelisk.ranged_packed_array<7 : 0 x !obelisk.integral<1, false, true, 0 : 0, logic>>} {
                    }
                    obelisk.sv.expression.assignment attributes {assignment_kind = 0 : i32, node_id = 205 : i64, semantic_type = !obelisk.integral<1, false, true, 0 : 0, logic>} {
                      obelisk.sv.expression.named_value attributes {node_id = 206 : i64, referenced_path = "sim_e2e.ready", referenced_symbol = @s5.$root::@s7.sim_e2e::@s8.sim_e2e::@s10.ready, semantic_type = !obelisk.integral<1, false, true, 0 : 0, logic>} {
                      }
                      obelisk.sv.expression.empty_argument attributes {node_id = 207 : i64, semantic_type = !obelisk.integral<1, false, true, 0 : 0, logic>} {
                      }
                    }
                  }
                }
              }
            }
          }
        }
        obelisk.sv.symbol.procedural_block attributes {hierarchical_name = "sim_e2e", node_id = 208 : i64, procedure_kind = 2 : i32, sym_name = "s39", time_precision_fs = 1000 : i64, time_unit_fs = 1000000 : i64} {
          obelisk.sv.statement.timed attributes {node_id = 209 : i64} {
            obelisk.sv.timing.event_list attributes {event_count = 2 : i64, node_id = 210 : i64} {
              obelisk.sv.timing.signal_event attributes {edge_kind = 1 : i32, has_iff = false, node_id = 211 : i64} {
                obelisk.sv.expression.named_value attributes {node_id = 212 : i64, referenced_path = "sim_e2e.clk", referenced_symbol = @s5.$root::@s7.sim_e2e::@s8.sim_e2e::@s9.clk, semantic_type = !obelisk.integral<1, false, true, 0 : 0, logic>} {
                }
              }
              obelisk.sv.timing.signal_event attributes {edge_kind = 2 : i32, has_iff = false, node_id = 213 : i64} {
                obelisk.sv.expression.named_value attributes {node_id = 214 : i64, referenced_path = "sim_e2e.ready", referenced_symbol = @s5.$root::@s7.sim_e2e::@s8.sim_e2e::@s10.ready, semantic_type = !obelisk.integral<1, false, true, 0 : 0, logic>} {
                }
              }
            }
            obelisk.sv.statement.expression_statement attributes {node_id = 215 : i64} {
              obelisk.sv.expression.assignment attributes {assignment_kind = 1 : i32, node_id = 216 : i64, semantic_type = !obelisk.ranged_packed_array<7 : 0 x !obelisk.integral<1, false, true, 0 : 0, logic>>} {
                obelisk.sv.expression.named_value attributes {node_id = 217 : i64, referenced_path = "sim_e2e.state", referenced_symbol = @s5.$root::@s7.sim_e2e::@s8.sim_e2e::@s13.state, semantic_type = !obelisk.ranged_packed_array<7 : 0 x !obelisk.integral<1, false, true, 0 : 0, logic>>} {
                }
                obelisk.sv.expression.named_value attributes {node_id = 218 : i64, referenced_path = "sim_e2e.input_b", referenced_symbol = @s5.$root::@s7.sim_e2e::@s8.sim_e2e::@s12.input_b, semantic_type = !obelisk.ranged_packed_array<7 : 0 x !obelisk.integral<1, false, true, 0 : 0, logic>>} {
                }
              }
            }
          }
        }
      }
    }
    obelisk.sv.symbol.instance attributes {hierarchical_name = "sim_expression_and_arguments", is_uninstantiated = false, name = "sim_expression_and_arguments", node_id = 219 : i64, referenced_path = "sim_expression_and_arguments", referenced_symbol = @s2.sim_expression_and_arguments, sym_name = "s40.sim_expression_and_arguments"} {
      obelisk.sv.symbol.instance_body attributes {hierarchical_name = "sim_expression_and_arguments", name = "sim_expression_and_arguments", node_id = 220 : i64, sym_name = "s41.sim_expression_and_arguments"} {
        obelisk.sv.symbol.variable attributes {hierarchical_name = "sim_expression_and_arguments.lhs", lifetime = 1 : i32, name = "lhs", node_id = 221 : i64, semantic_type = !obelisk.ranged_packed_array<7 : 0 x !obelisk.integral<1, false, true, 0 : 0, logic>>, sym_name = "s42.lhs"} {
        }
        obelisk.sv.symbol.variable attributes {hierarchical_name = "sim_expression_and_arguments.rhs", lifetime = 1 : i32, name = "rhs", node_id = 222 : i64, semantic_type = !obelisk.ranged_packed_array<7 : 0 x !obelisk.integral<1, false, true, 0 : 0, logic>>, sym_name = "s43.rhs"} {
        }
        obelisk.sv.symbol.variable attributes {hierarchical_name = "sim_expression_and_arguments.logical_result", lifetime = 1 : i32, name = "logical_result", node_id = 223 : i64, semantic_type = !obelisk.integral<1, false, true, 0 : 0, logic>, sym_name = "s44.logical_result"} {
        }
        obelisk.sv.symbol.variable attributes {hierarchical_name = "sim_expression_and_arguments.logical_not_result", lifetime = 1 : i32, name = "logical_not_result", node_id = 224 : i64, semantic_type = !obelisk.integral<1, false, true, 0 : 0, logic>, sym_name = "s45.logical_not_result"} {
        }
        obelisk.sv.symbol.variable attributes {hierarchical_name = "sim_expression_and_arguments.two_state", lifetime = 1 : i32, name = "two_state", node_id = 225 : i64, semantic_type = !obelisk.ranged_packed_array<7 : 0 x !obelisk.integral<1, false, false, 0 : 0, bit>>, sym_name = "s46.two_state"} {
        }
        obelisk.sv.symbol.variable attributes {hierarchical_name = "sim_expression_and_arguments.two_state_not", lifetime = 1 : i32, name = "two_state_not", node_id = 226 : i64, semantic_type = !obelisk.integral<1, false, false, 0 : 0, bit>, sym_name = "s47.two_state_not"} {
        }
        obelisk.sv.symbol.variable attributes {hierarchical_name = "sim_expression_and_arguments.output_source", lifetime = 1 : i32, name = "output_source", node_id = 227 : i64, semantic_type = !obelisk.integral<1, false, true, 0 : 0, logic>, sym_name = "s48.output_source"} {
        }
        obelisk.sv.symbol.variable attributes {hierarchical_name = "sim_expression_and_arguments.inout_source", lifetime = 1 : i32, name = "inout_source", node_id = 228 : i64, semantic_type = !obelisk.integral<1, false, true, 0 : 0, logic>, sym_name = "s49.inout_source"} {
        }
        obelisk.sv.symbol.variable attributes {hierarchical_name = "sim_expression_and_arguments.input_source", lifetime = 1 : i32, name = "input_source", node_id = 229 : i64, semantic_type = !obelisk.integral<1, false, true, 0 : 0, logic>, sym_name = "s50.input_source"} {
        }
        obelisk.sv.symbol.variable attributes {hierarchical_name = "sim_expression_and_arguments.function_result", lifetime = 1 : i32, name = "function_result", node_id = 230 : i64, semantic_type = !obelisk.integral<1, false, true, 0 : 0, logic>, sym_name = "s51.function_result"} {
        }
        obelisk.sv.symbol.subroutine attributes {hierarchical_name = "sim_expression_and_arguments.observe", name = "observe", node_id = 231 : i64, return_variable_path = "sim_expression_and_arguments.observe.observe", return_variable_symbol = @s5.$root::@s40.sim_expression_and_arguments::@s41.sim_expression_and_arguments::@s52.observe::@s56.observe, semantic_type = !obelisk.subroutine<(!obelisk.integral<1, false, true, 0 : 0, logic>, !obelisk.integral<1, false, true, 0 : 0, logic>, !obelisk.integral<1, false, true, 0 : 0, logic>) -> !obelisk.integral<1, false, true, 0 : 0, logic>, false>, subroutine_kind = 0 : i32, sym_name = "s52.observe", time_precision_fs = 1000000 : i64, time_unit_fs = 10000000 : i64} {
          obelisk.sv.statement.list attributes {node_id = 232 : i64} {
            obelisk.sv.statement.expression_statement attributes {node_id = 233 : i64} {
              obelisk.sv.expression.assignment attributes {assignment_kind = 0 : i32, node_id = 234 : i64, semantic_type = !obelisk.integral<1, false, true, 0 : 0, logic>} {
                obelisk.sv.expression.named_value attributes {node_id = 235 : i64, referenced_path = "sim_expression_and_arguments.observe.scratch", referenced_symbol = @s5.$root::@s40.sim_expression_and_arguments::@s41.sim_expression_and_arguments::@s52.observe::@s53.scratch, semantic_type = !obelisk.integral<1, false, true, 0 : 0, logic>} {
                }
                obelisk.sv.expression.unary_op attributes {node_id = 236 : i64, operator_kind = 2 : i32, semantic_type = !obelisk.integral<1, false, true, 0 : 0, logic>} {
                  obelisk.sv.expression.named_value attributes {node_id = 237 : i64, referenced_path = "sim_expression_and_arguments.observe.scratch", referenced_symbol = @s5.$root::@s40.sim_expression_and_arguments::@s41.sim_expression_and_arguments::@s52.observe::@s53.scratch, semantic_type = !obelisk.integral<1, false, true, 0 : 0, logic>} {
                  }
                }
              }
            }
            obelisk.sv.statement.expression_statement attributes {node_id = 238 : i64} {
              obelisk.sv.expression.assignment attributes {assignment_kind = 0 : i32, node_id = 239 : i64, semantic_type = !obelisk.integral<1, false, true, 0 : 0, logic>} {
                obelisk.sv.expression.named_value attributes {node_id = 240 : i64, referenced_path = "sim_expression_and_arguments.observe.copied", referenced_symbol = @s5.$root::@s40.sim_expression_and_arguments::@s41.sim_expression_and_arguments::@s52.observe::@s54.copied, semantic_type = !obelisk.integral<1, false, true, 0 : 0, logic>} {
                }
                obelisk.sv.expression.conversion attributes {node_id = 241 : i64, semantic_type = !obelisk.integral<1, false, true, 0 : 0, logic>} {
                  obelisk.sv.expression.integer_literal attributes {constant_value = "1'b1", node_id = 242 : i64, semantic_type = !obelisk.ranged_packed_array<0 : 0 x !obelisk.integral<1, false, false, 0 : 0, bit>>} {
                  }
                }
              }
            }
            obelisk.sv.statement.expression_statement attributes {node_id = 243 : i64} {
              obelisk.sv.expression.assignment attributes {assignment_kind = 0 : i32, node_id = 244 : i64, semantic_type = !obelisk.integral<1, false, true, 0 : 0, logic>} {
                obelisk.sv.expression.named_value attributes {node_id = 245 : i64, referenced_path = "sim_expression_and_arguments.observe.exchanged", referenced_symbol = @s5.$root::@s40.sim_expression_and_arguments::@s41.sim_expression_and_arguments::@s52.observe::@s55.exchanged, semantic_type = !obelisk.integral<1, false, true, 0 : 0, logic>} {
                }
                obelisk.sv.expression.unary_op attributes {node_id = 246 : i64, operator_kind = 2 : i32, semantic_type = !obelisk.integral<1, false, true, 0 : 0, logic>} {
                  obelisk.sv.expression.named_value attributes {node_id = 247 : i64, referenced_path = "sim_expression_and_arguments.observe.exchanged", referenced_symbol = @s5.$root::@s40.sim_expression_and_arguments::@s41.sim_expression_and_arguments::@s52.observe::@s55.exchanged, semantic_type = !obelisk.integral<1, false, true, 0 : 0, logic>} {
                  }
                }
              }
            }
            obelisk.sv.statement.return attributes {node_id = 248 : i64} {
              obelisk.sv.expression.binary_op attributes {node_id = 249 : i64, operator_kind = 7 : i32, semantic_type = !obelisk.integral<1, false, true, 0 : 0, logic>} {
                obelisk.sv.expression.named_value attributes {node_id = 250 : i64, referenced_path = "sim_expression_and_arguments.observe.scratch", referenced_symbol = @s5.$root::@s40.sim_expression_and_arguments::@s41.sim_expression_and_arguments::@s52.observe::@s53.scratch, semantic_type = !obelisk.integral<1, false, true, 0 : 0, logic>} {
                }
                obelisk.sv.expression.named_value attributes {node_id = 251 : i64, referenced_path = "sim_expression_and_arguments.output_source", referenced_symbol = @s5.$root::@s40.sim_expression_and_arguments::@s41.sim_expression_and_arguments::@s48.output_source, semantic_type = !obelisk.integral<1, false, true, 0 : 0, logic>} {
                }
              }
            }
          }
          obelisk.sv.symbol.formal_argument attributes {direction = 0 : i32, hierarchical_name = "sim_expression_and_arguments.observe.scratch", name = "scratch", node_id = 252 : i64, semantic_type = !obelisk.integral<1, false, true, 0 : 0, logic>, sym_name = "s53.scratch"} {
          }
          obelisk.sv.symbol.formal_argument attributes {direction = 1 : i32, hierarchical_name = "sim_expression_and_arguments.observe.copied", name = "copied", node_id = 253 : i64, semantic_type = !obelisk.integral<1, false, true, 0 : 0, logic>, sym_name = "s54.copied"} {
          }
          obelisk.sv.symbol.formal_argument attributes {direction = 2 : i32, hierarchical_name = "sim_expression_and_arguments.observe.exchanged", name = "exchanged", node_id = 254 : i64, semantic_type = !obelisk.integral<1, false, true, 0 : 0, logic>, sym_name = "s55.exchanged"} {
          }
          obelisk.sv.symbol.variable attributes {hierarchical_name = "sim_expression_and_arguments.observe.observe", is_compiler_generated, name = "observe", node_id = 255 : i64, semantic_type = !obelisk.integral<1, false, true, 0 : 0, logic>, sym_name = "s56.observe"} {
          }
        }
        obelisk.sv.symbol.procedural_block attributes {hierarchical_name = "sim_expression_and_arguments", node_id = 256 : i64, procedure_kind = 3 : i32, sym_name = "s57", time_precision_fs = 1000000 : i64, time_unit_fs = 10000000 : i64} {
          obelisk.sv.statement.block attributes {node_id = 257 : i64} {
            obelisk.sv.statement.list attributes {node_id = 258 : i64} {
              obelisk.sv.statement.expression_statement attributes {node_id = 259 : i64} {
                obelisk.sv.expression.assignment attributes {assignment_kind = 0 : i32, node_id = 260 : i64, semantic_type = !obelisk.integral<1, false, true, 0 : 0, logic>} {
                  obelisk.sv.expression.named_value attributes {node_id = 261 : i64, referenced_path = "sim_expression_and_arguments.logical_result", referenced_symbol = @s5.$root::@s40.sim_expression_and_arguments::@s41.sim_expression_and_arguments::@s44.logical_result, semantic_type = !obelisk.integral<1, false, true, 0 : 0, logic>} {
                  }
                  obelisk.sv.expression.binary_op attributes {node_id = 262 : i64, operator_kind = 19 : i32, semantic_type = !obelisk.integral<1, false, true, 0 : 0, logic>} {
                    obelisk.sv.expression.named_value attributes {node_id = 263 : i64, referenced_path = "sim_expression_and_arguments.lhs", referenced_symbol = @s5.$root::@s40.sim_expression_and_arguments::@s41.sim_expression_and_arguments::@s42.lhs, semantic_type = !obelisk.ranged_packed_array<7 : 0 x !obelisk.integral<1, false, true, 0 : 0, logic>>} {
                    }
                    obelisk.sv.expression.named_value attributes {node_id = 264 : i64, referenced_path = "sim_expression_and_arguments.rhs", referenced_symbol = @s5.$root::@s40.sim_expression_and_arguments::@s41.sim_expression_and_arguments::@s43.rhs, semantic_type = !obelisk.ranged_packed_array<7 : 0 x !obelisk.integral<1, false, true, 0 : 0, logic>>} {
                    }
                  }
                }
              }
              obelisk.sv.statement.expression_statement attributes {node_id = 265 : i64} {
                obelisk.sv.expression.assignment attributes {assignment_kind = 0 : i32, node_id = 266 : i64, semantic_type = !obelisk.integral<1, false, true, 0 : 0, logic>} {
                  obelisk.sv.expression.named_value attributes {node_id = 267 : i64, referenced_path = "sim_expression_and_arguments.logical_not_result", referenced_symbol = @s5.$root::@s40.sim_expression_and_arguments::@s41.sim_expression_and_arguments::@s45.logical_not_result, semantic_type = !obelisk.integral<1, false, true, 0 : 0, logic>} {
                  }
                  obelisk.sv.expression.unary_op attributes {node_id = 268 : i64, operator_kind = 9 : i32, semantic_type = !obelisk.integral<1, false, true, 0 : 0, logic>} {
                    obelisk.sv.expression.named_value attributes {node_id = 269 : i64, referenced_path = "sim_expression_and_arguments.lhs", referenced_symbol = @s5.$root::@s40.sim_expression_and_arguments::@s41.sim_expression_and_arguments::@s42.lhs, semantic_type = !obelisk.ranged_packed_array<7 : 0 x !obelisk.integral<1, false, true, 0 : 0, logic>>} {
                    }
                  }
                }
              }
              obelisk.sv.statement.expression_statement attributes {node_id = 270 : i64} {
                obelisk.sv.expression.assignment attributes {assignment_kind = 0 : i32, node_id = 271 : i64, semantic_type = !obelisk.integral<1, false, false, 0 : 0, bit>} {
                  obelisk.sv.expression.named_value attributes {node_id = 272 : i64, referenced_path = "sim_expression_and_arguments.two_state_not", referenced_symbol = @s5.$root::@s40.sim_expression_and_arguments::@s41.sim_expression_and_arguments::@s47.two_state_not, semantic_type = !obelisk.integral<1, false, false, 0 : 0, bit>} {
                  }
                  obelisk.sv.expression.unary_op attributes {node_id = 273 : i64, operator_kind = 9 : i32, semantic_type = !obelisk.integral<1, false, false, 0 : 0, bit>} {
                    obelisk.sv.expression.named_value attributes {node_id = 274 : i64, referenced_path = "sim_expression_and_arguments.two_state", referenced_symbol = @s5.$root::@s40.sim_expression_and_arguments::@s41.sim_expression_and_arguments::@s46.two_state, semantic_type = !obelisk.ranged_packed_array<7 : 0 x !obelisk.integral<1, false, false, 0 : 0, bit>>} {
                    }
                  }
                }
              }
              obelisk.sv.statement.expression_statement attributes {node_id = 275 : i64} {
                obelisk.sv.expression.assignment attributes {assignment_kind = 0 : i32, node_id = 276 : i64, semantic_type = !obelisk.ranged_packed_array<7 : 0 x !obelisk.integral<1, false, true, 0 : 0, logic>>} {
                  obelisk.sv.expression.named_value attributes {node_id = 277 : i64, referenced_path = "sim_expression_and_arguments.rhs", referenced_symbol = @s5.$root::@s40.sim_expression_and_arguments::@s41.sim_expression_and_arguments::@s43.rhs, semantic_type = !obelisk.ranged_packed_array<7 : 0 x !obelisk.integral<1, false, true, 0 : 0, logic>>} {
                  }
                  obelisk.sv.expression.binary_op attributes {node_id = 278 : i64, operator_kind = 23 : i32, semantic_type = !obelisk.ranged_packed_array<7 : 0 x !obelisk.integral<1, false, true, 0 : 0, logic>>} {
                    obelisk.sv.expression.named_value attributes {node_id = 279 : i64, referenced_path = "sim_expression_and_arguments.lhs", referenced_symbol = @s5.$root::@s40.sim_expression_and_arguments::@s41.sim_expression_and_arguments::@s42.lhs, semantic_type = !obelisk.ranged_packed_array<7 : 0 x !obelisk.integral<1, false, true, 0 : 0, logic>>} {
                    }
                    obelisk.sv.expression.integer_literal attributes {constant_value = "1", node_id = 280 : i64, semantic_type = !obelisk.integral<32, true, false, 31 : 0, int>} {
                    }
                  }
                }
              }
            }
          }
        }
        obelisk.sv.symbol.procedural_block attributes {hierarchical_name = "sim_expression_and_arguments", node_id = 281 : i64, procedure_kind = 0 : i32, sym_name = "s58", time_precision_fs = 1000000 : i64, time_unit_fs = 10000000 : i64} {
          obelisk.sv.statement.block attributes {node_id = 282 : i64} {
            obelisk.sv.statement.list attributes {node_id = 283 : i64} {
              obelisk.sv.statement.timed attributes {node_id = 284 : i64} {
                obelisk.sv.timing.delay attributes {node_id = 285 : i64} {
                  obelisk.sv.expression.integer_literal attributes {constant_value = "5", node_id = 286 : i64, semantic_type = !obelisk.integral<32, true, false, 31 : 0, int>} {
                  }
                }
                obelisk.sv.statement.empty attributes {node_id = 287 : i64} {
                }
              }
              obelisk.sv.statement.expression_statement attributes {node_id = 288 : i64} {
                obelisk.sv.expression.assignment attributes {assignment_kind = 0 : i32, node_id = 289 : i64, semantic_type = !obelisk.integral<1, false, true, 0 : 0, logic>} {
                  obelisk.sv.expression.named_value attributes {node_id = 290 : i64, referenced_path = "sim_expression_and_arguments.function_result", referenced_symbol = @s5.$root::@s40.sim_expression_and_arguments::@s41.sim_expression_and_arguments::@s51.function_result, semantic_type = !obelisk.integral<1, false, true, 0 : 0, logic>} {
                  }
                  obelisk.sv.expression.call attributes {argument_count = 3 : i64, callee_name = "observe", constraint_restrictions = [], defaulted_arguments = array<i64: 0, 0, 0>, has_inline_constraints = false, has_iterator_expression = false, has_output_arguments = true, has_this_class = false, is_super_class = false, is_system_call = false, node_id = 291 : i64, referenced_path = "sim_expression_and_arguments.observe", referenced_symbol = @s5.$root::@s40.sim_expression_and_arguments::@s41.sim_expression_and_arguments::@s52.observe, semantic_type = !obelisk.integral<1, false, true, 0 : 0, logic>, subroutine_kind = 0 : i32} {
                    obelisk.sv.expression.named_value attributes {node_id = 292 : i64, referenced_path = "sim_expression_and_arguments.input_source", referenced_symbol = @s5.$root::@s40.sim_expression_and_arguments::@s41.sim_expression_and_arguments::@s50.input_source, semantic_type = !obelisk.integral<1, false, true, 0 : 0, logic>} {
                    }
                    obelisk.sv.expression.assignment attributes {assignment_kind = 0 : i32, node_id = 293 : i64, semantic_type = !obelisk.integral<1, false, true, 0 : 0, logic>} {
                      obelisk.sv.expression.named_value attributes {node_id = 294 : i64, referenced_path = "sim_expression_and_arguments.output_source", referenced_symbol = @s5.$root::@s40.sim_expression_and_arguments::@s41.sim_expression_and_arguments::@s48.output_source, semantic_type = !obelisk.integral<1, false, true, 0 : 0, logic>} {
                      }
                      obelisk.sv.expression.empty_argument attributes {node_id = 295 : i64, semantic_type = !obelisk.integral<1, false, true, 0 : 0, logic>} {
                      }
                    }
                    obelisk.sv.expression.assignment attributes {assignment_kind = 0 : i32, node_id = 296 : i64, semantic_type = !obelisk.integral<1, false, true, 0 : 0, logic>} {
                      obelisk.sv.expression.named_value attributes {node_id = 297 : i64, referenced_path = "sim_expression_and_arguments.inout_source", referenced_symbol = @s5.$root::@s40.sim_expression_and_arguments::@s41.sim_expression_and_arguments::@s49.inout_source, semantic_type = !obelisk.integral<1, false, true, 0 : 0, logic>} {
                      }
                      obelisk.sv.expression.empty_argument attributes {node_id = 298 : i64, semantic_type = !obelisk.integral<1, false, true, 0 : 0, logic>} {
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
    obelisk.sv.symbol.instance attributes {hierarchical_name = "sim_fast_timescale", is_uninstantiated = false, name = "sim_fast_timescale", node_id = 299 : i64, referenced_path = "sim_fast_timescale", referenced_symbol = @s3.sim_fast_timescale, sym_name = "s59.sim_fast_timescale"} {
      obelisk.sv.symbol.instance_body attributes {hierarchical_name = "sim_fast_timescale", name = "sim_fast_timescale", node_id = 300 : i64, sym_name = "s60.sim_fast_timescale"} {
        obelisk.sv.symbol.variable attributes {hierarchical_name = "sim_fast_timescale.value", lifetime = 1 : i32, name = "value", node_id = 301 : i64, semantic_type = !obelisk.ranged_packed_array<7 : 0 x !obelisk.integral<1, false, true, 0 : 0, logic>>, sym_name = "s61.value"} {
        }
        obelisk.sv.symbol.procedural_block attributes {hierarchical_name = "sim_fast_timescale", node_id = 302 : i64, procedure_kind = 0 : i32, sym_name = "s62", time_precision_fs = 1000 : i64, time_unit_fs = 1000000 : i64} {
          obelisk.sv.statement.block attributes {node_id = 303 : i64} {
            obelisk.sv.statement.list attributes {node_id = 304 : i64} {
              obelisk.sv.statement.expression_statement attributes {node_id = 305 : i64} {
                obelisk.sv.expression.assignment attributes {assignment_kind = 0 : i32, node_id = 306 : i64, semantic_type = !obelisk.ranged_packed_array<7 : 0 x !obelisk.integral<1, false, true, 0 : 0, logic>>} {
                  obelisk.sv.expression.named_value attributes {node_id = 307 : i64, referenced_path = "sim_fast_timescale.value", referenced_symbol = @s5.$root::@s59.sim_fast_timescale::@s60.sim_fast_timescale::@s61.value, semantic_type = !obelisk.ranged_packed_array<7 : 0 x !obelisk.integral<1, false, true, 0 : 0, logic>>} {
                  }
                  obelisk.sv.expression.unbased_unsized_integer_literal attributes {constant_value = "8'd0", node_id = 308 : i64, semantic_type = !obelisk.ranged_packed_array<7 : 0 x !obelisk.integral<1, false, true, 0 : 0, logic>>} {
                  }
                }
              }
              obelisk.sv.statement.timed attributes {node_id = 309 : i64} {
                obelisk.sv.timing.delay attributes {node_id = 310 : i64} {
                  obelisk.sv.expression.integer_literal attributes {constant_value = "5", node_id = 311 : i64, semantic_type = !obelisk.integral<32, true, false, 31 : 0, int>} {
                  }
                }
                obelisk.sv.statement.empty attributes {node_id = 312 : i64} {
                }
              }
            }
          }
        }
      }
    }
    obelisk.sv.symbol.instance attributes {hierarchical_name = "sim_sccp_pipeline", is_uninstantiated = false, name = "sim_sccp_pipeline", node_id = 313 : i64, referenced_path = "sim_sccp_pipeline", referenced_symbol = @s4.sim_sccp_pipeline, sym_name = "s63.sim_sccp_pipeline"} {
      obelisk.sv.symbol.instance_body attributes {hierarchical_name = "sim_sccp_pipeline", name = "sim_sccp_pipeline", node_id = 314 : i64, sym_name = "s64.sim_sccp_pipeline"} {
        obelisk.sv.symbol.variable attributes {hierarchical_name = "sim_sccp_pipeline.folded_sink", lifetime = 1 : i32, name = "folded_sink", node_id = 315 : i64, semantic_type = !obelisk.integral<1, false, false, 0 : 0, bit>, sym_name = "s65.folded_sink"} {
        }
        obelisk.sv.symbol.subroutine attributes {hierarchical_name = "sim_sccp_pipeline.fold_identity", name = "fold_identity", node_id = 316 : i64, return_variable_path = "sim_sccp_pipeline.fold_identity.fold_identity", return_variable_symbol = @s5.$root::@s63.sim_sccp_pipeline::@s64.sim_sccp_pipeline::@s66.fold_identity::@s68.fold_identity, semantic_type = !obelisk.subroutine<(!obelisk.integral<1, false, false, 0 : 0, bit>) -> !obelisk.integral<1, false, false, 0 : 0, bit>, false>, subroutine_kind = 0 : i32, sym_name = "s66.fold_identity", time_precision_fs = 1000000 : i64, time_unit_fs = 10000000 : i64} {
          obelisk.sv.statement.expression_statement attributes {node_id = 317 : i64} {
            obelisk.sv.expression.assignment attributes {assignment_kind = 0 : i32, node_id = 318 : i64, semantic_type = !obelisk.integral<1, false, false, 0 : 0, bit>} {
              obelisk.sv.expression.named_value attributes {node_id = 319 : i64, referenced_path = "sim_sccp_pipeline.fold_identity.fold_identity", referenced_symbol = @s5.$root::@s63.sim_sccp_pipeline::@s64.sim_sccp_pipeline::@s66.fold_identity::@s68.fold_identity, semantic_type = !obelisk.integral<1, false, false, 0 : 0, bit>} {
              }
              obelisk.sv.expression.named_value attributes {node_id = 320 : i64, referenced_path = "sim_sccp_pipeline.fold_identity.value", referenced_symbol = @s5.$root::@s63.sim_sccp_pipeline::@s64.sim_sccp_pipeline::@s66.fold_identity::@s67.value, semantic_type = !obelisk.integral<1, false, false, 0 : 0, bit>} {
              }
            }
          }
          obelisk.sv.symbol.formal_argument attributes {direction = 0 : i32, hierarchical_name = "sim_sccp_pipeline.fold_identity.value", name = "value", node_id = 321 : i64, semantic_type = !obelisk.integral<1, false, false, 0 : 0, bit>, sym_name = "s67.value"} {
          }
          obelisk.sv.symbol.variable attributes {hierarchical_name = "sim_sccp_pipeline.fold_identity.fold_identity", is_compiler_generated, name = "fold_identity", node_id = 322 : i64, semantic_type = !obelisk.integral<1, false, false, 0 : 0, bit>, sym_name = "s68.fold_identity"} {
          }
        }
        obelisk.sv.symbol.subroutine attributes {hierarchical_name = "sim_sccp_pipeline.unused_function", name = "unused_function", node_id = 323 : i64, return_variable_path = "sim_sccp_pipeline.unused_function.unused_function", return_variable_symbol = @s5.$root::@s63.sim_sccp_pipeline::@s64.sim_sccp_pipeline::@s69.unused_function::@s71.unused_function, semantic_type = !obelisk.subroutine<(!obelisk.integral<1, false, false, 0 : 0, bit>) -> !obelisk.integral<32, false, false, 31 : 0, int>, false>, subroutine_kind = 0 : i32, sym_name = "s69.unused_function", time_precision_fs = 1000000 : i64, time_unit_fs = 10000000 : i64} {
          obelisk.sv.statement.expression_statement attributes {node_id = 324 : i64} {
            obelisk.sv.expression.assignment attributes {assignment_kind = 0 : i32, node_id = 325 : i64, semantic_type = !obelisk.integral<32, false, false, 31 : 0, int>} {
              obelisk.sv.expression.named_value attributes {node_id = 326 : i64, referenced_path = "sim_sccp_pipeline.unused_function.unused_function", referenced_symbol = @s5.$root::@s63.sim_sccp_pipeline::@s64.sim_sccp_pipeline::@s69.unused_function::@s71.unused_function, semantic_type = !obelisk.integral<32, false, false, 31 : 0, int>} {
              }
              obelisk.sv.expression.conversion attributes {node_id = 327 : i64, semantic_type = !obelisk.integral<32, false, false, 31 : 0, int>} {
                obelisk.sv.expression.integer_literal attributes {constant_value = "123456789", node_id = 328 : i64, semantic_type = !obelisk.integral<32, true, false, 31 : 0, int>} {
                }
              }
            }
          }
          obelisk.sv.symbol.formal_argument attributes {direction = 0 : i32, hierarchical_name = "sim_sccp_pipeline.unused_function.value", name = "value", node_id = 329 : i64, semantic_type = !obelisk.integral<1, false, false, 0 : 0, bit>, sym_name = "s70.value"} {
          }
          obelisk.sv.symbol.variable attributes {hierarchical_name = "sim_sccp_pipeline.unused_function.unused_function", is_compiler_generated, name = "unused_function", node_id = 330 : i64, semantic_type = !obelisk.integral<32, false, false, 31 : 0, int>, sym_name = "s71.unused_function"} {
          }
        }
        obelisk.sv.symbol.procedural_block attributes {hierarchical_name = "sim_sccp_pipeline", node_id = 331 : i64, procedure_kind = 0 : i32, sym_name = "s72", time_precision_fs = 1000000 : i64, time_unit_fs = 10000000 : i64} {
          obelisk.sv.statement.expression_statement attributes {node_id = 332 : i64} {
            obelisk.sv.expression.assignment attributes {assignment_kind = 0 : i32, node_id = 333 : i64, semantic_type = !obelisk.integral<1, false, false, 0 : 0, bit>} {
              obelisk.sv.expression.named_value attributes {node_id = 334 : i64, referenced_path = "sim_sccp_pipeline.folded_sink", referenced_symbol = @s5.$root::@s63.sim_sccp_pipeline::@s64.sim_sccp_pipeline::@s65.folded_sink, semantic_type = !obelisk.integral<1, false, false, 0 : 0, bit>} {
              }
              obelisk.sv.expression.call attributes {argument_count = 1 : i64, callee_name = "fold_identity", constraint_restrictions = [], defaulted_arguments = array<i64: 0>, has_inline_constraints = false, has_iterator_expression = false, has_output_arguments = false, has_this_class = false, is_super_class = false, is_system_call = false, node_id = 335 : i64, referenced_path = "sim_sccp_pipeline.fold_identity", referenced_symbol = @s5.$root::@s63.sim_sccp_pipeline::@s64.sim_sccp_pipeline::@s66.fold_identity, semantic_type = !obelisk.integral<1, false, false, 0 : 0, bit>, subroutine_kind = 0 : i32} {
                obelisk.sv.expression.conversion attributes {node_id = 336 : i64, semantic_type = !obelisk.integral<1, false, false, 0 : 0, bit>} {
                  obelisk.sv.expression.integer_literal attributes {constant_value = "1", node_id = 337 : i64, semantic_type = !obelisk.integral<32, true, false, 31 : 0, int>} {
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

// OPTIONS: #obelisk_sim.graph<version = 1, vpi = read, workers = 2

// SIM: obelisk_sim.design @design attributes {{.*}}time_precision_fs = 1000 : i64
// SIM-DAG: obelisk_sim.scope.decl 0
// SIM-DAG: obelisk_sim.storage.decl
// SIM-DAG: obelisk_sim.net.decl
// SIM-DAG: obelisk_sim.driver.decl
// SIM-DAG: obelisk_sim.func @__obelisk_root
// SIM-DAG: obelisk_sim.spawn
// SIM-DAG: obelisk_sim.func private @unit_
// SIM-DAG: obelisk_sim.call @unit_
// SIM-DAG: obelisk_sim.ref.extract
// SIM-DAG: obelisk_sim.logic.replicate
// SIM-DAG: obelisk_sim.logic.compare slt
// SIM-DAG: obelisk_sim.time.constant 5000
// SIM-DAG: obelisk_sim.time.constant 50000
// SIM-DAG: obelisk_sim.logic.logical and
// SIM-DAG: obelisk_sim.logic.unary logical_not
// SIM-DAG: arith.cmpi eq
// SIM-DAG: obelisk_sim.logic.shift left
// SIM-DAG: -> (!obelisk_sim.logic<1>, !obelisk_sim.logic<1>, !obelisk_sim.logic<1>) attributes {{.*}}entry_kind = 8 : i32
// SIM-DAG: {{%.*}}:3 = obelisk_sim.call
// SIM-DAG: obelisk_sim.ref.store {{%.*}}#1
// SIM-DAG: obelisk_sim.ref.store {{%.*}}#2
// SIM-DAG: obelisk_sim.ref.subelement
// SIM-DAG: obelisk_sim.packed.flatten
// SIM-DAG: cf.cond_br
// SIM-DAG: obelisk_sim.nba.enqueue
// SIM-DAG: obelisk_sim.suspend.change
// SIM-DAG: obelisk_sim.suspend.edge posedge
// SIM-DAG: obelisk_sim.suspend.any
// SIM-DAG: obelisk_sim.suspend.delay {{.*}} to ^

// FINAL-NOT: obelisk.sv.
// FINAL-NOT: obelisk_sim.bindings
// FINAL-NOT: obelisk_sim.delay_scale
// FINAL-NOT: time_unit_fs
// FINAL-NOT: obelisk_sim.ref.alloc
// FINAL-NOT: obelisk_sim.func @unit_

// SCCP: obelisk_sim.func private @[[IDENTITY:unit_[0-9]+]]({{.*}}%arg1: i1
// SCCP-SAME: -> i1
// SCCP: %[[IDENTITY_RESULT:.*]] = arith.constant true
// SCCP: obelisk_sim.return %[[IDENTITY_RESULT]] : i1
// SCCP: obelisk_sim.func private @[[CALLER:unit_[0-9]+]]({{.*}}!obelisk_sim.ref<i1>
// SCCP: %[[FOLDED:.*]] = arith.constant true
// SCCP: obelisk_sim.call @[[IDENTITY]]
// SCCP: obelisk_sim.ref.store %[[FOLDED]]

// Input arguments are value-only. Only output and inout results are copied
// back before the function return value is stored.
// COPYBACK: obelisk_sim.time.constant 50000
// COPYBACK: obelisk_sim.suspend.delay
// COPYBACK: %[[INPUT_VALUE:.*]] = obelisk_sim.ref.load %[[INPUT_REF:.*]]
// COPYBACK: %[[CALL:.*]]:3 = obelisk_sim.call {{.*}}%[[INPUT_VALUE]]
// COPYBACK-NOT: obelisk_sim.ref.store {{.*}} to %[[INPUT_REF]]
// COPYBACK: obelisk_sim.ref.store
// COPYBACK-NOT: obelisk_sim.ref.store {{.*}} to %[[INPUT_REF]]
// COPYBACK-NEXT: obelisk_sim.ref.store %[[CALL]]#2
// COPYBACK-NOT: obelisk_sim.ref.store {{.*}} to %[[INPUT_REF]]
// COPYBACK-NEXT: obelisk_sim.ref.store %[[CALL]]#0
// COPYBACK-NOT: obelisk_sim.ref.store {{.*}} to %[[INPUT_REF]]
// COPYBACK-NEXT: obelisk_sim.return
