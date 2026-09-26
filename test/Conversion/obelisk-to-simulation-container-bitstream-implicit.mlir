// RUN: obelisk-opt %s '--lower-obelisk-to-sim=opt-level=0' | FileCheck %s --check-prefix=LOWER

// Runtime behavior is checked in ../Runtime/obelisk-to-simulation-container-bitstream-implicit.test.

// A same-size element write must wake the implicit process that reads the
// whole container through a bit-stream cast.
// LOWER-DAG: obelisk_sim.managed.watch container_size
// LOWER-DAG: obelisk_sim.container.export_bitstream
// LOWER: obelisk_sim.suspend.any

!byte = !obelisk.ranged_packed_array<7 : 0 x !obelisk.integral<1, false, true, 0 : 0, logic>>
!bytes = !obelisk.dynarray<!byte>
!packed = !obelisk.ranged_packed_array<23 : 0 x !obelisk.integral<1, false, true, 0 : 0, logic>>

module attributes {
  llvm.data_layout = "e-m:e-p:64:64-i64:64-n8:16:32:64-S128",
  llvm.target_triple = "x86_64-unknown-linux-gnu"
} {
  obelisk.sv.symbol.definition attributes {definition_kind = 0 : i32,
      hierarchical_name = "top", name = "top", node_id = 0 : i64,
      sym_name = "top_def"} {
  }
  obelisk.sv.symbol.root attributes {hierarchical_name = "\\$root ",
      name = "$root", node_id = 1 : i64, sym_name = "root"} {
    obelisk.sv.symbol.instance attributes {hierarchical_name = "top",
        is_uninstantiated = false, name = "top", node_id = 2 : i64,
        referenced_path = "top", referenced_symbol = @top_def,
        sym_name = "top"} {
      obelisk.sv.symbol.instance_body attributes {hierarchical_name = "top",
          name = "top", node_id = 3 : i64, sym_name = "body",
          time_precision_fs = 1000000 : i64,
          time_unit_fs = 1000000 : i64} {
        obelisk.sv.symbol.variable attributes {hierarchical_name = "top.source",
            lifetime = 1 : i32, name = "source", node_id = 4 : i64,
            semantic_type = !bytes, sym_name = "source"} {
          obelisk.sv.expression.new_array attributes {is_signed = false,
              node_id = 5 : i64, semantic_type = !bytes} {
            obelisk.sv.expression.integer_literal attributes {
                constant_value = "3", is_declared_unsized = true,
                is_signed = true, node_id = 6 : i64,
                semantic_type = !obelisk.integral<32, true, false, 31 : 0, int>} {
            }
          }
        }
        obelisk.sv.symbol.variable attributes {
            hierarchical_name = "top.packed_value", lifetime = 1 : i32,
            name = "packed_value", node_id = 7 : i64,
            semantic_type = !packed, sym_name = "packed_value"} {
        }
        obelisk.sv.symbol.procedural_block attributes {
            hierarchical_name = "top", node_id = 8 : i64,
            procedure_kind = 3 : i32, sym_name = "always_comb"} {
          obelisk.sv.statement.expression_statement attributes {
              node_id = 9 : i64} {
            obelisk.sv.expression.assignment attributes {
                assignment_kind = 0 : i32, is_signed = false,
                node_id = 10 : i64, semantic_type = !packed} {
              obelisk.sv.expression.named_value attributes {
                  is_signed = false, node_id = 11 : i64,
                  referenced_path = "top.packed_value",
                  referenced_symbol = @root::@top::@body::@packed_value,
                  semantic_type = !packed} {
              }
              obelisk.sv.expression.conversion attributes {
                  is_implicit = false, is_signed = false,
                  node_id = 12 : i64, semantic_type = !packed} {
                obelisk.sv.expression.named_value attributes {
                    is_signed = false, node_id = 13 : i64,
                    referenced_path = "top.source",
                    referenced_symbol = @root::@top::@body::@source,
                    semantic_type = !bytes} {
                }
              }
            }
          }
        }
        obelisk.sv.symbol.procedural_block attributes {
            hierarchical_name = "top", node_id = 14 : i64,
            procedure_kind = 0 : i32, sym_name = "initial",
            time_precision_fs = 1000000 : i64,
            time_unit_fs = 1000000 : i64} {
          obelisk.sv.statement.block attributes {node_id = 15 : i64} {
            obelisk.sv.statement.list attributes {node_id = 16 : i64} {
              obelisk.sv.statement.timed attributes {node_id = 17 : i64} {
                obelisk.sv.timing.delay attributes {node_id = 18 : i64} {
                  obelisk.sv.expression.integer_literal attributes {
                      constant_value = "1", is_declared_unsized = true,
                      is_signed = true, node_id = 19 : i64,
                      semantic_type = !obelisk.integral<32, true, false, 31 : 0, int>} {
                  }
                }
                obelisk.sv.statement.empty attributes {node_id = 20 : i64} {
                }
              }
              obelisk.sv.statement.expression_statement attributes {
                  node_id = 21 : i64} {
                obelisk.sv.expression.assignment attributes {
                    assignment_kind = 0 : i32, is_signed = false,
                    node_id = 22 : i64, semantic_type = !byte} {
                  obelisk.sv.expression.element_select attributes {
                      is_signed = false, node_id = 23 : i64,
                      semantic_type = !byte} {
                    obelisk.sv.expression.named_value attributes {
                        is_signed = false, node_id = 24 : i64,
                        referenced_path = "top.source",
                        referenced_symbol = @root::@top::@body::@source,
                        semantic_type = !bytes} {
                    }
                    obelisk.sv.expression.integer_literal attributes {
                        constant_value = "1", is_declared_unsized = true,
                        is_signed = true, node_id = 25 : i64,
                        semantic_type = !obelisk.integral<32, true, false, 31 : 0, int>} {
                    }
                  }
                  obelisk.sv.expression.integer_literal attributes {
                      constant_value = "8'h22", is_signed = false,
                      node_id = 26 : i64, semantic_type = !byte} {
                  }
                }
              }
              obelisk.sv.statement.timed attributes {node_id = 27 : i64} {
                obelisk.sv.timing.delay attributes {node_id = 28 : i64} {
                  obelisk.sv.expression.integer_literal attributes {
                      constant_value = "1", is_declared_unsized = true,
                      is_signed = true, node_id = 29 : i64,
                      semantic_type = !obelisk.integral<32, true, false, 31 : 0, int>} {
                  }
                }
                obelisk.sv.statement.empty attributes {node_id = 30 : i64} {
                }
              }
              obelisk.sv.statement.expression_statement attributes {
                  node_id = 31 : i64} {
                obelisk.sv.expression.call attributes {argument_count = 2 : i64,
                    callee_name = "$display", constraint_restrictions = [],
                    defaulted_arguments = array<i64: 0, 0>,
                    has_inline_constraints = false,
                    has_iterator_expression = false,
                    has_output_arguments = false, has_this_class = false,
                    is_signed = false, is_super_class = false,
                    is_system_call = true, node_id = 32 : i64,
                    semantic_type = !obelisk.void, subroutine_kind = 1 : i32,
                    system_library_cell = "work.top", system_scope_path = "top",
                    system_scope_symbol = @root::@top::@body} {
                  obelisk.sv.expression.string_literal attributes {
                      constant_value = "%06h", is_signed = false,
                      node_id = 33 : i64,
                      semantic_type = !obelisk.ranged_packed_array<31 : 0 x !obelisk.integral<1, false, false, 0 : 0, bit>>} {
                  }
                  obelisk.sv.expression.named_value attributes {
                      is_signed = false, node_id = 34 : i64,
                      referenced_path = "top.packed_value",
                      referenced_symbol = @root::@top::@body::@packed_value,
                      semantic_type = !packed} {
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
