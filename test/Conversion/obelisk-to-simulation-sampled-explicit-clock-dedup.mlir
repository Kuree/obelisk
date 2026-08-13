// RUN: obelisk-opt %s '--lower-obelisk-to-sim=opt-level=0' | FileCheck %s

// Two properties sampling $past under the same explicit clock share a single
// alternate-clock sampler rather than each spawning their own.
//
//   module native_sampled_explicit_clock_dedup;   // --std=1800-2023
//     logic main_clk = 0, alternate_clk = 0, data = 0, gate = 1;
//     first: assert property (@(posedge main_clk)
//         $past(data, 1, gate, @(posedge alternate_clk)) === 1'b0);
//     second: assert property (@(posedge main_clk)
//         $past(data, 1, gate, @(posedge alternate_clk)) === 1'b0);
//   endmodule

module {
  obelisk.sv.symbol.definition attributes {definition_kind = 0 : i32, hierarchical_name = "native_sampled_explicit_clock_dedup", name = "native_sampled_explicit_clock_dedup", node_id = 0 : i64, sym_name = "s0.native_sampled_explicit_clock_dedup"} {
  }
  obelisk.sv.symbol.root attributes {hierarchical_name = "\\$root ", name = "$root", node_id = 1 : i64, sym_name = "s1.$root"} {
    obelisk.sv.symbol.compilation_unit attributes {hierarchical_name = "$unit", node_id = 2 : i64, sym_name = "s2"} {
    }
    obelisk.sv.symbol.instance attributes {hierarchical_name = "native_sampled_explicit_clock_dedup", is_uninstantiated = false, name = "native_sampled_explicit_clock_dedup", node_id = 3 : i64, referenced_path = "native_sampled_explicit_clock_dedup", referenced_symbol = @s0.native_sampled_explicit_clock_dedup, sym_name = "s3.native_sampled_explicit_clock_dedup"} {
      obelisk.sv.symbol.instance_body attributes {hierarchical_name = "native_sampled_explicit_clock_dedup", name = "native_sampled_explicit_clock_dedup", node_id = 4 : i64, sym_name = "s4.native_sampled_explicit_clock_dedup", time_precision_fs = 1000000 : i64, time_unit_fs = 1000000 : i64} {
        obelisk.sv.symbol.variable attributes {hierarchical_name = "native_sampled_explicit_clock_dedup.main_clk", lifetime = 1 : i32, name = "main_clk", node_id = 5 : i64, semantic_type = !obelisk.integral<1, false, true, 0 : 0, logic>, sym_name = "s5.main_clk"} {
          obelisk.sv.expression.conversion attributes {folded_constant = "1'b0", is_signed = false, node_id = 6 : i64, semantic_type = !obelisk.integral<1, false, true, 0 : 0, logic>} {
            obelisk.sv.expression.conversion attributes {folded_constant = "0", is_signed = true, node_id = 7 : i64, semantic_type = !obelisk.ranged_packed_array<31 : 0 x !obelisk.integral<1, true, true, 0 : 0, logic>>} {
              obelisk.sv.expression.integer_literal attributes {constant_value = "0", is_declared_unsized = true, is_signed = true, node_id = 8 : i64, semantic_type = !obelisk.integral<32, true, false, 31 : 0, int>} {
              }
            }
          }
        }
        obelisk.sv.symbol.variable attributes {hierarchical_name = "native_sampled_explicit_clock_dedup.alternate_clk", lifetime = 1 : i32, name = "alternate_clk", node_id = 9 : i64, semantic_type = !obelisk.integral<1, false, true, 0 : 0, logic>, sym_name = "s6.alternate_clk"} {
          obelisk.sv.expression.conversion attributes {folded_constant = "1'b0", is_signed = false, node_id = 10 : i64, semantic_type = !obelisk.integral<1, false, true, 0 : 0, logic>} {
            obelisk.sv.expression.conversion attributes {folded_constant = "0", is_signed = true, node_id = 11 : i64, semantic_type = !obelisk.ranged_packed_array<31 : 0 x !obelisk.integral<1, true, true, 0 : 0, logic>>} {
              obelisk.sv.expression.integer_literal attributes {constant_value = "0", is_declared_unsized = true, is_signed = true, node_id = 12 : i64, semantic_type = !obelisk.integral<32, true, false, 31 : 0, int>} {
              }
            }
          }
        }
        obelisk.sv.symbol.variable attributes {hierarchical_name = "native_sampled_explicit_clock_dedup.data", lifetime = 1 : i32, name = "data", node_id = 13 : i64, semantic_type = !obelisk.integral<1, false, true, 0 : 0, logic>, sym_name = "s7.data"} {
          obelisk.sv.expression.conversion attributes {folded_constant = "1'b0", is_signed = false, node_id = 14 : i64, semantic_type = !obelisk.integral<1, false, true, 0 : 0, logic>} {
            obelisk.sv.expression.conversion attributes {folded_constant = "0", is_signed = true, node_id = 15 : i64, semantic_type = !obelisk.ranged_packed_array<31 : 0 x !obelisk.integral<1, true, true, 0 : 0, logic>>} {
              obelisk.sv.expression.integer_literal attributes {constant_value = "0", is_declared_unsized = true, is_signed = true, node_id = 16 : i64, semantic_type = !obelisk.integral<32, true, false, 31 : 0, int>} {
              }
            }
          }
        }
        obelisk.sv.symbol.variable attributes {hierarchical_name = "native_sampled_explicit_clock_dedup.gate", lifetime = 1 : i32, name = "gate", node_id = 17 : i64, semantic_type = !obelisk.integral<1, false, true, 0 : 0, logic>, sym_name = "s8.gate"} {
          obelisk.sv.expression.conversion attributes {folded_constant = "1'b1", is_signed = false, node_id = 18 : i64, semantic_type = !obelisk.integral<1, false, true, 0 : 0, logic>} {
            obelisk.sv.expression.conversion attributes {folded_constant = "1", is_signed = true, node_id = 19 : i64, semantic_type = !obelisk.ranged_packed_array<31 : 0 x !obelisk.integral<1, true, true, 0 : 0, logic>>} {
              obelisk.sv.expression.integer_literal attributes {constant_value = "1", is_declared_unsized = true, is_signed = true, node_id = 20 : i64, semantic_type = !obelisk.integral<32, true, false, 31 : 0, int>} {
              }
            }
          }
        }
        obelisk.sv.symbol.statement_block attributes {block_kind = 0 : i32, hierarchical_name = "native_sampled_explicit_clock_dedup.first", name = "first", node_id = 21 : i64, sym_name = "s9.first"} {
        }
        obelisk.sv.symbol.procedural_block attributes {hierarchical_name = "native_sampled_explicit_clock_dedup", node_id = 22 : i64, procedure_kind = 2 : i32, sym_name = "s10", time_precision_fs = 1000000 : i64, time_unit_fs = 1000000 : i64} {
          obelisk.sv.statement.block attributes {block_path = "native_sampled_explicit_clock_dedup.first", block_symbol = @s1.$root::@s3.native_sampled_explicit_clock_dedup::@s4.native_sampled_explicit_clock_dedup::@s9.first, node_id = 23 : i64} {
            obelisk.sv.statement.concurrent_assertion attributes {assertion_kind = 0 : i32, has_default_disable = false, has_fail_action = false, has_pass_action = true, node_id = 24 : i64} {
              obelisk.sv.assertion.clocking attributes {node_id = 25 : i64} {
                obelisk.sv.timing.signal_event attributes {edge_kind = 1 : i32, has_iff = false, node_id = 26 : i64} {
                  obelisk.sv.expression.named_value attributes {is_signed = false, node_id = 27 : i64, referenced_path = "native_sampled_explicit_clock_dedup.main_clk", referenced_symbol = @s1.$root::@s3.native_sampled_explicit_clock_dedup::@s4.native_sampled_explicit_clock_dedup::@s5.main_clk, semantic_type = !obelisk.integral<1, false, true, 0 : 0, logic>} {
                  }
                }
                obelisk.sv.assertion.simple attributes {has_repetition = false, is_null = false, node_id = 28 : i64, repetition_is_unbounded = false} {
                  obelisk.sv.expression.binary_op attributes {is_signed = false, node_id = 29 : i64, operator_kind = 11 : i32, semantic_type = !obelisk.integral<1, false, false, 0 : 0, bit>} {
                    obelisk.sv.expression.call attributes {argument_count = 4 : i64, callee_name = "$past", constraint_restrictions = [], defaulted_arguments = array<i64: 0, 0, 0, 0>, has_inline_constraints = false, has_iterator_expression = false, has_output_arguments = false, has_this_class = false, is_signed = false, is_super_class = false, is_system_call = true, node_id = 30 : i64, semantic_type = !obelisk.integral<1, false, true, 0 : 0, logic>, subroutine_kind = 0 : i32, system_library_cell = "work.native_sampled_explicit_clock_dedup", system_scope_path = "native_sampled_explicit_clock_dedup.first", system_scope_symbol = @s1.$root::@s3.native_sampled_explicit_clock_dedup::@s4.native_sampled_explicit_clock_dedup::@s9.first} {
                      obelisk.sv.expression.named_value attributes {is_signed = false, node_id = 31 : i64, referenced_path = "native_sampled_explicit_clock_dedup.data", referenced_symbol = @s1.$root::@s3.native_sampled_explicit_clock_dedup::@s4.native_sampled_explicit_clock_dedup::@s7.data, semantic_type = !obelisk.integral<1, false, true, 0 : 0, logic>} {
                      }
                      obelisk.sv.expression.integer_literal attributes {constant_value = "1", is_declared_unsized = true, is_signed = true, node_id = 32 : i64, semantic_type = !obelisk.integral<32, true, false, 31 : 0, int>} {
                      }
                      obelisk.sv.expression.named_value attributes {is_signed = false, node_id = 33 : i64, referenced_path = "native_sampled_explicit_clock_dedup.gate", referenced_symbol = @s1.$root::@s3.native_sampled_explicit_clock_dedup::@s4.native_sampled_explicit_clock_dedup::@s8.gate, semantic_type = !obelisk.integral<1, false, true, 0 : 0, logic>} {
                      }
                      obelisk.sv.expression.clocking_event attributes {is_signed = false, node_id = 34 : i64, semantic_type = !obelisk.void} {
                        obelisk.sv.timing.signal_event attributes {edge_kind = 1 : i32, has_iff = false, node_id = 35 : i64} {
                          obelisk.sv.expression.named_value attributes {is_signed = false, node_id = 36 : i64, referenced_path = "native_sampled_explicit_clock_dedup.alternate_clk", referenced_symbol = @s1.$root::@s3.native_sampled_explicit_clock_dedup::@s4.native_sampled_explicit_clock_dedup::@s6.alternate_clk, semantic_type = !obelisk.integral<1, false, true, 0 : 0, logic>} {
                          }
                        }
                      }
                    }
                    obelisk.sv.expression.conversion attributes {folded_constant = "1'b0", is_signed = false, node_id = 37 : i64, semantic_type = !obelisk.integral<1, false, true, 0 : 0, logic>} {
                      obelisk.sv.expression.integer_literal attributes {constant_value = "1'b0", is_signed = false, node_id = 38 : i64, semantic_type = !obelisk.ranged_packed_array<0 : 0 x !obelisk.integral<1, false, false, 0 : 0, bit>>} {
                      }
                    }
                  }
                }
              }
              obelisk.sv.statement.empty attributes {node_id = 39 : i64} {
              }
            }
          }
        }
        obelisk.sv.symbol.statement_block attributes {block_kind = 0 : i32, hierarchical_name = "native_sampled_explicit_clock_dedup.second", name = "second", node_id = 40 : i64, sym_name = "s11.second"} {
        }
        obelisk.sv.symbol.procedural_block attributes {hierarchical_name = "native_sampled_explicit_clock_dedup", node_id = 41 : i64, procedure_kind = 2 : i32, sym_name = "s12", time_precision_fs = 1000000 : i64, time_unit_fs = 1000000 : i64} {
          obelisk.sv.statement.block attributes {block_path = "native_sampled_explicit_clock_dedup.second", block_symbol = @s1.$root::@s3.native_sampled_explicit_clock_dedup::@s4.native_sampled_explicit_clock_dedup::@s11.second, node_id = 42 : i64} {
            obelisk.sv.statement.concurrent_assertion attributes {assertion_kind = 0 : i32, has_default_disable = false, has_fail_action = false, has_pass_action = true, node_id = 43 : i64} {
              obelisk.sv.assertion.clocking attributes {node_id = 44 : i64} {
                obelisk.sv.timing.signal_event attributes {edge_kind = 1 : i32, has_iff = false, node_id = 45 : i64} {
                  obelisk.sv.expression.named_value attributes {is_signed = false, node_id = 46 : i64, referenced_path = "native_sampled_explicit_clock_dedup.main_clk", referenced_symbol = @s1.$root::@s3.native_sampled_explicit_clock_dedup::@s4.native_sampled_explicit_clock_dedup::@s5.main_clk, semantic_type = !obelisk.integral<1, false, true, 0 : 0, logic>} {
                  }
                }
                obelisk.sv.assertion.simple attributes {has_repetition = false, is_null = false, node_id = 47 : i64, repetition_is_unbounded = false} {
                  obelisk.sv.expression.binary_op attributes {is_signed = false, node_id = 48 : i64, operator_kind = 11 : i32, semantic_type = !obelisk.integral<1, false, false, 0 : 0, bit>} {
                    obelisk.sv.expression.call attributes {argument_count = 4 : i64, callee_name = "$past", constraint_restrictions = [], defaulted_arguments = array<i64: 0, 0, 0, 0>, has_inline_constraints = false, has_iterator_expression = false, has_output_arguments = false, has_this_class = false, is_signed = false, is_super_class = false, is_system_call = true, node_id = 49 : i64, semantic_type = !obelisk.integral<1, false, true, 0 : 0, logic>, subroutine_kind = 0 : i32, system_library_cell = "work.native_sampled_explicit_clock_dedup", system_scope_path = "native_sampled_explicit_clock_dedup.second", system_scope_symbol = @s1.$root::@s3.native_sampled_explicit_clock_dedup::@s4.native_sampled_explicit_clock_dedup::@s11.second} {
                      obelisk.sv.expression.named_value attributes {is_signed = false, node_id = 50 : i64, referenced_path = "native_sampled_explicit_clock_dedup.data", referenced_symbol = @s1.$root::@s3.native_sampled_explicit_clock_dedup::@s4.native_sampled_explicit_clock_dedup::@s7.data, semantic_type = !obelisk.integral<1, false, true, 0 : 0, logic>} {
                      }
                      obelisk.sv.expression.integer_literal attributes {constant_value = "1", is_declared_unsized = true, is_signed = true, node_id = 51 : i64, semantic_type = !obelisk.integral<32, true, false, 31 : 0, int>} {
                      }
                      obelisk.sv.expression.named_value attributes {is_signed = false, node_id = 52 : i64, referenced_path = "native_sampled_explicit_clock_dedup.gate", referenced_symbol = @s1.$root::@s3.native_sampled_explicit_clock_dedup::@s4.native_sampled_explicit_clock_dedup::@s8.gate, semantic_type = !obelisk.integral<1, false, true, 0 : 0, logic>} {
                      }
                      obelisk.sv.expression.clocking_event attributes {is_signed = false, node_id = 53 : i64, semantic_type = !obelisk.void} {
                        obelisk.sv.timing.signal_event attributes {edge_kind = 1 : i32, has_iff = false, node_id = 54 : i64} {
                          obelisk.sv.expression.named_value attributes {is_signed = false, node_id = 55 : i64, referenced_path = "native_sampled_explicit_clock_dedup.alternate_clk", referenced_symbol = @s1.$root::@s3.native_sampled_explicit_clock_dedup::@s4.native_sampled_explicit_clock_dedup::@s6.alternate_clk, semantic_type = !obelisk.integral<1, false, true, 0 : 0, logic>} {
                          }
                        }
                      }
                    }
                    obelisk.sv.expression.conversion attributes {folded_constant = "1'b0", is_signed = false, node_id = 56 : i64, semantic_type = !obelisk.integral<1, false, true, 0 : 0, logic>} {
                      obelisk.sv.expression.integer_literal attributes {constant_value = "1'b0", is_signed = false, node_id = 57 : i64, semantic_type = !obelisk.ranged_packed_array<0 : 0 x !obelisk.integral<1, false, false, 0 : 0, bit>>} {
                      }
                    }
                  }
                }
              }
              obelisk.sv.statement.empty attributes {node_id = 58 : i64} {
              }
            }
          }
        }
      }
    }
  }
}

// CHECK-COUNT-1: obelisk_sim.code_unit.decl {{[0-9]+}} in 0 always hierarchy {{.*}} debug "alternate-clock sampler"
// CHECK-COUNT-1: obelisk_sim.spawn {{.*clocked_sample.*}}
// CHECK-COUNT-1: obelisk_sim.assert.clocked_sample_update
