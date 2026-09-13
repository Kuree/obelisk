// RUN: %split-file %s %t
// RUN: obelisk-opt %t/zero.mlir --obelisk-sim-prepare 2>&1 | FileCheck %s --implicit-check-not='error:'
// RUN: obelisk-opt %t/stream.mlir --obelisk-sim-prepare 2>&1 | FileCheck %s --implicit-check-not='error:'
// Speculative storage-alias queries must reject non-addressable expressions
// before attempting to normalize their void-typed children.
// CHECK: obelisk_sim.design

//--- zero.mlir
module attributes {obelisk.coverage.language_version = 2023 : i32} {
  obelisk.sv.symbol.definition attributes {definition_kind = 0 : i32, hierarchical_name = "top", name = "top", node_id = 0 : i64, sym_name = "s0.top"} {
  }
  obelisk.sv.symbol.root attributes {hierarchical_name = "\\$root ", name = "$root", node_id = 1 : i64, sym_name = "s1.$root"} {
    obelisk.sv.symbol.compilation_unit attributes {hierarchical_name = "$unit", node_id = 2 : i64, obelisk_sim.vpi_definition_name = "$unit", sym_name = "s2"} {
    }
    obelisk.sv.symbol.instance attributes {hierarchical_name = "top", is_uninstantiated = false, name = "top", node_id = 3 : i64, referenced_path = "top", referenced_symbol = @s0.top, sym_name = "s3.top"} {
      obelisk.sv.symbol.instance_body attributes {hierarchical_name = "top", name = "top", node_id = 4 : i64, obelisk_sim.vpi_automatic = false, obelisk_sim.vpi_cell_instance = false, obelisk_sim.vpi_definition_name = "top", obelisk_sim.vpi_top = true, sym_name = "s4.top", time_precision_fs = 1000000 : i64, time_unit_fs = 1000000 : i64, vpi_scope_kind = 32 : i32} {
        obelisk.sv.symbol.variable attributes {hierarchical_name = "top.i", lifetime = 1 : i32, name = "i", node_id = 5 : i64, semantic_type = !obelisk.ranged_packed_array<7 : 0 x !obelisk.integral<1, false, true, 0 : 0, logic>>, sym_name = "s5.i"} {
          obelisk.sv.expression.conversion attributes {folded_constant = "8'd165", is_implicit = true, is_signed = false, node_id = 6 : i64, semantic_type = !obelisk.ranged_packed_array<7 : 0 x !obelisk.integral<1, false, true, 0 : 0, logic>>} {
            obelisk.sv.expression.integer_literal attributes {constant_value = "8'd165", is_signed = false, node_id = 7 : i64, semantic_type = !obelisk.ranged_packed_array<7 : 0 x !obelisk.integral<1, false, false, 0 : 0, bit>>} {
            }
          }
        }
        obelisk.sv.symbol.variable attributes {hierarchical_name = "top.o", lifetime = 1 : i32, name = "o", node_id = 8 : i64, semantic_type = !obelisk.ranged_packed_array<7 : 0 x !obelisk.integral<1, false, true, 0 : 0, logic>>, sym_name = "s6.o"} {
        }
        obelisk.sv.symbol.continuous_assign attributes {hierarchical_name = "top", node_id = 9 : i64, sym_name = "s7", time_precision_fs = 1000000 : i64, time_unit_fs = 1000000 : i64} {
          obelisk.sv.expression.assignment attributes {assignment_kind = 0 : i32, is_signed = false, node_id = 10 : i64, semantic_type = !obelisk.ranged_packed_array<7 : 0 x !obelisk.integral<1, false, true, 0 : 0, logic>>} {
            obelisk.sv.expression.named_value attributes {is_signed = false, node_id = 11 : i64, referenced_path = "top.o", referenced_symbol = @s1.$root::@s3.top::@s4.top::@s6.o, semantic_type = !obelisk.ranged_packed_array<7 : 0 x !obelisk.integral<1, false, true, 0 : 0, logic>>} {
            }
            obelisk.sv.expression.concatenation attributes {is_signed = false, node_id = 12 : i64, semantic_type = !obelisk.ranged_packed_array<7 : 0 x !obelisk.integral<1, false, true, 0 : 0, logic>>} {
              obelisk.sv.expression.replication attributes {is_signed = false, node_id = 13 : i64, semantic_type = !obelisk.void} {
                obelisk.sv.expression.integer_literal attributes {constant_value = "0", is_declared_unsized = true, is_signed = true, node_id = 14 : i64, semantic_type = !obelisk.integral<32, true, false, 31 : 0, int>} {
                }
                obelisk.sv.expression.concatenation attributes {is_signed = false, node_id = 15 : i64, semantic_type = !obelisk.ranged_packed_array<0 : 0 x !obelisk.integral<1, false, false, 0 : 0, bit>>} {
                  obelisk.sv.expression.integer_literal attributes {constant_value = "1'b0", is_signed = false, node_id = 16 : i64, semantic_type = !obelisk.ranged_packed_array<0 : 0 x !obelisk.integral<1, false, false, 0 : 0, bit>>} {
                  }
                }
              }
              obelisk.sv.expression.named_value attributes {is_signed = false, node_id = 17 : i64, referenced_path = "top.i", referenced_symbol = @s1.$root::@s3.top::@s4.top::@s5.i, semantic_type = !obelisk.ranged_packed_array<7 : 0 x !obelisk.integral<1, false, true, 0 : 0, logic>>} {
              }
            }
          }
        }
        obelisk.sv.symbol.procedural_block attributes {hierarchical_name = "top", node_id = 18 : i64, procedure_kind = 0 : i32, sym_name = "s8", time_precision_fs = 1000000 : i64, time_unit_fs = 1000000 : i64} {
          obelisk.sv.statement.timed attributes {node_id = 19 : i64} {
            obelisk.sv.timing.delay attributes {node_id = 20 : i64} {
              obelisk.sv.expression.integer_literal attributes {constant_value = "1", is_declared_unsized = true, is_signed = true, node_id = 21 : i64, semantic_type = !obelisk.integral<32, true, false, 31 : 0, int>} {
              }
            }
            obelisk.sv.statement.expression_statement attributes {node_id = 22 : i64} {
              obelisk.sv.expression.call attributes {argument_count = 2 : i64, callee_name = "$display", constraint_restrictions = [], defaulted_arguments = array<i64: 0, 0>, has_inline_constraints = false, has_iterator_expression = false, has_output_arguments = false, has_this_class = false, is_signed = false, is_super_class = false, is_system_call = true, node_id = 23 : i64, semantic_type = !obelisk.void, subroutine_kind = 1 : i32, system_library_cell = "work.top", system_scope_path = "top", system_scope_symbol = @s1.$root::@s3.top::@s4.top} {
                obelisk.sv.expression.string_literal attributes {constant_value = "%h", is_signed = false, node_id = 24 : i64, semantic_type = !obelisk.ranged_packed_array<15 : 0 x !obelisk.integral<1, false, false, 0 : 0, bit>>} {
                }
                obelisk.sv.expression.named_value attributes {is_signed = false, node_id = 25 : i64, referenced_path = "top.o", referenced_symbol = @s1.$root::@s3.top::@s4.top::@s6.o, semantic_type = !obelisk.ranged_packed_array<7 : 0 x !obelisk.integral<1, false, true, 0 : 0, logic>>} {
                }
              }
            }
          }
        }
      }
    }
  }
}


