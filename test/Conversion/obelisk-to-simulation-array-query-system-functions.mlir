// RUN: obelisk-opt %s '--lower-obelisk-to-sim=opt-level=0' | FileCheck %s
// RUN: obelisk-opt %s '--lower-obelisk-to-sim=opt-level=0' | FileCheck %s --check-prefix=LOADS
// RUN: obelisk-opt %s '--lower-obelisk-to-sim=opt-level=0' | FileCheck %s --check-prefix=COMPARES
// RUN: obelisk-opt %s '--lower-obelisk-to-sim=opt-level=0' | FileCheck %s --check-prefix=SELECTS
// RUN: obelisk-opt %s '--lower-obelisk-to-sim=opt-level=0' | FileCheck %s --check-prefix=TO-BITS

module {
  obelisk.sv.symbol.definition attributes {definition_kind = 0 : i32, hierarchical_name = "simulation_array_query_system_functions", name = "simulation_array_query_system_functions", node_id = 0 : i64, sym_name = "s0.simulation_array_query_system_functions"} {
  }
  obelisk.sv.symbol.root attributes {hierarchical_name = "\\$root ", name = "$root", node_id = 1 : i64, sym_name = "s1.$root"} {
    obelisk.sv.symbol.compilation_unit attributes {hierarchical_name = "$unit", node_id = 2 : i64, sym_name = "s2"} {
    }
    obelisk.sv.symbol.instance attributes {hierarchical_name = "simulation_array_query_system_functions", is_uninstantiated = false, name = "simulation_array_query_system_functions", node_id = 3 : i64, referenced_path = "simulation_array_query_system_functions", referenced_symbol = @s0.simulation_array_query_system_functions, sym_name = "s3.simulation_array_query_system_functions"} {
      obelisk.sv.symbol.instance_body attributes {hierarchical_name = "simulation_array_query_system_functions", name = "simulation_array_query_system_functions", node_id = 4 : i64, sym_name = "s4.simulation_array_query_system_functions"} {
        obelisk.sv.symbol.variable attributes {hierarchical_name = "simulation_array_query_system_functions.matrix", lifetime = 1 : i32, name = "matrix", node_id = 5 : i64, semantic_type = !obelisk.ranged_unpacked_array<7 : 5 x !obelisk.ranged_packed_array<3 : 1 x !obelisk.ranged_packed_array<2 : 4 x !obelisk.integral<1, false, true, 0 : 0, logic>>>>, sym_name = "s5.matrix"} {
        }
        obelisk.sv.symbol.variable attributes {hierarchical_name = "simulation_array_query_system_functions.dimension", lifetime = 1 : i32, name = "dimension", node_id = 6 : i64, semantic_type = !obelisk.integral<32, true, true, 31 : 0, integer>, sym_name = "s6.dimension"} {
        }
        obelisk.sv.symbol.variable attributes {hierarchical_name = "simulation_array_query_system_functions.result", lifetime = 1 : i32, name = "result", node_id = 7 : i64, semantic_type = !obelisk.integral<32, true, false, 31 : 0, int>, sym_name = "s7.result"} {
        }
        obelisk.sv.symbol.procedural_block attributes {hierarchical_name = "simulation_array_query_system_functions", node_id = 8 : i64, procedure_kind = 0 : i32, sym_name = "s8", time_precision_fs = 1000000 : i64, time_unit_fs = 1000000 : i64} {
          obelisk.sv.statement.block attributes {node_id = 9 : i64} {
            obelisk.sv.statement.list attributes {node_id = 10 : i64} {
              obelisk.sv.statement.expression_statement attributes {node_id = 11 : i64} {
                obelisk.sv.expression.assignment attributes {assignment_kind = 0 : i32, node_id = 12 : i64, semantic_type = !obelisk.integral<32, true, false, 31 : 0, int>} {
                  obelisk.sv.expression.named_value attributes {node_id = 13 : i64, referenced_path = "simulation_array_query_system_functions.result", referenced_symbol = @s1.$root::@s3.simulation_array_query_system_functions::@s4.simulation_array_query_system_functions::@s7.result, semantic_type = !obelisk.integral<32, true, false, 31 : 0, int>} {
                  }
                  obelisk.sv.expression.conversion attributes {node_id = 14 : i64, semantic_type = !obelisk.integral<32, true, false, 31 : 0, int>} {
                    obelisk.sv.expression.call attributes {argument_count = 1 : i64, callee_name = "$dimensions", constraint_restrictions = [], defaulted_arguments = array<i64: 0>, has_inline_constraints = false, has_iterator_expression = false, has_output_arguments = false, has_this_class = false, is_super_class = false, is_system_call = true, node_id = 15 : i64, semantic_type = !obelisk.integral<32, true, true, 31 : 0, integer>, subroutine_kind = 0 : i32, system_library_cell = "work.simulation_array_query_system_functions", system_scope_path = "simulation_array_query_system_functions", system_scope_symbol = @s1.$root::@s3.simulation_array_query_system_functions::@s4.simulation_array_query_system_functions} {
                      obelisk.sv.expression.named_value attributes {node_id = 16 : i64, referenced_path = "simulation_array_query_system_functions.matrix", referenced_symbol = @s1.$root::@s3.simulation_array_query_system_functions::@s4.simulation_array_query_system_functions::@s5.matrix, semantic_type = !obelisk.ranged_unpacked_array<7 : 5 x !obelisk.ranged_packed_array<3 : 1 x !obelisk.ranged_packed_array<2 : 4 x !obelisk.integral<1, false, true, 0 : 0, logic>>>>} {
                      }
                    }
                  }
                }
              }
              obelisk.sv.statement.expression_statement attributes {node_id = 17 : i64} {
                obelisk.sv.expression.assignment attributes {assignment_kind = 0 : i32, node_id = 18 : i64, semantic_type = !obelisk.integral<32, true, false, 31 : 0, int>} {
                  obelisk.sv.expression.named_value attributes {node_id = 19 : i64, referenced_path = "simulation_array_query_system_functions.result", referenced_symbol = @s1.$root::@s3.simulation_array_query_system_functions::@s4.simulation_array_query_system_functions::@s7.result, semantic_type = !obelisk.integral<32, true, false, 31 : 0, int>} {
                  }
                  obelisk.sv.expression.conversion attributes {node_id = 20 : i64, semantic_type = !obelisk.integral<32, true, false, 31 : 0, int>} {
                    obelisk.sv.expression.call attributes {argument_count = 1 : i64, callee_name = "$unpacked_dimensions", constraint_restrictions = [], defaulted_arguments = array<i64: 0>, has_inline_constraints = false, has_iterator_expression = false, has_output_arguments = false, has_this_class = false, is_super_class = false, is_system_call = true, node_id = 21 : i64, semantic_type = !obelisk.integral<32, true, true, 31 : 0, integer>, subroutine_kind = 0 : i32, system_library_cell = "work.simulation_array_query_system_functions", system_scope_path = "simulation_array_query_system_functions", system_scope_symbol = @s1.$root::@s3.simulation_array_query_system_functions::@s4.simulation_array_query_system_functions} {
                      obelisk.sv.expression.named_value attributes {node_id = 22 : i64, referenced_path = "simulation_array_query_system_functions.matrix", referenced_symbol = @s1.$root::@s3.simulation_array_query_system_functions::@s4.simulation_array_query_system_functions::@s5.matrix, semantic_type = !obelisk.ranged_unpacked_array<7 : 5 x !obelisk.ranged_packed_array<3 : 1 x !obelisk.ranged_packed_array<2 : 4 x !obelisk.integral<1, false, true, 0 : 0, logic>>>>} {
                      }
                    }
                  }
                }
              }
              obelisk.sv.statement.expression_statement attributes {node_id = 23 : i64} {
                obelisk.sv.expression.assignment attributes {assignment_kind = 0 : i32, node_id = 24 : i64, semantic_type = !obelisk.integral<32, true, false, 31 : 0, int>} {
                  obelisk.sv.expression.named_value attributes {node_id = 25 : i64, referenced_path = "simulation_array_query_system_functions.result", referenced_symbol = @s1.$root::@s3.simulation_array_query_system_functions::@s4.simulation_array_query_system_functions::@s7.result, semantic_type = !obelisk.integral<32, true, false, 31 : 0, int>} {
                  }
                  obelisk.sv.expression.conversion attributes {node_id = 26 : i64, semantic_type = !obelisk.integral<32, true, false, 31 : 0, int>} {
                    obelisk.sv.expression.call attributes {argument_count = 1 : i64, callee_name = "$left", constraint_restrictions = [], defaulted_arguments = array<i64: 0>, has_inline_constraints = false, has_iterator_expression = false, has_output_arguments = false, has_this_class = false, is_super_class = false, is_system_call = true, node_id = 27 : i64, semantic_type = !obelisk.integral<32, true, true, 31 : 0, integer>, subroutine_kind = 0 : i32, system_library_cell = "work.simulation_array_query_system_functions", system_scope_path = "simulation_array_query_system_functions", system_scope_symbol = @s1.$root::@s3.simulation_array_query_system_functions::@s4.simulation_array_query_system_functions} {
                      obelisk.sv.expression.named_value attributes {node_id = 28 : i64, referenced_path = "simulation_array_query_system_functions.matrix", referenced_symbol = @s1.$root::@s3.simulation_array_query_system_functions::@s4.simulation_array_query_system_functions::@s5.matrix, semantic_type = !obelisk.ranged_unpacked_array<7 : 5 x !obelisk.ranged_packed_array<3 : 1 x !obelisk.ranged_packed_array<2 : 4 x !obelisk.integral<1, false, true, 0 : 0, logic>>>>} {
                      }
                    }
                  }
                }
              }
              obelisk.sv.statement.expression_statement attributes {node_id = 29 : i64} {
                obelisk.sv.expression.assignment attributes {assignment_kind = 0 : i32, node_id = 30 : i64, semantic_type = !obelisk.integral<32, true, false, 31 : 0, int>} {
                  obelisk.sv.expression.named_value attributes {node_id = 31 : i64, referenced_path = "simulation_array_query_system_functions.result", referenced_symbol = @s1.$root::@s3.simulation_array_query_system_functions::@s4.simulation_array_query_system_functions::@s7.result, semantic_type = !obelisk.integral<32, true, false, 31 : 0, int>} {
                  }
                  obelisk.sv.expression.conversion attributes {node_id = 32 : i64, semantic_type = !obelisk.integral<32, true, false, 31 : 0, int>} {
                    obelisk.sv.expression.call attributes {argument_count = 2 : i64, callee_name = "$right", constraint_restrictions = [], defaulted_arguments = array<i64: 0, 0>, has_inline_constraints = false, has_iterator_expression = false, has_output_arguments = false, has_this_class = false, is_super_class = false, is_system_call = true, node_id = 33 : i64, semantic_type = !obelisk.integral<32, true, true, 31 : 0, integer>, subroutine_kind = 0 : i32, system_library_cell = "work.simulation_array_query_system_functions", system_scope_path = "simulation_array_query_system_functions", system_scope_symbol = @s1.$root::@s3.simulation_array_query_system_functions::@s4.simulation_array_query_system_functions} {
                      obelisk.sv.expression.named_value attributes {node_id = 34 : i64, referenced_path = "simulation_array_query_system_functions.matrix", referenced_symbol = @s1.$root::@s3.simulation_array_query_system_functions::@s4.simulation_array_query_system_functions::@s5.matrix, semantic_type = !obelisk.ranged_unpacked_array<7 : 5 x !obelisk.ranged_packed_array<3 : 1 x !obelisk.ranged_packed_array<2 : 4 x !obelisk.integral<1, false, true, 0 : 0, logic>>>>} {
                      }
                      obelisk.sv.expression.named_value attributes {node_id = 35 : i64, referenced_path = "simulation_array_query_system_functions.dimension", referenced_symbol = @s1.$root::@s3.simulation_array_query_system_functions::@s4.simulation_array_query_system_functions::@s6.dimension, semantic_type = !obelisk.integral<32, true, true, 31 : 0, integer>} {
                      }
                    }
                  }
                }
              }
              obelisk.sv.statement.expression_statement attributes {node_id = 36 : i64} {
                obelisk.sv.expression.assignment attributes {assignment_kind = 0 : i32, node_id = 37 : i64, semantic_type = !obelisk.integral<32, true, false, 31 : 0, int>} {
                  obelisk.sv.expression.named_value attributes {node_id = 38 : i64, referenced_path = "simulation_array_query_system_functions.result", referenced_symbol = @s1.$root::@s3.simulation_array_query_system_functions::@s4.simulation_array_query_system_functions::@s7.result, semantic_type = !obelisk.integral<32, true, false, 31 : 0, int>} {
                  }
                  obelisk.sv.expression.conversion attributes {node_id = 39 : i64, semantic_type = !obelisk.integral<32, true, false, 31 : 0, int>} {
                    obelisk.sv.expression.call attributes {argument_count = 2 : i64, callee_name = "$low", constraint_restrictions = [], defaulted_arguments = array<i64: 0, 0>, has_inline_constraints = false, has_iterator_expression = false, has_output_arguments = false, has_this_class = false, is_super_class = false, is_system_call = true, node_id = 40 : i64, semantic_type = !obelisk.integral<32, true, true, 31 : 0, integer>, subroutine_kind = 0 : i32, system_library_cell = "work.simulation_array_query_system_functions", system_scope_path = "simulation_array_query_system_functions", system_scope_symbol = @s1.$root::@s3.simulation_array_query_system_functions::@s4.simulation_array_query_system_functions} {
                      obelisk.sv.expression.named_value attributes {node_id = 41 : i64, referenced_path = "simulation_array_query_system_functions.matrix", referenced_symbol = @s1.$root::@s3.simulation_array_query_system_functions::@s4.simulation_array_query_system_functions::@s5.matrix, semantic_type = !obelisk.ranged_unpacked_array<7 : 5 x !obelisk.ranged_packed_array<3 : 1 x !obelisk.ranged_packed_array<2 : 4 x !obelisk.integral<1, false, true, 0 : 0, logic>>>>} {
                      }
                      obelisk.sv.expression.named_value attributes {node_id = 42 : i64, referenced_path = "simulation_array_query_system_functions.dimension", referenced_symbol = @s1.$root::@s3.simulation_array_query_system_functions::@s4.simulation_array_query_system_functions::@s6.dimension, semantic_type = !obelisk.integral<32, true, true, 31 : 0, integer>} {
                      }
                    }
                  }
                }
              }
              obelisk.sv.statement.expression_statement attributes {node_id = 43 : i64} {
                obelisk.sv.expression.assignment attributes {assignment_kind = 0 : i32, node_id = 44 : i64, semantic_type = !obelisk.integral<32, true, false, 31 : 0, int>} {
                  obelisk.sv.expression.named_value attributes {node_id = 45 : i64, referenced_path = "simulation_array_query_system_functions.result", referenced_symbol = @s1.$root::@s3.simulation_array_query_system_functions::@s4.simulation_array_query_system_functions::@s7.result, semantic_type = !obelisk.integral<32, true, false, 31 : 0, int>} {
                  }
                  obelisk.sv.expression.conversion attributes {node_id = 46 : i64, semantic_type = !obelisk.integral<32, true, false, 31 : 0, int>} {
                    obelisk.sv.expression.call attributes {argument_count = 2 : i64, callee_name = "$high", constraint_restrictions = [], defaulted_arguments = array<i64: 0, 0>, has_inline_constraints = false, has_iterator_expression = false, has_output_arguments = false, has_this_class = false, is_super_class = false, is_system_call = true, node_id = 47 : i64, semantic_type = !obelisk.integral<32, true, true, 31 : 0, integer>, subroutine_kind = 0 : i32, system_library_cell = "work.simulation_array_query_system_functions", system_scope_path = "simulation_array_query_system_functions", system_scope_symbol = @s1.$root::@s3.simulation_array_query_system_functions::@s4.simulation_array_query_system_functions} {
                      obelisk.sv.expression.named_value attributes {node_id = 48 : i64, referenced_path = "simulation_array_query_system_functions.matrix", referenced_symbol = @s1.$root::@s3.simulation_array_query_system_functions::@s4.simulation_array_query_system_functions::@s5.matrix, semantic_type = !obelisk.ranged_unpacked_array<7 : 5 x !obelisk.ranged_packed_array<3 : 1 x !obelisk.ranged_packed_array<2 : 4 x !obelisk.integral<1, false, true, 0 : 0, logic>>>>} {
                      }
                      obelisk.sv.expression.named_value attributes {node_id = 49 : i64, referenced_path = "simulation_array_query_system_functions.dimension", referenced_symbol = @s1.$root::@s3.simulation_array_query_system_functions::@s4.simulation_array_query_system_functions::@s6.dimension, semantic_type = !obelisk.integral<32, true, true, 31 : 0, integer>} {
                      }
                    }
                  }
                }
              }
              obelisk.sv.statement.expression_statement attributes {node_id = 50 : i64} {
                obelisk.sv.expression.assignment attributes {assignment_kind = 0 : i32, node_id = 51 : i64, semantic_type = !obelisk.integral<32, true, false, 31 : 0, int>} {
                  obelisk.sv.expression.named_value attributes {node_id = 52 : i64, referenced_path = "simulation_array_query_system_functions.result", referenced_symbol = @s1.$root::@s3.simulation_array_query_system_functions::@s4.simulation_array_query_system_functions::@s7.result, semantic_type = !obelisk.integral<32, true, false, 31 : 0, int>} {
                  }
                  obelisk.sv.expression.conversion attributes {node_id = 53 : i64, semantic_type = !obelisk.integral<32, true, false, 31 : 0, int>} {
                    obelisk.sv.expression.call attributes {argument_count = 2 : i64, callee_name = "$increment", constraint_restrictions = [], defaulted_arguments = array<i64: 0, 0>, has_inline_constraints = false, has_iterator_expression = false, has_output_arguments = false, has_this_class = false, is_super_class = false, is_system_call = true, node_id = 54 : i64, semantic_type = !obelisk.integral<32, true, true, 31 : 0, integer>, subroutine_kind = 0 : i32, system_library_cell = "work.simulation_array_query_system_functions", system_scope_path = "simulation_array_query_system_functions", system_scope_symbol = @s1.$root::@s3.simulation_array_query_system_functions::@s4.simulation_array_query_system_functions} {
                      obelisk.sv.expression.named_value attributes {node_id = 55 : i64, referenced_path = "simulation_array_query_system_functions.matrix", referenced_symbol = @s1.$root::@s3.simulation_array_query_system_functions::@s4.simulation_array_query_system_functions::@s5.matrix, semantic_type = !obelisk.ranged_unpacked_array<7 : 5 x !obelisk.ranged_packed_array<3 : 1 x !obelisk.ranged_packed_array<2 : 4 x !obelisk.integral<1, false, true, 0 : 0, logic>>>>} {
                      }
                      obelisk.sv.expression.named_value attributes {node_id = 56 : i64, referenced_path = "simulation_array_query_system_functions.dimension", referenced_symbol = @s1.$root::@s3.simulation_array_query_system_functions::@s4.simulation_array_query_system_functions::@s6.dimension, semantic_type = !obelisk.integral<32, true, true, 31 : 0, integer>} {
                      }
                    }
                  }
                }
              }
              obelisk.sv.statement.expression_statement attributes {node_id = 57 : i64} {
                obelisk.sv.expression.assignment attributes {assignment_kind = 0 : i32, node_id = 58 : i64, semantic_type = !obelisk.integral<32, true, false, 31 : 0, int>} {
                  obelisk.sv.expression.named_value attributes {node_id = 59 : i64, referenced_path = "simulation_array_query_system_functions.result", referenced_symbol = @s1.$root::@s3.simulation_array_query_system_functions::@s4.simulation_array_query_system_functions::@s7.result, semantic_type = !obelisk.integral<32, true, false, 31 : 0, int>} {
                  }
                  obelisk.sv.expression.conversion attributes {node_id = 60 : i64, semantic_type = !obelisk.integral<32, true, false, 31 : 0, int>} {
                    obelisk.sv.expression.call attributes {argument_count = 2 : i64, callee_name = "$size", constraint_restrictions = [], defaulted_arguments = array<i64: 0, 0>, has_inline_constraints = false, has_iterator_expression = false, has_output_arguments = false, has_this_class = false, is_super_class = false, is_system_call = true, node_id = 61 : i64, semantic_type = !obelisk.integral<32, true, true, 31 : 0, integer>, subroutine_kind = 0 : i32, system_library_cell = "work.simulation_array_query_system_functions", system_scope_path = "simulation_array_query_system_functions", system_scope_symbol = @s1.$root::@s3.simulation_array_query_system_functions::@s4.simulation_array_query_system_functions} {
                      obelisk.sv.expression.named_value attributes {node_id = 62 : i64, referenced_path = "simulation_array_query_system_functions.matrix", referenced_symbol = @s1.$root::@s3.simulation_array_query_system_functions::@s4.simulation_array_query_system_functions::@s5.matrix, semantic_type = !obelisk.ranged_unpacked_array<7 : 5 x !obelisk.ranged_packed_array<3 : 1 x !obelisk.ranged_packed_array<2 : 4 x !obelisk.integral<1, false, true, 0 : 0, logic>>>>} {
                      }
                      obelisk.sv.expression.named_value attributes {node_id = 63 : i64, referenced_path = "simulation_array_query_system_functions.dimension", referenced_symbol = @s1.$root::@s3.simulation_array_query_system_functions::@s4.simulation_array_query_system_functions::@s6.dimension, semantic_type = !obelisk.integral<32, true, true, 31 : 0, integer>} {
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

// CHECK-LABEL: obelisk_sim.func private @unit_0(
// CHECK-DAG: %[[DIMENSIONS:.*]] = arith.constant 3 : i32
// CHECK-DAG: %[[UNPACKED:.*]] = arith.constant 1 : i32
// CHECK-DAG: %[[LEFT:.*]] = arith.constant 7 : i32
// CHECK: obelisk_sim.ref.store %[[DIMENSIONS]]
// CHECK-NEXT: obelisk_sim.ref.store %[[UNPACKED]]
// CHECK-NEXT: obelisk_sim.ref.store %[[LEFT]]
// CHECK-NOT: obelisk.sv.
// LOADS-COUNT-5: obelisk_sim.ref.load
// COMPARES-COUNT-15: obelisk_sim.logic.compare case_eq
// SELECTS-COUNT-15: arith.select
// TO-BITS-COUNT-5: obelisk_sim.logic.to_bits
