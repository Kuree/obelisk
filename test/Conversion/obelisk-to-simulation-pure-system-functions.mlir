// RUN: obelisk-opt %s '--lower-obelisk-to-sim=opt-level=0' | FileCheck %s
// RUN: obelisk-opt %s '--lower-obelisk-to-sim=opt-level=0' | FileCheck %s --check-prefix=COUNT

module {
  obelisk.sv.symbol.definition attributes {definition_kind = 0 : i32, hierarchical_name = "simulation_pure_system_functions", name = "simulation_pure_system_functions", node_id = 0 : i64, sym_name = "s0.simulation_pure_system_functions"} {
  }
  obelisk.sv.symbol.root attributes {hierarchical_name = "\\$root ", name = "$root", node_id = 1 : i64, sym_name = "s1.$root"} {
    obelisk.sv.symbol.compilation_unit attributes {hierarchical_name = "$unit", node_id = 2 : i64, sym_name = "s2"} {
    }
    obelisk.sv.symbol.instance attributes {hierarchical_name = "simulation_pure_system_functions", is_uninstantiated = false, name = "simulation_pure_system_functions", node_id = 3 : i64, referenced_path = "simulation_pure_system_functions", referenced_symbol = @s0.simulation_pure_system_functions, sym_name = "s3.simulation_pure_system_functions"} {
      obelisk.sv.symbol.instance_body attributes {hierarchical_name = "simulation_pure_system_functions", name = "simulation_pure_system_functions", node_id = 4 : i64, sym_name = "s4.simulation_pure_system_functions"} {
        obelisk.sv.symbol.variable attributes {hierarchical_name = "simulation_pure_system_functions.value", lifetime = 1 : i32, name = "value", node_id = 5 : i64, semantic_type = !obelisk.ranged_packed_array<11 : 0 x !obelisk.integral<1, false, true, 0 : 0, logic>>, sym_name = "s5.value"} {
        }
        obelisk.sv.symbol.variable attributes {hierarchical_name = "simulation_pure_system_functions.control", lifetime = 1 : i32, name = "control", node_id = 6 : i64, semantic_type = !obelisk.integral<1, false, true, 0 : 0, logic>, sym_name = "s6.control"} {
        }
        obelisk.sv.symbol.variable attributes {hierarchical_name = "simulation_pure_system_functions.result", lifetime = 1 : i32, name = "result", node_id = 7 : i64, semantic_type = !obelisk.integral<32, true, false, 31 : 0, int>, sym_name = "s7.result"} {
        }
        obelisk.sv.symbol.subroutine attributes {hierarchical_name = "simulation_pure_system_functions.side_effect", name = "side_effect", node_id = 8 : i64, return_variable_path = "simulation_pure_system_functions.side_effect.side_effect", return_variable_symbol = @s1.$root::@s3.simulation_pure_system_functions::@s4.simulation_pure_system_functions::@s8.side_effect::@s9.side_effect, semantic_type = !obelisk.subroutine<() -> !obelisk.integral<32, true, false, 31 : 0, int>, false>, subroutine_kind = 0 : i32, sym_name = "s8.side_effect", time_precision_fs = 1000000 : i64, time_unit_fs = 1000000 : i64} {
          obelisk.sv.statement.list attributes {node_id = 9 : i64} {
            obelisk.sv.statement.expression_statement attributes {node_id = 10 : i64} {
              obelisk.sv.expression.unary_op attributes {node_id = 11 : i64, operator_kind = 12 : i32, semantic_type = !obelisk.integral<32, true, false, 31 : 0, int>} {
                obelisk.sv.expression.named_value attributes {node_id = 12 : i64, referenced_path = "simulation_pure_system_functions.result", referenced_symbol = @s1.$root::@s3.simulation_pure_system_functions::@s4.simulation_pure_system_functions::@s7.result, semantic_type = !obelisk.integral<32, true, false, 31 : 0, int>} {
                }
              }
            }
            obelisk.sv.statement.return attributes {node_id = 13 : i64} {
              obelisk.sv.expression.named_value attributes {node_id = 14 : i64, referenced_path = "simulation_pure_system_functions.result", referenced_symbol = @s1.$root::@s3.simulation_pure_system_functions::@s4.simulation_pure_system_functions::@s7.result, semantic_type = !obelisk.integral<32, true, false, 31 : 0, int>} {
              }
            }
          }
          obelisk.sv.symbol.variable attributes {hierarchical_name = "simulation_pure_system_functions.side_effect.side_effect", is_compiler_generated, name = "side_effect", node_id = 15 : i64, semantic_type = !obelisk.integral<32, true, false, 31 : 0, int>, sym_name = "s9.side_effect"} {
          }
        }
        obelisk.sv.symbol.procedural_block attributes {hierarchical_name = "simulation_pure_system_functions", node_id = 16 : i64, procedure_kind = 0 : i32, sym_name = "s10", time_precision_fs = 1000000 : i64, time_unit_fs = 1000000 : i64} {
          obelisk.sv.statement.block attributes {node_id = 17 : i64} {
            obelisk.sv.statement.list attributes {node_id = 18 : i64} {
              obelisk.sv.statement.expression_statement attributes {node_id = 19 : i64} {
                obelisk.sv.expression.assignment attributes {assignment_kind = 0 : i32, node_id = 20 : i64, semantic_type = !obelisk.integral<32, true, false, 31 : 0, int>} {
                  obelisk.sv.expression.named_value attributes {node_id = 21 : i64, referenced_path = "simulation_pure_system_functions.result", referenced_symbol = @s1.$root::@s3.simulation_pure_system_functions::@s4.simulation_pure_system_functions::@s7.result, semantic_type = !obelisk.integral<32, true, false, 31 : 0, int>} {
                  }
                  obelisk.sv.expression.conversion attributes {node_id = 22 : i64, semantic_type = !obelisk.integral<32, true, false, 31 : 0, int>} {
                    obelisk.sv.expression.call attributes {argument_count = 1 : i64, callee_name = "$bits", constraint_restrictions = [], defaulted_arguments = array<i64: 0>, has_inline_constraints = false, has_iterator_expression = false, has_output_arguments = false, has_this_class = false, is_super_class = false, is_system_call = true, node_id = 23 : i64, semantic_type = !obelisk.integral<32, true, true, 31 : 0, integer>, subroutine_kind = 0 : i32, system_library_cell = "work.simulation_pure_system_functions", system_scope_path = "simulation_pure_system_functions", system_scope_symbol = @s1.$root::@s3.simulation_pure_system_functions::@s4.simulation_pure_system_functions} {
                      obelisk.sv.expression.call attributes {argument_count = 0 : i64, callee_name = "side_effect", constraint_restrictions = [], defaulted_arguments = array<i64>, has_inline_constraints = false, has_iterator_expression = false, has_output_arguments = false, has_this_class = false, is_super_class = false, is_system_call = false, node_id = 24 : i64, referenced_path = "simulation_pure_system_functions.side_effect", referenced_symbol = @s1.$root::@s3.simulation_pure_system_functions::@s4.simulation_pure_system_functions::@s8.side_effect, semantic_type = !obelisk.integral<32, true, false, 31 : 0, int>, subroutine_kind = 0 : i32} {
                      }
                    }
                  }
                }
              }
              obelisk.sv.statement.expression_statement attributes {node_id = 25 : i64} {
                obelisk.sv.expression.assignment attributes {assignment_kind = 0 : i32, node_id = 26 : i64, semantic_type = !obelisk.integral<32, true, false, 31 : 0, int>} {
                  obelisk.sv.expression.named_value attributes {node_id = 27 : i64, referenced_path = "simulation_pure_system_functions.result", referenced_symbol = @s1.$root::@s3.simulation_pure_system_functions::@s4.simulation_pure_system_functions::@s7.result, semantic_type = !obelisk.integral<32, true, false, 31 : 0, int>} {
                  }
                  obelisk.sv.expression.conversion attributes {node_id = 28 : i64, semantic_type = !obelisk.integral<32, true, false, 31 : 0, int>} {
                    obelisk.sv.expression.call attributes {argument_count = 1 : i64, callee_name = "$clog2", constraint_restrictions = [], defaulted_arguments = array<i64: 0>, has_inline_constraints = false, has_iterator_expression = false, has_output_arguments = false, has_this_class = false, is_super_class = false, is_system_call = true, node_id = 29 : i64, semantic_type = !obelisk.integral<32, true, true, 31 : 0, integer>, subroutine_kind = 0 : i32, system_library_cell = "work.simulation_pure_system_functions", system_scope_path = "simulation_pure_system_functions", system_scope_symbol = @s1.$root::@s3.simulation_pure_system_functions::@s4.simulation_pure_system_functions} {
                      obelisk.sv.expression.named_value attributes {node_id = 30 : i64, referenced_path = "simulation_pure_system_functions.value", referenced_symbol = @s1.$root::@s3.simulation_pure_system_functions::@s4.simulation_pure_system_functions::@s5.value, semantic_type = !obelisk.ranged_packed_array<11 : 0 x !obelisk.integral<1, false, true, 0 : 0, logic>>} {
                      }
                    }
                  }
                }
              }
              obelisk.sv.statement.expression_statement attributes {node_id = 31 : i64} {
                obelisk.sv.expression.assignment attributes {assignment_kind = 0 : i32, node_id = 32 : i64, semantic_type = !obelisk.integral<32, true, false, 31 : 0, int>} {
                  obelisk.sv.expression.named_value attributes {node_id = 33 : i64, referenced_path = "simulation_pure_system_functions.result", referenced_symbol = @s1.$root::@s3.simulation_pure_system_functions::@s4.simulation_pure_system_functions::@s7.result, semantic_type = !obelisk.integral<32, true, false, 31 : 0, int>} {
                  }
                  obelisk.sv.expression.call attributes {argument_count = 5 : i64, callee_name = "$countbits", constraint_restrictions = [], defaulted_arguments = array<i64: 0, 0, 0, 0, 0>, has_inline_constraints = false, has_iterator_expression = false, has_output_arguments = false, has_this_class = false, is_super_class = false, is_system_call = true, node_id = 34 : i64, semantic_type = !obelisk.integral<32, true, false, 31 : 0, int>, subroutine_kind = 0 : i32, system_library_cell = "work.simulation_pure_system_functions", system_scope_path = "simulation_pure_system_functions", system_scope_symbol = @s1.$root::@s3.simulation_pure_system_functions::@s4.simulation_pure_system_functions} {
                    obelisk.sv.expression.named_value attributes {node_id = 35 : i64, referenced_path = "simulation_pure_system_functions.value", referenced_symbol = @s1.$root::@s3.simulation_pure_system_functions::@s4.simulation_pure_system_functions::@s5.value, semantic_type = !obelisk.ranged_packed_array<11 : 0 x !obelisk.integral<1, false, true, 0 : 0, logic>>} {
                    }
                    obelisk.sv.expression.integer_literal attributes {constant_value = "1'b0", node_id = 36 : i64, semantic_type = !obelisk.ranged_packed_array<0 : 0 x !obelisk.integral<1, false, false, 0 : 0, bit>>} {
                    }
                    obelisk.sv.expression.named_value attributes {node_id = 37 : i64, referenced_path = "simulation_pure_system_functions.control", referenced_symbol = @s1.$root::@s3.simulation_pure_system_functions::@s4.simulation_pure_system_functions::@s6.control, semantic_type = !obelisk.integral<1, false, true, 0 : 0, logic>} {
                    }
                    obelisk.sv.expression.integer_literal attributes {constant_value = "1'bx", node_id = 38 : i64, semantic_type = !obelisk.ranged_packed_array<0 : 0 x !obelisk.integral<1, false, true, 0 : 0, logic>>} {
                    }
                    obelisk.sv.expression.integer_literal attributes {constant_value = "1'bz", node_id = 39 : i64, semantic_type = !obelisk.ranged_packed_array<0 : 0 x !obelisk.integral<1, false, true, 0 : 0, logic>>} {
                    }
                  }
                }
              }
              obelisk.sv.statement.expression_statement attributes {node_id = 40 : i64} {
                obelisk.sv.expression.assignment attributes {assignment_kind = 0 : i32, node_id = 41 : i64, semantic_type = !obelisk.integral<32, true, false, 31 : 0, int>} {
                  obelisk.sv.expression.named_value attributes {node_id = 42 : i64, referenced_path = "simulation_pure_system_functions.result", referenced_symbol = @s1.$root::@s3.simulation_pure_system_functions::@s4.simulation_pure_system_functions::@s7.result, semantic_type = !obelisk.integral<32, true, false, 31 : 0, int>} {
                  }
                  obelisk.sv.expression.call attributes {argument_count = 1 : i64, callee_name = "$countones", constraint_restrictions = [], defaulted_arguments = array<i64: 0>, has_inline_constraints = false, has_iterator_expression = false, has_output_arguments = false, has_this_class = false, is_super_class = false, is_system_call = true, node_id = 43 : i64, semantic_type = !obelisk.integral<32, true, false, 31 : 0, int>, subroutine_kind = 0 : i32, system_library_cell = "work.simulation_pure_system_functions", system_scope_path = "simulation_pure_system_functions", system_scope_symbol = @s1.$root::@s3.simulation_pure_system_functions::@s4.simulation_pure_system_functions} {
                    obelisk.sv.expression.named_value attributes {node_id = 44 : i64, referenced_path = "simulation_pure_system_functions.value", referenced_symbol = @s1.$root::@s3.simulation_pure_system_functions::@s4.simulation_pure_system_functions::@s5.value, semantic_type = !obelisk.ranged_packed_array<11 : 0 x !obelisk.integral<1, false, true, 0 : 0, logic>>} {
                    }
                  }
                }
              }
              obelisk.sv.statement.expression_statement attributes {node_id = 45 : i64} {
                obelisk.sv.expression.assignment attributes {assignment_kind = 0 : i32, node_id = 46 : i64, semantic_type = !obelisk.integral<32, true, false, 31 : 0, int>} {
                  obelisk.sv.expression.named_value attributes {node_id = 47 : i64, referenced_path = "simulation_pure_system_functions.result", referenced_symbol = @s1.$root::@s3.simulation_pure_system_functions::@s4.simulation_pure_system_functions::@s7.result, semantic_type = !obelisk.integral<32, true, false, 31 : 0, int>} {
                  }
                  obelisk.sv.expression.conversion attributes {node_id = 48 : i64, semantic_type = !obelisk.integral<32, true, false, 31 : 0, int>} {
                    obelisk.sv.expression.conversion attributes {node_id = 49 : i64, semantic_type = !obelisk.ranged_packed_array<31 : 0 x !obelisk.integral<1, false, false, 0 : 0, bit>>} {
                      obelisk.sv.expression.call attributes {argument_count = 1 : i64, callee_name = "$onehot", constraint_restrictions = [], defaulted_arguments = array<i64: 0>, has_inline_constraints = false, has_iterator_expression = false, has_output_arguments = false, has_this_class = false, is_super_class = false, is_system_call = true, node_id = 50 : i64, semantic_type = !obelisk.integral<1, false, false, 0 : 0, bit>, subroutine_kind = 0 : i32, system_library_cell = "work.simulation_pure_system_functions", system_scope_path = "simulation_pure_system_functions", system_scope_symbol = @s1.$root::@s3.simulation_pure_system_functions::@s4.simulation_pure_system_functions} {
                        obelisk.sv.expression.named_value attributes {node_id = 51 : i64, referenced_path = "simulation_pure_system_functions.value", referenced_symbol = @s1.$root::@s3.simulation_pure_system_functions::@s4.simulation_pure_system_functions::@s5.value, semantic_type = !obelisk.ranged_packed_array<11 : 0 x !obelisk.integral<1, false, true, 0 : 0, logic>>} {
                        }
                      }
                    }
                  }
                }
              }
              obelisk.sv.statement.expression_statement attributes {node_id = 52 : i64} {
                obelisk.sv.expression.assignment attributes {assignment_kind = 0 : i32, node_id = 53 : i64, semantic_type = !obelisk.integral<32, true, false, 31 : 0, int>} {
                  obelisk.sv.expression.named_value attributes {node_id = 54 : i64, referenced_path = "simulation_pure_system_functions.result", referenced_symbol = @s1.$root::@s3.simulation_pure_system_functions::@s4.simulation_pure_system_functions::@s7.result, semantic_type = !obelisk.integral<32, true, false, 31 : 0, int>} {
                  }
                  obelisk.sv.expression.conversion attributes {node_id = 55 : i64, semantic_type = !obelisk.integral<32, true, false, 31 : 0, int>} {
                    obelisk.sv.expression.conversion attributes {node_id = 56 : i64, semantic_type = !obelisk.ranged_packed_array<31 : 0 x !obelisk.integral<1, false, false, 0 : 0, bit>>} {
                      obelisk.sv.expression.call attributes {argument_count = 1 : i64, callee_name = "$onehot0", constraint_restrictions = [], defaulted_arguments = array<i64: 0>, has_inline_constraints = false, has_iterator_expression = false, has_output_arguments = false, has_this_class = false, is_super_class = false, is_system_call = true, node_id = 57 : i64, semantic_type = !obelisk.integral<1, false, false, 0 : 0, bit>, subroutine_kind = 0 : i32, system_library_cell = "work.simulation_pure_system_functions", system_scope_path = "simulation_pure_system_functions", system_scope_symbol = @s1.$root::@s3.simulation_pure_system_functions::@s4.simulation_pure_system_functions} {
                        obelisk.sv.expression.named_value attributes {node_id = 58 : i64, referenced_path = "simulation_pure_system_functions.value", referenced_symbol = @s1.$root::@s3.simulation_pure_system_functions::@s4.simulation_pure_system_functions::@s5.value, semantic_type = !obelisk.ranged_packed_array<11 : 0 x !obelisk.integral<1, false, true, 0 : 0, logic>>} {
                        }
                      }
                    }
                  }
                }
              }
              obelisk.sv.statement.expression_statement attributes {node_id = 59 : i64} {
                obelisk.sv.expression.assignment attributes {assignment_kind = 0 : i32, node_id = 60 : i64, semantic_type = !obelisk.integral<32, true, false, 31 : 0, int>} {
                  obelisk.sv.expression.named_value attributes {node_id = 61 : i64, referenced_path = "simulation_pure_system_functions.result", referenced_symbol = @s1.$root::@s3.simulation_pure_system_functions::@s4.simulation_pure_system_functions::@s7.result, semantic_type = !obelisk.integral<32, true, false, 31 : 0, int>} {
                  }
                  obelisk.sv.expression.conversion attributes {node_id = 62 : i64, semantic_type = !obelisk.integral<32, true, false, 31 : 0, int>} {
                    obelisk.sv.expression.conversion attributes {node_id = 63 : i64, semantic_type = !obelisk.ranged_packed_array<31 : 0 x !obelisk.integral<1, false, false, 0 : 0, bit>>} {
                      obelisk.sv.expression.call attributes {argument_count = 1 : i64, callee_name = "$isunknown", constraint_restrictions = [], defaulted_arguments = array<i64: 0>, has_inline_constraints = false, has_iterator_expression = false, has_output_arguments = false, has_this_class = false, is_super_class = false, is_system_call = true, node_id = 64 : i64, semantic_type = !obelisk.integral<1, false, false, 0 : 0, bit>, subroutine_kind = 0 : i32, system_library_cell = "work.simulation_pure_system_functions", system_scope_path = "simulation_pure_system_functions", system_scope_symbol = @s1.$root::@s3.simulation_pure_system_functions::@s4.simulation_pure_system_functions} {
                        obelisk.sv.expression.named_value attributes {node_id = 65 : i64, referenced_path = "simulation_pure_system_functions.value", referenced_symbol = @s1.$root::@s3.simulation_pure_system_functions::@s4.simulation_pure_system_functions::@s5.value, semantic_type = !obelisk.ranged_packed_array<11 : 0 x !obelisk.integral<1, false, true, 0 : 0, logic>>} {
                        }
                      }
                    }
                  }
                }
              }
              obelisk.sv.statement.expression_statement attributes {node_id = 66 : i64} {
                obelisk.sv.expression.assignment attributes {assignment_kind = 0 : i32, node_id = 67 : i64, semantic_type = !obelisk.ranged_packed_array<11 : 0 x !obelisk.integral<1, false, true, 0 : 0, logic>>} {
                  obelisk.sv.expression.named_value attributes {node_id = 68 : i64, referenced_path = "simulation_pure_system_functions.value", referenced_symbol = @s1.$root::@s3.simulation_pure_system_functions::@s4.simulation_pure_system_functions::@s5.value, semantic_type = !obelisk.ranged_packed_array<11 : 0 x !obelisk.integral<1, false, true, 0 : 0, logic>>} {
                  }
                  obelisk.sv.expression.call attributes {argument_count = 1 : i64, callee_name = "$unsigned", constraint_restrictions = [], defaulted_arguments = array<i64: 0>, has_inline_constraints = false, has_iterator_expression = false, has_output_arguments = false, has_this_class = false, is_super_class = false, is_system_call = true, node_id = 69 : i64, semantic_type = !obelisk.ranged_packed_array<11 : 0 x !obelisk.integral<1, false, true, 0 : 0, logic>>, subroutine_kind = 0 : i32, system_library_cell = "work.simulation_pure_system_functions", system_scope_path = "simulation_pure_system_functions", system_scope_symbol = @s1.$root::@s3.simulation_pure_system_functions::@s4.simulation_pure_system_functions} {
                    obelisk.sv.expression.call attributes {argument_count = 1 : i64, callee_name = "$signed", constraint_restrictions = [], defaulted_arguments = array<i64: 0>, has_inline_constraints = false, has_iterator_expression = false, has_output_arguments = false, has_this_class = false, is_super_class = false, is_system_call = true, node_id = 70 : i64, semantic_type = !obelisk.ranged_packed_array<11 : 0 x !obelisk.integral<1, true, true, 0 : 0, logic>>, subroutine_kind = 0 : i32, system_library_cell = "work.simulation_pure_system_functions", system_scope_path = "simulation_pure_system_functions", system_scope_symbol = @s1.$root::@s3.simulation_pure_system_functions::@s4.simulation_pure_system_functions} {
                      obelisk.sv.expression.named_value attributes {node_id = 71 : i64, referenced_path = "simulation_pure_system_functions.value", referenced_symbol = @s1.$root::@s3.simulation_pure_system_functions::@s4.simulation_pure_system_functions::@s5.value, semantic_type = !obelisk.ranged_packed_array<11 : 0 x !obelisk.integral<1, false, true, 0 : 0, logic>>} {
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
  }
}

// CHECK-LABEL: obelisk_sim.func private @unit_1(
// CHECK: %[[BITS:.*]] = arith.constant 32 : i32
// CHECK-NEXT: obelisk_sim.ref.store %[[BITS]]
// CHECK: %[[CLOG2:.*]] = obelisk_sim.logic.clog2
// CHECK-NEXT: obelisk_sim.ref.store %[[CLOG2]]
// CHECK: obelisk_sim.logic.count_bits {{.*}} matching {{.*}}, {{.*}}, {{.*}}, {{.*}}
// CHECK: %[[ONEHOT:.*]] = arith.cmpi eq
// CHECK: %[[ONEHOT0:.*]] = arith.cmpi ule
// CHECK: %[[ISUNKNOWN:.*]] = arith.cmpi ne
// CHECK: %[[VALUE:.*]] = obelisk_sim.ref.load
// CHECK-NEXT: obelisk_sim.ref.store %[[VALUE]]
// CHECK-NOT: obelisk_sim.call
// CHECK-NOT: obelisk.sv.
// COUNT-COUNT-5: obelisk_sim.logic.count_bits
