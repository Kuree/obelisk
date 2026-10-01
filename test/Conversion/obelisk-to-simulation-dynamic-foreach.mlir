// RUN: obelisk-opt %s '--lower-obelisk-to-sim=opt-level=0' | FileCheck %s

module {
  obelisk.sv.symbol.definition @s0.unsupported_foreach attributes {definition_kind = 0 : i32, hierarchical_name = "unsupported_foreach", name = "unsupported_foreach", node_id = 0 : i64} {
  }
  obelisk.sv.symbol.root @s1.$root attributes {hierarchical_name = "\\$root ", name = "$root", node_id = 1 : i64} {
    obelisk.sv.symbol.compilation_unit @s2 attributes {hierarchical_name = "$unit", node_id = 2 : i64} {
    }
    obelisk.sv.symbol.instance @s3.unsupported_foreach attributes {hierarchical_name = "unsupported_foreach", is_uninstantiated = false, name = "unsupported_foreach", node_id = 3 : i64, referenced_path = "unsupported_foreach", referenced_symbol = @s0.unsupported_foreach} {
      obelisk.sv.symbol.instance_body @s4.unsupported_foreach attributes {hierarchical_name = "unsupported_foreach", name = "unsupported_foreach", node_id = 4 : i64} {
        obelisk.sv.symbol.variable @s5.values attributes {hierarchical_name = "unsupported_foreach.values", lifetime = 1 : i32, name = "values", node_id = 5 : i64, semantic_type = !obelisk.dynarray<!obelisk.integral<32, true, false, 31 : 0, int>>} {
        }
        obelisk.sv.symbol.variable @s18.assoc_values attributes {hierarchical_name = "unsupported_foreach.assoc_values", lifetime = 1 : i32, name = "assoc_values", node_id = 18 : i64, semantic_type = !obelisk.assoc<!obelisk.integral<32, true, false, 31 : 0, int>, !obelisk.integral<32, true, false, 31 : 0, int>, false>} {
        }
        obelisk.sv.symbol.statement_block @s6 attributes {block_kind = 0 : i32, hierarchical_name = "unsupported_foreach", node_id = 6 : i64} {
          obelisk.sv.symbol.iterator @s7.index attributes {array_type = !obelisk.dynarray<!obelisk.integral<32, true, false, 31 : 0, int>>, hierarchical_name = "unsupported_foreach.index", index_method_name = "", is_const, name = "index", node_id = 7 : i64, semantic_type = !obelisk.integral<32, true, false, 31 : 0, int>} {
          }
        }
        obelisk.sv.symbol.statement_block @s19 attributes {block_kind = 0 : i32, hierarchical_name = "unsupported_foreach", node_id = 19 : i64} {
          obelisk.sv.symbol.iterator @s20.assoc_index attributes {array_type = !obelisk.assoc<!obelisk.integral<32, true, false, 31 : 0, int>, !obelisk.integral<32, true, false, 31 : 0, int>, false>, hierarchical_name = "unsupported_foreach.assoc_index", index_method_name = "", is_const, name = "assoc_index", node_id = 20 : i64, semantic_type = !obelisk.integral<32, true, false, 31 : 0, int>} {
          }
        }
        obelisk.sv.symbol.procedural_block @s8 attributes {hierarchical_name = "unsupported_foreach", node_id = 8 : i64, procedure_kind = 0 : i32, time_precision_fs = 1000000 : i64, time_unit_fs = 1000000 : i64} {
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
        obelisk.sv.symbol.procedural_block @s21 attributes {hierarchical_name = "unsupported_foreach", node_id = 21 : i64, procedure_kind = 0 : i32, time_precision_fs = 1000000 : i64, time_unit_fs = 1000000 : i64} {
          obelisk.sv.statement.block attributes {node_id = 22 : i64} {
            obelisk.sv.statement.foreach_loop attributes {loop_dimensions = [{has_iterator = true, has_static_range = false, iterator_path = "unsupported_foreach.assoc_index", iterator_symbol = @s1.$root::@s4.unsupported_foreach::@s19::@s20.assoc_index, iterator_type = !obelisk.integral<32, true, false, 31 : 0, int>}], node_id = 23 : i64} {
              obelisk.sv.expression.named_value attributes {node_id = 24 : i64, referenced_path = "unsupported_foreach.assoc_values", referenced_symbol = @s1.$root::@s3.unsupported_foreach::@s4.unsupported_foreach::@s18.assoc_values, semantic_type = !obelisk.assoc<!obelisk.integral<32, true, false, 31 : 0, int>, !obelisk.integral<32, true, false, 31 : 0, int>, false>} {
              }
              obelisk.sv.statement.continue attributes {node_id = 25 : i64} {
              }
            }
          }
        }
      }
    }
  }
}

// CHECK: simulation.container.size
// CHECK: cf.cond_br
// CHECK: simulation.container.write
// An associative foreach carries its current key to the traversal step when
// continue bypasses the rest of the loop body.
// CHECK: simulation.assoc.traverse
// CHECK: ^[[ASSOC_HEADER:bb[0-9]+]](%[[KEY:.*]]: i32, %[[VALID:.*]]: i1):
// CHECK: cf.cond_br %[[VALID]], ^[[ASSOC_STEP:bb[0-9]+]](%[[KEY]] : i32),
// CHECK: ^[[ASSOC_STEP]](%[[STEP_KEY:.*]]: i32):
// CHECK: simulation.assoc.traverse {{.*}}, %[[STEP_KEY]]
// CHECK-NOT: obelisk.sv.
