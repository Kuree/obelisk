// RUN: obelisk-opt %s '--lower-obelisk-to-sim=opt-level=0' | FileCheck %s

module {
  obelisk.sv.symbol.definition attributes {definition_kind = 0 : i32, hierarchical_name = "unsupported_foreach", name = "unsupported_foreach", node_id = 0 : i64, sym_name = "s0.unsupported_foreach"} {
  }
  obelisk.sv.symbol.root attributes {hierarchical_name = "\\$root ", name = "$root", node_id = 1 : i64, sym_name = "s1.$root"} {
    obelisk.sv.symbol.compilation_unit attributes {hierarchical_name = "$unit", node_id = 2 : i64, sym_name = "s2"} {
    }
    obelisk.sv.symbol.instance attributes {hierarchical_name = "unsupported_foreach", is_uninstantiated = false, name = "unsupported_foreach", node_id = 3 : i64, referenced_path = "unsupported_foreach", referenced_symbol = @s0.unsupported_foreach, sym_name = "s3.unsupported_foreach"} {
      obelisk.sv.symbol.instance_body attributes {hierarchical_name = "unsupported_foreach", name = "unsupported_foreach", node_id = 4 : i64, sym_name = "s4.unsupported_foreach"} {
        obelisk.sv.symbol.variable attributes {hierarchical_name = "unsupported_foreach.values", lifetime = 1 : i32, name = "values", node_id = 5 : i64, semantic_type = !obelisk.dynarray<!obelisk.integral<32, true, false, 31 : 0, int>>, sym_name = "s5.values"} {
        }
        obelisk.sv.symbol.statement_block attributes {block_kind = 0 : i32, hierarchical_name = "unsupported_foreach", node_id = 6 : i64, sym_name = "s6"} {
          obelisk.sv.symbol.iterator attributes {array_type = !obelisk.dynarray<!obelisk.integral<32, true, false, 31 : 0, int>>, hierarchical_name = "unsupported_foreach.index", index_method_name = "", is_const, name = "index", node_id = 7 : i64, semantic_type = !obelisk.integral<32, true, false, 31 : 0, int>, sym_name = "s7.index"} {
          }
        }
        obelisk.sv.symbol.procedural_block attributes {hierarchical_name = "unsupported_foreach", node_id = 8 : i64, procedure_kind = 0 : i32, sym_name = "s8", time_precision_fs = 1000000 : i64, time_unit_fs = 1000000 : i64} {
          obelisk.sv.statement.block attributes {node_id = 9 : i64} {
            obelisk.sv.statement.foreach_loop attributes {loop_dimensions = [{has_iterator = true, has_static_range = false, iterator_path = "unsupported_foreach.index", iterator_symbol = @s1.$root::@s4.unsupported_foreach::@s6::@s7.index, iterator_type = !obelisk.integral<32, true, false, 31 : 0, int>}], node_id = 10 : i64} {
              obelisk.sv.expression.named_value attributes {node_id = 11 : i64, referenced_path = "unsupported_foreach.values", referenced_symbol = @s1.$root::@s3.unsupported_foreach::@s4.unsupported_foreach::@s5.values, semantic_type = !obelisk.dynarray<!obelisk.integral<32, true, false, 31 : 0, int>>} {
              }
              obelisk.sv.statement.expression_statement attributes {node_id = 12 : i64} {
                obelisk.sv.expression.assignment attributes {assignment_kind = 0 : i32, node_id = 13 : i64, semantic_type = !obelisk.integral<32, true, false, 31 : 0, int>} {
                  obelisk.sv.expression.element_select attributes {node_id = 14 : i64, semantic_type = !obelisk.integral<32, true, false, 31 : 0, int>} {
                    obelisk.sv.expression.named_value attributes {node_id = 15 : i64, referenced_path = "unsupported_foreach.values", referenced_symbol = @s1.$root::@s3.unsupported_foreach::@s4.unsupported_foreach::@s5.values, semantic_type = !obelisk.dynarray<!obelisk.integral<32, true, false, 31 : 0, int>>} {
                    }
                    obelisk.sv.expression.named_value attributes {node_id = 16 : i64, referenced_path = "unsupported_foreach.index", referenced_symbol = @s1.$root::@s3.unsupported_foreach::@s4.unsupported_foreach::@s6::@s7.index, semantic_type = !obelisk.integral<32, true, false, 31 : 0, int>} {
                    }
                  }
                  obelisk.sv.expression.named_value attributes {node_id = 17 : i64, referenced_path = "unsupported_foreach.index", referenced_symbol = @s1.$root::@s3.unsupported_foreach::@s4.unsupported_foreach::@s6::@s7.index, semantic_type = !obelisk.integral<32, true, false, 31 : 0, int>} {
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

// CHECK: obelisk_sim.container.size
// CHECK: cf.cond_br
// CHECK: obelisk_sim.container.write
// CHECK-NOT: obelisk.sv.
