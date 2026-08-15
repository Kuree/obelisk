// RUN: obelisk-opt %s '--lower-obelisk-to-sim=opt-level=0' | FileCheck %s
// This fixture also pins Slang's validated compatibility extension: a bare
// filename and multiple module selections in one call.

module {
  obelisk.sv.symbol.definition attributes {
    definition_kind = 0 : i32, hierarchical_name = "leaf", name = "leaf",
    node_id = 0 : i64, sym_name = "s0.leaf"
  } {
  }
  obelisk.sv.symbol.definition attributes {
    definition_kind = 0 : i32, hierarchical_name = "top", name = "top",
    node_id = 1 : i64, sym_name = "s1.top"
  } {
  }
  obelisk.sv.symbol.root attributes {
    hierarchical_name = "\\$root ", name = "$root", node_id = 2 : i64,
    sym_name = "s2.$root"
  } {
    obelisk.sv.symbol.compilation_unit attributes {
      hierarchical_name = "$unit", node_id = 3 : i64, sym_name = "s3"
    } {
    }
    obelisk.sv.symbol.instance attributes {
      hierarchical_name = "top", is_uninstantiated = false, name = "top",
      node_id = 4 : i64, referenced_path = "top",
      referenced_symbol = @s1.top, sym_name = "s4.top"
    } {
      obelisk.sv.symbol.instance_body attributes {
        hierarchical_name = "top", name = "top", node_id = 5 : i64,
        sym_name = "s5.top"
      } {
        obelisk.sv.symbol.instance attributes {
          hierarchical_name = "top.a", is_uninstantiated = false, name = "a",
          node_id = 6 : i64, referenced_path = "leaf",
          referenced_symbol = @s0.leaf, sym_name = "s6.a"
        } {
          obelisk.sv.symbol.instance_body attributes {
            hierarchical_name = "top.a", name = "leaf", node_id = 7 : i64,
            sym_name = "s7.leaf"
          } {
          }
        }
        obelisk.sv.symbol.instance attributes {
          hierarchical_name = "top.b", is_uninstantiated = false, name = "b",
          node_id = 8 : i64, referenced_path = "leaf",
          referenced_symbol = @s0.leaf, sym_name = "s8.b"
        } {
          obelisk.sv.symbol.instance_body attributes {
            hierarchical_name = "top.b", name = "leaf", node_id = 9 : i64,
            sym_name = "s9.leaf"
          } {
          }
        }
        obelisk.sv.symbol.procedural_block attributes {
          hierarchical_name = "top", node_id = 10 : i64,
          procedure_kind = 0 : i32, sym_name = "s10",
          time_precision_fs = 1000000 : i64,
          time_unit_fs = 1000000 : i64
        } {
          obelisk.sv.statement.expression_statement attributes {node_id = 11 : i64} {
            obelisk.sv.expression.call attributes {
              argument_count = 0 : i64, callee_name = "$dumpports",
              constraint_restrictions = [], has_inline_constraints = false,
              has_iterator_expression = false, has_output_arguments = false,
              has_this_class = false, is_super_class = false,
              is_system_call = true, node_id = 12 : i64,
              semantic_type = !obelisk.void, subroutine_kind = 1 : i32,
              system_library_cell = "work.top", system_scope_path = "top",
              system_scope_symbol = @s2.$root::@s4.top::@s5.top
            } {
            }
          }
          obelisk.sv.statement.expression_statement attributes {node_id = 13 : i64} {
            obelisk.sv.expression.call attributes {
              argument_count = 1 : i64, callee_name = "$dumpports",
              constraint_restrictions = [], has_inline_constraints = false,
              has_iterator_expression = false, has_output_arguments = false,
              has_this_class = false, is_super_class = false,
              is_system_call = true, node_id = 14 : i64,
              semantic_type = !obelisk.void, subroutine_kind = 1 : i32,
              system_library_cell = "work.top", system_scope_path = "top",
              system_scope_symbol = @s2.$root::@s4.top::@s5.top
            } {
              obelisk.sv.expression.empty_argument attributes {
                node_id = 15 : i64, semantic_type = !obelisk.void
              } {
              }
            }
          }
          obelisk.sv.statement.expression_statement attributes {node_id = 16 : i64} {
            obelisk.sv.expression.call attributes {
              argument_count = 1 : i64, callee_name = "$dumpports",
              constraint_restrictions = [], has_inline_constraints = false,
              has_iterator_expression = false, has_output_arguments = false,
              has_this_class = false, is_super_class = false,
              is_system_call = true, node_id = 17 : i64,
              semantic_type = !obelisk.void, subroutine_kind = 1 : i32,
              system_library_cell = "work.top", system_scope_path = "top",
              system_scope_symbol = @s2.$root::@s4.top::@s5.top
            } {
              obelisk.sv.expression.conversion attributes {
                node_id = 18 : i64, semantic_type = !obelisk.string
              } {
                obelisk.sv.expression.string_literal attributes {
                  constant_value = "default.evcd", node_id = 19 : i64,
                  semantic_type = !obelisk.ranged_packed_array<95 : 0 x !obelisk.integral<1, false, false, 0 : 0, bit>>
                } {
                }
              }
            }
          }
          obelisk.sv.statement.expression_statement attributes {node_id = 20 : i64} {
            obelisk.sv.expression.call attributes {
              argument_count = 2 : i64, callee_name = "$dumpports",
              constraint_restrictions = [], has_inline_constraints = false,
              has_iterator_expression = false, has_output_arguments = false,
              has_this_class = false, is_super_class = false,
              is_system_call = true, node_id = 21 : i64,
              semantic_type = !obelisk.void, subroutine_kind = 1 : i32,
              system_library_cell = "work.top", system_scope_path = "top",
              system_scope_symbol = @s2.$root::@s4.top::@s5.top
            } {
              obelisk.sv.expression.empty_argument attributes {
                node_id = 22 : i64, semantic_type = !obelisk.void
              } {
              }
              obelisk.sv.expression.conversion attributes {
                node_id = 23 : i64, semantic_type = !obelisk.string
              } {
                obelisk.sv.expression.string_literal attributes {
                  constant_value = "omitted.evcd", node_id = 24 : i64,
                  semantic_type = !obelisk.ranged_packed_array<95 : 0 x !obelisk.integral<1, false, false, 0 : 0, bit>>
                } {
                }
              }
            }
          }
          obelisk.sv.statement.expression_statement attributes {node_id = 25 : i64} {
            obelisk.sv.expression.call attributes {
              argument_count = 2 : i64, callee_name = "$dumpports",
              constraint_restrictions = [], has_inline_constraints = false,
              has_iterator_expression = false, has_output_arguments = false,
              has_this_class = false, is_super_class = false,
              is_system_call = true, node_id = 26 : i64,
              semantic_type = !obelisk.void, subroutine_kind = 1 : i32,
              system_library_cell = "work.top", system_scope_path = "top",
              system_scope_symbol = @s2.$root::@s4.top::@s5.top
            } {
              obelisk.sv.expression.arbitrary_symbol attributes {
                node_id = 27 : i64, referenced_path = "top.a",
                referenced_symbol = @s2.$root::@s4.top::@s5.top::@s6.a,
                semantic_type = !obelisk.void
              } {
              }
              obelisk.sv.expression.arbitrary_symbol attributes {
                node_id = 28 : i64, referenced_path = "top.b",
                referenced_symbol = @s2.$root::@s4.top::@s5.top::@s8.b,
                semantic_type = !obelisk.void
              } {
              }
            }
          }
          obelisk.sv.statement.expression_statement attributes {node_id = 29 : i64} {
            obelisk.sv.expression.call attributes {
              argument_count = 3 : i64, callee_name = "$dumpports",
              constraint_restrictions = [], has_inline_constraints = false,
              has_iterator_expression = false, has_output_arguments = false,
              has_this_class = false, is_super_class = false,
              is_system_call = true, node_id = 30 : i64,
              semantic_type = !obelisk.void, subroutine_kind = 1 : i32,
              system_library_cell = "work.top", system_scope_path = "top",
              system_scope_symbol = @s2.$root::@s4.top::@s5.top
            } {
              obelisk.sv.expression.arbitrary_symbol attributes {
                node_id = 31 : i64, referenced_path = "top.a",
                referenced_symbol = @s2.$root::@s4.top::@s5.top::@s6.a,
                semantic_type = !obelisk.void
              } {
              }
              obelisk.sv.expression.arbitrary_symbol attributes {
                node_id = 32 : i64, referenced_path = "top.b",
                referenced_symbol = @s2.$root::@s4.top::@s5.top::@s8.b,
                semantic_type = !obelisk.void
              } {
              }
              obelisk.sv.expression.conversion attributes {
                node_id = 33 : i64, semantic_type = !obelisk.string
              } {
                obelisk.sv.expression.string_literal attributes {
                  constant_value = "multi.evcd", node_id = 34 : i64,
                  semantic_type = !obelisk.ranged_packed_array<79 : 0 x !obelisk.integral<1, false, false, 0 : 0, bit>>
                } {
                }
              }
            }
          }
          obelisk.sv.statement.expression_statement attributes {node_id = 35 : i64} {
            obelisk.sv.expression.call attributes {
              argument_count = 0 : i64, callee_name = "$dumpportsoff",
              constraint_restrictions = [], has_inline_constraints = false,
              has_iterator_expression = false, has_output_arguments = false,
              has_this_class = false, is_super_class = false,
              is_system_call = true, node_id = 36 : i64,
              semantic_type = !obelisk.void, subroutine_kind = 1 : i32,
              system_library_cell = "work.top", system_scope_path = "top",
              system_scope_symbol = @s2.$root::@s4.top::@s5.top
            } {
            }
          }
          obelisk.sv.statement.expression_statement attributes {node_id = 37 : i64} {
            obelisk.sv.expression.call attributes {
              argument_count = 1 : i64, callee_name = "$dumpportson",
              constraint_restrictions = [], has_inline_constraints = false,
              has_iterator_expression = false, has_output_arguments = false,
              has_this_class = false, is_super_class = false,
              is_system_call = true, node_id = 38 : i64,
              semantic_type = !obelisk.void, subroutine_kind = 1 : i32,
              system_library_cell = "work.top", system_scope_path = "top",
              system_scope_symbol = @s2.$root::@s4.top::@s5.top
            } {
              obelisk.sv.expression.conversion attributes {
                node_id = 39 : i64, semantic_type = !obelisk.string
              } {
                obelisk.sv.expression.string_literal attributes {
                  constant_value = "named.evcd", node_id = 40 : i64,
                  semantic_type = !obelisk.ranged_packed_array<79 : 0 x !obelisk.integral<1, false, false, 0 : 0, bit>>
                } {
                }
              }
            }
          }
          obelisk.sv.statement.expression_statement attributes {node_id = 41 : i64} {
            obelisk.sv.expression.call attributes {
              argument_count = 2 : i64, callee_name = "$dumpportslimit",
              constraint_restrictions = [], has_inline_constraints = false,
              has_iterator_expression = false, has_output_arguments = false,
              has_this_class = false, is_super_class = false,
              is_system_call = true, node_id = 42 : i64,
              semantic_type = !obelisk.void, subroutine_kind = 1 : i32,
              system_library_cell = "work.top", system_scope_path = "top",
              system_scope_symbol = @s2.$root::@s4.top::@s5.top
            } {
              obelisk.sv.expression.integer_literal attributes {
                constant_value = "64", is_signed = true, node_id = 43 : i64,
                semantic_type = !obelisk.integral<32, true, false, 31 : 0, int>
              } {
              }
              obelisk.sv.expression.conversion attributes {
                node_id = 44 : i64, semantic_type = !obelisk.string
              } {
                obelisk.sv.expression.string_literal attributes {
                  constant_value = "named.evcd", node_id = 45 : i64,
                  semantic_type = !obelisk.ranged_packed_array<79 : 0 x !obelisk.integral<1, false, false, 0 : 0, bit>>
                } {
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
// CHECK: %[[DEFAULT0:.*]] = obelisk_sim.string.literal "dumpports.vcd"
// CHECK: %[[TOP0:.*]] = obelisk_sim.string.literal "top"
// CHECK: obelisk_sim.dump.ports {{.*}}, %[[DEFAULT0]], %[[TOP0]],
// CHECK: %[[DEFAULT1:.*]] = obelisk_sim.string.literal "dumpports.vcd"
// CHECK: %[[TOP1:.*]] = obelisk_sim.string.literal "top"
// CHECK: obelisk_sim.dump.ports {{.*}}, %[[DEFAULT1]], %[[TOP1]],
// CHECK: %[[NAMED0:.*]] = obelisk_sim.string.literal "default.evcd"
// CHECK: %[[TOP2:.*]] = obelisk_sim.string.literal "top"
// CHECK: obelisk_sim.dump.ports {{.*}}, %[[NAMED0]], %[[TOP2]],
// CHECK: %[[NAMED1:.*]] = obelisk_sim.string.literal "omitted.evcd"
// CHECK: %[[TOP3:.*]] = obelisk_sim.string.literal "top"
// CHECK: obelisk_sim.dump.ports {{.*}}, %[[NAMED1]], %[[TOP3]],
// CHECK: %[[DEFAULT2:.*]] = obelisk_sim.string.literal "dumpports.vcd"
// CHECK: %[[A0:.*]] = obelisk_sim.string.literal "top.a"
// CHECK: obelisk_sim.dump.ports {{.*}}, %[[DEFAULT2]], %[[A0]],
// CHECK: %[[B0:.*]] = obelisk_sim.string.literal "top.b"
// CHECK: obelisk_sim.dump.ports {{.*}}, %[[DEFAULT2]], %[[B0]],
// CHECK: %[[NAMED2:.*]] = obelisk_sim.string.literal "multi.evcd"
// CHECK: %[[A1:.*]] = obelisk_sim.string.literal "top.a"
// CHECK: obelisk_sim.dump.ports {{.*}}, %[[NAMED2]], %[[A1]],
// CHECK: %[[B1:.*]] = obelisk_sim.string.literal "top.b"
// CHECK: obelisk_sim.dump.ports {{.*}}, %[[NAMED2]], %[[B1]],
// CHECK: %[[ALL:.*]] = obelisk_sim.string.literal ""
// CHECK: obelisk_sim.dump.ports_control {{.*}}, %[[ALL]], {{.*}} {action = 0 : i32}
// CHECK: %[[CONTROL:.*]] = obelisk_sim.string.literal "named.evcd"
// CHECK: obelisk_sim.dump.ports_control {{.*}}, %[[CONTROL]], {{.*}} {action = 1 : i32}
// CHECK: %[[LIMIT_PATH:.*]] = obelisk_sim.string.literal "named.evcd"
// CHECK: obelisk_sim.dump.ports_control {{.*}}, %[[LIMIT_PATH]], {{.*}} {action = 4 : i32}
