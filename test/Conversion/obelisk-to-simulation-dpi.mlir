// RUN: obelisk-opt %s '--lower-obelisk-to-sim=opt-level=0' | FileCheck %s

module {
  obelisk.sv.symbol.definition attributes {definition_kind = 0 : i32, hierarchical_name = "dpi_imports", name = "dpi_imports", node_id = 0 : i64, sym_name = "s0.dpi_imports"} {
  }
  obelisk.sv.symbol.root attributes {hierarchical_name = "\\$root ", name = "$root", node_id = 1 : i64, sym_name = "s1.$root"} {
    obelisk.sv.symbol.compilation_unit attributes {hierarchical_name = "$unit", node_id = 2 : i64, sym_name = "s2"} {
    }
    obelisk.sv.symbol.instance attributes {hierarchical_name = "dpi_imports", is_uninstantiated = false, name = "dpi_imports", node_id = 3 : i64, referenced_path = "dpi_imports", referenced_symbol = @s0.dpi_imports, sym_name = "s3.dpi_imports"} {
      obelisk.sv.symbol.instance_body attributes {hierarchical_name = "dpi_imports", name = "dpi_imports", node_id = 4 : i64, sym_name = "s4.dpi_imports"} {
        obelisk.sv.symbol.subroutine attributes {dpi_c_identifier = "c_add", hierarchical_name = "dpi_imports.sv_add", is_dpi_import, is_pure, name = "sv_add", node_id = 5 : i64, semantic_type = !obelisk.subroutine<(!obelisk.integral<32, true, false, 31 : 0, int>) -> !obelisk.integral<32, true, false, 31 : 0, int>, false>, subroutine_kind = 0 : i32, sym_name = "s5.sv_add", time_precision_fs = 1000000 : i64, time_unit_fs = 1000000 : i64} {
          obelisk.sv.statement.list attributes {node_id = 6 : i64} {
          }
          obelisk.sv.symbol.formal_argument attributes {direction = 0 : i32, hierarchical_name = "dpi_imports.sv_add.value", name = "value", node_id = 7 : i64, semantic_type = !obelisk.integral<32, true, false, 31 : 0, int>, sym_name = "s6.value"} {
          }
        }
        obelisk.sv.symbol.subroutine attributes {dpi_c_identifier = "update", hierarchical_name = "dpi_imports.update", is_dpi_context, is_dpi_import, name = "update", node_id = 8 : i64, semantic_type = !obelisk.subroutine<(!obelisk.ranged_packed_array<64 : 0 x !obelisk.integral<1, false, true, 0 : 0, logic>>, !obelisk.ranged_packed_array<32 : 0 x !obelisk.integral<1, false, false, 0 : 0, bit>>) -> (), true>, subroutine_kind = 1 : i32, sym_name = "s7.update", time_precision_fs = 1000000 : i64, time_unit_fs = 1000000 : i64} {
          obelisk.sv.statement.list attributes {node_id = 9 : i64} {
          }
          obelisk.sv.symbol.formal_argument attributes {direction = 0 : i32, hierarchical_name = "dpi_imports.update.source", name = "source", node_id = 10 : i64, semantic_type = !obelisk.ranged_packed_array<64 : 0 x !obelisk.integral<1, false, true, 0 : 0, logic>>, sym_name = "s8.source"} {
          }
          obelisk.sv.symbol.formal_argument attributes {direction = 2 : i32, hierarchical_name = "dpi_imports.update.destination", name = "destination", node_id = 11 : i64, semantic_type = !obelisk.ranged_packed_array<32 : 0 x !obelisk.integral<1, false, false, 0 : 0, bit>>, sym_name = "s9.destination"} {
          }
        }
        obelisk.sv.symbol.subroutine attributes {dpi_c_identifier = "notify", hierarchical_name = "dpi_imports.notify", is_dpi_import, name = "notify", node_id = 100 : i64, semantic_type = !obelisk.subroutine<(!obelisk.integral<32, true, false, 31 : 0, int>) -> !obelisk.void, false>, subroutine_kind = 0 : i32, sym_name = "s100.notify", time_precision_fs = 1000000 : i64, time_unit_fs = 1000000 : i64} {
          obelisk.sv.statement.list attributes {node_id = 101 : i64} {
          }
          obelisk.sv.symbol.formal_argument attributes {direction = 0 : i32, hierarchical_name = "dpi_imports.notify.value", name = "value", node_id = 102 : i64, semantic_type = !obelisk.integral<32, true, false, 31 : 0, int>, sym_name = "s101.value"} {
          }
        }
        obelisk.sv.symbol.variable attributes {hierarchical_name = "dpi_imports.result", lifetime = 1 : i32, name = "result", node_id = 12 : i64, semantic_type = !obelisk.integral<32, true, false, 31 : 0, int>, sym_name = "s10.result"} {
        }
        obelisk.sv.symbol.variable attributes {hierarchical_name = "dpi_imports.source", lifetime = 1 : i32, name = "source", node_id = 13 : i64, semantic_type = !obelisk.ranged_packed_array<64 : 0 x !obelisk.integral<1, false, true, 0 : 0, logic>>, sym_name = "s11.source"} {
        }
        obelisk.sv.symbol.variable attributes {hierarchical_name = "dpi_imports.destination", lifetime = 1 : i32, name = "destination", node_id = 14 : i64, semantic_type = !obelisk.ranged_packed_array<32 : 0 x !obelisk.integral<1, false, false, 0 : 0, bit>>, sym_name = "s12.destination"} {
        }
        obelisk.sv.symbol.procedural_block attributes {hierarchical_name = "dpi_imports", node_id = 15 : i64, procedure_kind = 0 : i32, sym_name = "s13", time_precision_fs = 1000000 : i64, time_unit_fs = 1000000 : i64} {
          obelisk.sv.statement.block attributes {node_id = 16 : i64} {
            obelisk.sv.statement.list attributes {node_id = 17 : i64} {
              obelisk.sv.statement.expression_statement attributes {node_id = 18 : i64} {
                obelisk.sv.expression.assignment attributes {assignment_kind = 0 : i32, node_id = 19 : i64, semantic_type = !obelisk.integral<32, true, false, 31 : 0, int>} {
                  obelisk.sv.expression.named_value attributes {node_id = 20 : i64, referenced_path = "dpi_imports.result", referenced_symbol = @s1.$root::@s3.dpi_imports::@s4.dpi_imports::@s10.result, semantic_type = !obelisk.integral<32, true, false, 31 : 0, int>} {
                  }
                  obelisk.sv.expression.call attributes {argument_count = 1 : i64, callee_name = "sv_add", constraint_restrictions = [], defaulted_arguments = array<i64: 0>, has_inline_constraints = false, has_iterator_expression = false, has_output_arguments = false, has_this_class = false, is_super_class = false, is_system_call = false, node_id = 21 : i64, referenced_path = "dpi_imports.sv_add", referenced_symbol = @s1.$root::@s3.dpi_imports::@s4.dpi_imports::@s5.sv_add, semantic_type = !obelisk.integral<32, true, false, 31 : 0, int>, subroutine_kind = 0 : i32} {
                    obelisk.sv.expression.integer_literal attributes {constant_value = "7", node_id = 22 : i64, semantic_type = !obelisk.integral<32, true, false, 31 : 0, int>} {
                    }
                  }
                }
              }
              obelisk.sv.statement.expression_statement attributes {node_id = 23 : i64} {
                obelisk.sv.expression.call attributes {argument_count = 2 : i64, callee_name = "update", constraint_restrictions = [], defaulted_arguments = array<i64: 0, 0>, has_inline_constraints = false, has_iterator_expression = false, has_output_arguments = true, has_this_class = false, is_super_class = false, is_system_call = false, node_id = 24 : i64, referenced_path = "dpi_imports.update", referenced_symbol = @s1.$root::@s3.dpi_imports::@s4.dpi_imports::@s7.update, semantic_type = !obelisk.void, subroutine_kind = 1 : i32} {
                  obelisk.sv.expression.named_value attributes {node_id = 25 : i64, referenced_path = "dpi_imports.source", referenced_symbol = @s1.$root::@s3.dpi_imports::@s4.dpi_imports::@s11.source, semantic_type = !obelisk.ranged_packed_array<64 : 0 x !obelisk.integral<1, false, true, 0 : 0, logic>>} {
                  }
                  obelisk.sv.expression.assignment attributes {assignment_kind = 0 : i32, node_id = 26 : i64, semantic_type = !obelisk.ranged_packed_array<32 : 0 x !obelisk.integral<1, false, false, 0 : 0, bit>>} {
                    obelisk.sv.expression.named_value attributes {node_id = 27 : i64, referenced_path = "dpi_imports.destination", referenced_symbol = @s1.$root::@s3.dpi_imports::@s4.dpi_imports::@s12.destination, semantic_type = !obelisk.ranged_packed_array<32 : 0 x !obelisk.integral<1, false, false, 0 : 0, bit>>} {
                    }
                    obelisk.sv.expression.empty_argument attributes {node_id = 28 : i64, semantic_type = !obelisk.ranged_packed_array<32 : 0 x !obelisk.integral<1, false, false, 0 : 0, bit>>} {
                    }
                  }
                }
              }
              obelisk.sv.statement.expression_statement attributes {node_id = 103 : i64} {
                obelisk.sv.expression.call attributes {argument_count = 1 : i64, callee_name = "notify", constraint_restrictions = [], defaulted_arguments = array<i64: 0>, has_inline_constraints = false, has_iterator_expression = false, has_output_arguments = false, has_this_class = false, is_super_class = false, is_system_call = false, node_id = 104 : i64, referenced_path = "dpi_imports.notify", referenced_symbol = @s1.$root::@s3.dpi_imports::@s4.dpi_imports::@s100.notify, semantic_type = !obelisk.void, subroutine_kind = 0 : i32} {
                  obelisk.sv.expression.integer_literal attributes {constant_value = "9", node_id = 105 : i64, semantic_type = !obelisk.integral<32, true, false, 31 : 0, int>} {
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

// CHECK: obelisk_sim.dpi.call "c_add" id {{-?[0-9]+}} scope 1
// CHECK-SAME: context
// CHECK-SAME: kind = int
// CHECK-SAME: direction = result
// CHECK-SAME: is_pure = true
// CHECK-NEXT: obelisk_sim.status.check
// CHECK: obelisk_sim.dpi.call "update" id {{-?[0-9]+}} scope 1
// CHECK-SAME: kind = logic_vector
// CHECK-SAME: width = 65
// CHECK-SAME: kind = bit_vector
// CHECK-SAME: direction = inout
// CHECK-SAME: width = 33
// CHECK-SAME: is_context = true
// CHECK-SAME: is_task = true
// CHECK-NEXT: obelisk_sim.status.check
// CHECK: obelisk_sim.dpi.call "notify" id {{-?[0-9]+}} scope 1
// CHECK-SAME: kind = int
// CHECK-SAME: direction = input
// CHECK-NOT: direction = result
// CHECK-NEXT: obelisk_sim.status.check
// CHECK-NOT: obelisk.sv.