//--- stream.mlir
module attributes {obelisk.coverage.language_version = 2023 : i32} {
  obelisk.sv.symbol.definition attributes {definition_kind = 0 : i32, hierarchical_name = "top", name = "top", node_id = 0 : i64, sym_name = "s0.top"} {
  }
  obelisk.sv.symbol.root attributes {hierarchical_name = "\\$root ", name = "$root", node_id = 1 : i64, sym_name = "s1.$root"} {
    obelisk.sv.symbol.compilation_unit attributes {hierarchical_name = "$unit", node_id = 2 : i64, obelisk_sim.vpi_definition_name = "$unit", sym_name = "s2"} {
    }
    obelisk.sv.symbol.instance attributes {hierarchical_name = "top", is_uninstantiated = false, name = "top", node_id = 3 : i64, referenced_path = "top", referenced_symbol = @s0.top, sym_name = "s3.top"} {
      obelisk.sv.symbol.instance_body attributes {hierarchical_name = "top", name = "top", node_id = 4 : i64, obelisk_sim.vpi_automatic = false, obelisk_sim.vpi_cell_instance = false, obelisk_sim.vpi_definition_name = "top", obelisk_sim.vpi_top = true, sym_name = "s4.top", time_precision_fs = 1000000 : i64, time_unit_fs = 1000000 : i64, vpi_scope_kind = 32 : i32} {
        obelisk.sv.symbol.variable attributes {hierarchical_name = "top.i", lifetime = 1 : i32, name = "i", node_id = 5 : i64, semantic_type = !obelisk.ranged_packed_array<7 : 0 x !obelisk.integral<1, false, true, 0 : 0, logic>>, sym_name = "s5.i"} {
          obelisk.sv.expression.conversion attributes {folded_constant = "8'd165", is_implicit = true, is_signed = false, node_id = 6 : i64, semantic_type = !obelisk.ranged_packed_array<7 : 0 x !obelisk.integral<1, false, true, 0 : 0, logic>>} {
            obelisk.sv.expression.integer_literal attributes {constant_value = "8'd165", is_signed = false, node_id = 7 : i64, semantic_type = !obelisk.ranged_packed_array<7 : 0 x !obelisk.integral<1, false, false, 0 : 0, bit>>} {
            }
          }
        }
        obelisk.sv.symbol.variable attributes {hierarchical_name = "top.o", lifetime = 1 : i32, name = "o", node_id = 8 : i64, semantic_type = !obelisk.ranged_packed_array<7 : 0 x !obelisk.integral<1, false, true, 0 : 0, logic>>, sym_name = "s6.o"} {
        }
        obelisk.sv.symbol.continuous_assign attributes {hierarchical_name = "top", node_id = 9 : i64, sym_name = "s7", time_precision_fs = 1000000 : i64, time_unit_fs = 1000000 : i64} {
          obelisk.sv.expression.assignment attributes {assignment_kind = 0 : i32, is_signed = false, node_id = 10 : i64, semantic_type = !obelisk.ranged_packed_array<7 : 0 x !obelisk.integral<1, false, true, 0 : 0, logic>>} {
            obelisk.sv.expression.named_value attributes {is_signed = false, node_id = 11 : i64, referenced_path = "top.o", referenced_symbol = @s1.$root::@s3.top::@s4.top::@s6.o, semantic_type = !obelisk.ranged_packed_array<7 : 0 x !obelisk.integral<1, false, true, 0 : 0, logic>>} {
            }
            obelisk.sv.expression.conversion attributes {is_implicit = false, is_signed = false, node_id = 12 : i64, semantic_type = !obelisk.ranged_packed_array<7 : 0 x !obelisk.integral<1, false, true, 0 : 0, logic>>} {
              obelisk.sv.expression.streaming attributes {bitstream_width = 8 : i64, is_fixed_size = true, is_signed = false, node_id = 13 : i64, semantic_type = !obelisk.void, slice_size = 1 : i64, stream_count = 1 : i64, stream_with_flags = array<i64: 0>} {
                obelisk.sv.expression.named_value attributes {is_signed = false, node_id = 14 : i64, referenced_path = "top.i", referenced_symbol = @s1.$root::@s3.top::@s4.top::@s5.i, semantic_type = !obelisk.ranged_packed_array<7 : 0 x !obelisk.integral<1, false, true, 0 : 0, logic>>} {
                }
              }
            }
          }
        }
        obelisk.sv.symbol.procedural_block attributes {hierarchical_name = "top", node_id = 15 : i64, procedure_kind = 0 : i32, sym_name = "s8", time_precision_fs = 1000000 : i64, time_unit_fs = 1000000 : i64} {
          obelisk.sv.statement.timed attributes {node_id = 16 : i64} {
            obelisk.sv.timing.delay attributes {node_id = 17 : i64} {
              obelisk.sv.expression.integer_literal attributes {constant_value = "1", is_declared_unsized = true, is_signed = true, node_id = 18 : i64, semantic_type = !obelisk.integral<32, true, false, 31 : 0, int>} {
              }
            }
            obelisk.sv.statement.expression_statement attributes {node_id = 19 : i64} {
              obelisk.sv.expression.call attributes {argument_count = 2 : i64, callee_name = "$display", constraint_restrictions = [], defaulted_arguments = array<i64: 0, 0>, has_inline_constraints = false, has_iterator_expression = false, has_output_arguments = false, has_this_class = false, is_signed = false, is_super_class = false, is_system_call = true, node_id = 20 : i64, semantic_type = !obelisk.void, subroutine_kind = 1 : i32, system_library_cell = "work.top", system_scope_path = "top", system_scope_symbol = @s1.$root::@s3.top::@s4.top} {
                obelisk.sv.expression.string_literal attributes {constant_value = "%b", is_signed = false, node_id = 21 : i64, semantic_type = !obelisk.ranged_packed_array<15 : 0 x !obelisk.integral<1, false, false, 0 : 0, bit>>} {
                }
                obelisk.sv.expression.named_value attributes {is_signed = false, node_id = 22 : i64, referenced_path = "top.o", referenced_symbol = @s1.$root::@s3.top::@s4.top::@s6.o, semantic_type = !obelisk.ranged_packed_array<7 : 0 x !obelisk.integral<1, false, true, 0 : 0, logic>>} {
                }
              }
            }
          }
        }
      }
    }
  }
}
