// RUN: not obelisk-opt %s '--lower-obelisk-to-sim=opt-level=0' -o /dev/null 2>&1 | FileCheck %s

// A combined bounded consequent may branch temporally, but a nested
// first_match needs per-obligation priority state and remains diagnosed.
// CHECK: error: combined branching implication/followed-by currently requires bounded consequent alternatives without retained nested first_match boundaries, vacuous alternatives, or match items and at most 256 antecedent/consequent alternative pairs

module {
  obelisk.sv.symbol.definition attributes {definition_kind = 0 : i32, hierarchical_name = "combined_temporal_invalid", name = "combined_temporal_invalid", node_id = 0 : i64, sym_name = "s0.combined_temporal_invalid"} {
  }
  obelisk.sv.symbol.root attributes {hierarchical_name = "\\$root ", name = "$root", node_id = 1 : i64, sym_name = "s1.$root"} {
    obelisk.sv.symbol.compilation_unit attributes {hierarchical_name = "$unit", node_id = 2 : i64, sym_name = "s2"} {
    }
    obelisk.sv.symbol.instance attributes {hierarchical_name = "combined_temporal_invalid", is_uninstantiated = false, name = "combined_temporal_invalid", node_id = 3 : i64, referenced_path = "combined_temporal_invalid", referenced_symbol = @s0.combined_temporal_invalid, sym_name = "s3.combined_temporal_invalid"} {
      obelisk.sv.symbol.instance_body attributes {hierarchical_name = "combined_temporal_invalid", name = "combined_temporal_invalid", node_id = 4 : i64, sym_name = "s4.combined_temporal_invalid", time_precision_fs = 1000000 : i64, time_unit_fs = 1000000 : i64} {
        obelisk.sv.symbol.port attributes {direction = 0 : i32, hierarchical_name = "combined_temporal_invalid.clk", name = "clk", node_id = 5 : i64, semantic_type = !obelisk.integral<1, false, true, 0 : 0, logic>, sym_name = "s5.clk"} {
        }
        obelisk.sv.symbol.variable attributes {hierarchical_name = "combined_temporal_invalid.clk", lifetime = 1 : i32, name = "clk", node_id = 6 : i64, semantic_type = !obelisk.integral<1, false, true, 0 : 0, logic>, sym_name = "s6.clk"} {
        }
        obelisk.sv.symbol.port attributes {direction = 0 : i32, hierarchical_name = "combined_temporal_invalid.a", name = "a", node_id = 7 : i64, semantic_type = !obelisk.integral<1, false, true, 0 : 0, logic>, sym_name = "s7.a"} {
        }
        obelisk.sv.symbol.variable attributes {hierarchical_name = "combined_temporal_invalid.a", lifetime = 1 : i32, name = "a", node_id = 8 : i64, semantic_type = !obelisk.integral<1, false, true, 0 : 0, logic>, sym_name = "s8.a"} {
        }
        obelisk.sv.symbol.port attributes {direction = 0 : i32, hierarchical_name = "combined_temporal_invalid.b", name = "b", node_id = 9 : i64, semantic_type = !obelisk.integral<1, false, true, 0 : 0, logic>, sym_name = "s9.b"} {
        }
        obelisk.sv.symbol.variable attributes {hierarchical_name = "combined_temporal_invalid.b", lifetime = 1 : i32, name = "b", node_id = 10 : i64, semantic_type = !obelisk.integral<1, false, true, 0 : 0, logic>, sym_name = "s10.b"} {
        }
        obelisk.sv.symbol.port attributes {direction = 0 : i32, hierarchical_name = "combined_temporal_invalid.c", name = "c", node_id = 11 : i64, semantic_type = !obelisk.integral<1, false, true, 0 : 0, logic>, sym_name = "s11.c"} {
        }
        obelisk.sv.symbol.variable attributes {hierarchical_name = "combined_temporal_invalid.c", lifetime = 1 : i32, name = "c", node_id = 12 : i64, semantic_type = !obelisk.integral<1, false, true, 0 : 0, logic>, sym_name = "s12.c"} {
        }
        obelisk.sv.symbol.port attributes {direction = 0 : i32, hierarchical_name = "combined_temporal_invalid.d", name = "d", node_id = 13 : i64, semantic_type = !obelisk.integral<1, false, true, 0 : 0, logic>, sym_name = "s13.d"} {
        }
        obelisk.sv.symbol.variable attributes {hierarchical_name = "combined_temporal_invalid.d", lifetime = 1 : i32, name = "d", node_id = 14 : i64, semantic_type = !obelisk.integral<1, false, true, 0 : 0, logic>, sym_name = "s14.d"} {
        }
        obelisk.sv.symbol.procedural_block attributes {hierarchical_name = "combined_temporal_invalid", node_id = 15 : i64, procedure_kind = 2 : i32, sym_name = "s15", time_precision_fs = 1000000 : i64, time_unit_fs = 1000000 : i64} {
          obelisk.sv.statement.concurrent_assertion attributes {assertion_kind = 0 : i32, has_default_disable = false, has_fail_action = false, has_pass_action = true, node_id = 16 : i64} {
            obelisk.sv.assertion.clocking attributes {node_id = 17 : i64} {
              obelisk.sv.timing.signal_event attributes {edge_kind = 1 : i32, has_iff = false, node_id = 18 : i64} {
                obelisk.sv.expression.named_value attributes {is_signed = false, node_id = 19 : i64, referenced_path = "combined_temporal_invalid.clk", referenced_symbol = @s1.$root::@s3.combined_temporal_invalid::@s4.combined_temporal_invalid::@s6.clk, semantic_type = !obelisk.integral<1, false, true, 0 : 0, logic>} {
                }
              }
              obelisk.sv.assertion.binary attributes {node_id = 20 : i64, operator_kind = 11 : i32} {
                obelisk.sv.assertion.binary attributes {node_id = 21 : i64, operator_kind = 1 : i32} {
                  obelisk.sv.assertion.simple attributes {has_repetition = false, is_null = false, node_id = 22 : i64, repetition_is_unbounded = false} {
                    obelisk.sv.expression.named_value attributes {is_signed = false, node_id = 23 : i64, referenced_path = "combined_temporal_invalid.a", referenced_symbol = @s1.$root::@s3.combined_temporal_invalid::@s4.combined_temporal_invalid::@s8.a, semantic_type = !obelisk.integral<1, false, true, 0 : 0, logic>} {
                    }
                  }
                  obelisk.sv.assertion.simple attributes {has_repetition = false, is_null = false, node_id = 24 : i64, repetition_is_unbounded = false} {
                    obelisk.sv.expression.named_value attributes {is_signed = false, node_id = 25 : i64, referenced_path = "combined_temporal_invalid.b", referenced_symbol = @s1.$root::@s3.combined_temporal_invalid::@s4.combined_temporal_invalid::@s10.b, semantic_type = !obelisk.integral<1, false, true, 0 : 0, logic>} {
                    }
                  }
                }
                obelisk.sv.assertion.binary attributes {node_id = 26 : i64, operator_kind = 1 : i32} {
                  obelisk.sv.assertion.first_match attributes {match_item_count = 0 : i64, node_id = 27 : i64} {
                    obelisk.sv.assertion.sequence_concat attributes {delays = [{is_unbounded = false, max = 0 : i64, min = 0 : i64}, {is_unbounded = false, max = 2 : i64, min = 1 : i64}], node_id = 28 : i64} {
                      obelisk.sv.assertion.simple attributes {has_repetition = false, is_null = false, node_id = 29 : i64, repetition_is_unbounded = false} {
                        obelisk.sv.expression.named_value attributes {is_signed = false, node_id = 30 : i64, referenced_path = "combined_temporal_invalid.c", referenced_symbol = @s1.$root::@s3.combined_temporal_invalid::@s4.combined_temporal_invalid::@s12.c, semantic_type = !obelisk.integral<1, false, true, 0 : 0, logic>} {
                        }
                      }
                      obelisk.sv.assertion.simple attributes {has_repetition = false, is_null = false, node_id = 31 : i64, repetition_is_unbounded = false} {
                        obelisk.sv.expression.named_value attributes {is_signed = false, node_id = 32 : i64, referenced_path = "combined_temporal_invalid.d", referenced_symbol = @s1.$root::@s3.combined_temporal_invalid::@s4.combined_temporal_invalid::@s14.d, semantic_type = !obelisk.integral<1, false, true, 0 : 0, logic>} {
                        }
                      }
                    }
                  }
                  obelisk.sv.assertion.sequence_concat attributes {delays = [{is_unbounded = false, max = 0 : i64, min = 0 : i64}, {is_unbounded = false, max = 3 : i64, min = 3 : i64}], node_id = 33 : i64} {
                    obelisk.sv.assertion.simple attributes {has_repetition = false, is_null = false, node_id = 34 : i64, repetition_is_unbounded = false} {
                      obelisk.sv.expression.named_value attributes {is_signed = false, node_id = 35 : i64, referenced_path = "combined_temporal_invalid.c", referenced_symbol = @s1.$root::@s3.combined_temporal_invalid::@s4.combined_temporal_invalid::@s12.c, semantic_type = !obelisk.integral<1, false, true, 0 : 0, logic>} {
                      }
                    }
                    obelisk.sv.assertion.simple attributes {has_repetition = false, is_null = false, node_id = 36 : i64, repetition_is_unbounded = false} {
                      obelisk.sv.expression.named_value attributes {is_signed = false, node_id = 37 : i64, referenced_path = "combined_temporal_invalid.d", referenced_symbol = @s1.$root::@s3.combined_temporal_invalid::@s4.combined_temporal_invalid::@s14.d, semantic_type = !obelisk.integral<1, false, true, 0 : 0, logic>} {
                      }
                    }
                  }
                }
              }
            }
            obelisk.sv.statement.empty attributes {node_id = 38 : i64} {
            }
          }
        }
      }
    }
  }
}
