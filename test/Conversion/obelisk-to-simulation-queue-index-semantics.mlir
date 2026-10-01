// RUN: obelisk-opt %s '--lower-obelisk-to-sim=opt-level=0' | FileCheck %s

// Four-state container indices retain their knownness. X/Z-containing values
// become the negative invalid-index sentinel instead of aliasing index zero.
// CHECK-LABEL: simulation.func private @unit_0
// CHECK: %[[DELETE_KNOWN:.*]] = simulation.logic.compare case_eq
// CHECK: %[[DELETE_INDEX:.*]] = arith.select %[[DELETE_KNOWN]], {{.*}}, %{{.*}} : i64
// CHECK: simulation.queue.delete {{.*}}[%[[DELETE_INDEX]]]
// CHECK: %[[INSERT_KNOWN:.*]] = simulation.logic.compare case_eq
// CHECK: %[[INSERT_INDEX:.*]] = arith.select %[[INSERT_KNOWN]], {{.*}}, %{{.*}} : i64
// A full bounded queue trims its last element only for a valid insertion index.
// CHECK: %[[INSERT_SIZE:.*]] = simulation.container.size
// CHECK: arith.cmpi uge, %[[INSERT_SIZE]],
// CHECK: arith.cmpi sge, %[[INSERT_INDEX]],
// CHECK: arith.cmpi ule, %[[INSERT_INDEX]],
// CHECK: simulation.bytes.constant {{.*}}bounded queue insert discarded its last element
// CHECK: simulation.display
// CHECK: simulation.queue.delete
// CHECK: simulation.queue.insert {{.*}}[%[[INSERT_INDEX]]]
// Queue assignment delegates append and invalid-index handling to the one
// container-write intrinsic rather than duplicating bounds checks per caller.
// CHECK: simulation.container.write
// A queue read uses the same knownness-preserving index conversion.
// CHECK: %[[READ_KNOWN:.*]] = simulation.logic.compare case_eq
// CHECK: %[[READ_INDEX:.*]] = arith.select %[[READ_KNOWN]], {{.*}}, %{{.*}} : i64
// CHECK: simulation.container.read {{.*}}, %[[READ_INDEX]]

module {
  obelisk.sv.symbol.definition @s0.queue_index_regression attributes {definition_kind = 0 : i32, hierarchical_name = "queue_index_regression", name = "queue_index_regression", node_id = 0 : i64} {
  }
  obelisk.sv.symbol.root @s1.$root attributes {hierarchical_name = "\\$root ", name = "$root", node_id = 1 : i64} {
    obelisk.sv.symbol.compilation_unit @s2 attributes {hierarchical_name = "$unit", node_id = 2 : i64} {
    }
    obelisk.sv.symbol.instance @s3.queue_index_regression attributes {hierarchical_name = "queue_index_regression", is_uninstantiated = false, name = "queue_index_regression", node_id = 3 : i64, referenced_path = "queue_index_regression", referenced_symbol = @s0.queue_index_regression} {
      obelisk.sv.symbol.instance_body @s4.queue_index_regression attributes {hierarchical_name = "queue_index_regression", name = "queue_index_regression", node_id = 4 : i64, time_precision_fs = 1000000 : i64, time_unit_fs = 1000000 : i64} {
        obelisk.sv.symbol.variable @s5.queue attributes {hierarchical_name = "queue_index_regression.queue", lifetime = 1 : i32, name = "queue", node_id = 5 : i64, semantic_type = !obelisk.queue<!obelisk.string, 2>} {
        }
        obelisk.sv.symbol.variable @s6.index attributes {hierarchical_name = "queue_index_regression.index", lifetime = 1 : i32, name = "index", node_id = 6 : i64, semantic_type = !obelisk.ranged_packed_array<31 : 0 x !obelisk.integral<1, false, true, 0 : 0, logic>>} {
        }
        obelisk.sv.symbol.variable @s7.value attributes {hierarchical_name = "queue_index_regression.value", lifetime = 1 : i32, name = "value", node_id = 7 : i64, semantic_type = !obelisk.string} {
        }
        obelisk.sv.symbol.procedural_block @s8 attributes {hierarchical_name = "queue_index_regression", node_id = 8 : i64, procedure_kind = 0 : i32, time_precision_fs = 1000000 : i64, time_unit_fs = 1000000 : i64} {
          obelisk.sv.statement.block attributes {node_id = 9 : i64} {
            obelisk.sv.statement.list attributes {node_id = 10 : i64} {
              obelisk.sv.statement.expression_statement attributes {node_id = 11 : i64} {
                obelisk.sv.expression.call attributes {argument_count = 2 : i64, callee_name = "delete", constraint_restrictions = [], defaulted_arguments = array<i64: 0, 0>, has_inline_constraints = false, has_iterator_expression = false, has_output_arguments = false, has_this_class = false, is_signed = false, is_super_class = false, is_system_call = true, node_id = 12 : i64, semantic_type = !obelisk.void, subroutine_kind = 0 : i32, system_library_cell = "work.queue_index_regression", system_scope_path = "queue_index_regression", system_scope_symbol = @s1.$root::@s3.queue_index_regression::@s4.queue_index_regression} {
                  obelisk.sv.expression.named_value attributes {is_signed = false, node_id = 13 : i64, referenced_path = "queue_index_regression.queue", referenced_symbol = @s1.$root::@s3.queue_index_regression::@s4.queue_index_regression::@s5.queue, semantic_type = !obelisk.queue<!obelisk.string, 2>} {
                  }
                  obelisk.sv.expression.named_value attributes {is_signed = false, node_id = 14 : i64, referenced_path = "queue_index_regression.index", referenced_symbol = @s1.$root::@s3.queue_index_regression::@s4.queue_index_regression::@s6.index, semantic_type = !obelisk.ranged_packed_array<31 : 0 x !obelisk.integral<1, false, true, 0 : 0, logic>>} {
                  }
                }
              }
              obelisk.sv.statement.expression_statement attributes {node_id = 15 : i64} {
                obelisk.sv.expression.call attributes {argument_count = 3 : i64, callee_name = "insert", constraint_restrictions = [], defaulted_arguments = array<i64: 0, 0, 0>, has_inline_constraints = false, has_iterator_expression = false, has_output_arguments = false, has_this_class = false, is_signed = false, is_super_class = false, is_system_call = true, node_id = 16 : i64, semantic_type = !obelisk.void, subroutine_kind = 0 : i32, system_library_cell = "work.queue_index_regression", system_scope_path = "queue_index_regression", system_scope_symbol = @s1.$root::@s3.queue_index_regression::@s4.queue_index_regression} {
                  obelisk.sv.expression.named_value attributes {is_signed = false, node_id = 17 : i64, referenced_path = "queue_index_regression.queue", referenced_symbol = @s1.$root::@s3.queue_index_regression::@s4.queue_index_regression::@s5.queue, semantic_type = !obelisk.queue<!obelisk.string, 2>} {
                  }
                  obelisk.sv.expression.named_value attributes {is_signed = false, node_id = 18 : i64, referenced_path = "queue_index_regression.index", referenced_symbol = @s1.$root::@s3.queue_index_regression::@s4.queue_index_regression::@s6.index, semantic_type = !obelisk.ranged_packed_array<31 : 0 x !obelisk.integral<1, false, true, 0 : 0, logic>>} {
                  }
                  obelisk.sv.expression.conversion attributes {is_signed = false, node_id = 19 : i64, semantic_type = !obelisk.string} {
                    obelisk.sv.expression.string_literal attributes {constant_value = "inserted", is_signed = false, node_id = 20 : i64, semantic_type = !obelisk.ranged_packed_array<63 : 0 x !obelisk.integral<1, false, false, 0 : 0, bit>>} {
                    }
                  }
                }
              }
              obelisk.sv.statement.expression_statement attributes {node_id = 21 : i64} {
                obelisk.sv.expression.assignment attributes {assignment_kind = 0 : i32, is_signed = false, node_id = 22 : i64, semantic_type = !obelisk.string} {
                  obelisk.sv.expression.element_select attributes {is_signed = false, node_id = 23 : i64, semantic_type = !obelisk.string} {
                    obelisk.sv.expression.named_value attributes {is_signed = false, node_id = 24 : i64, referenced_path = "queue_index_regression.queue", referenced_symbol = @s1.$root::@s3.queue_index_regression::@s4.queue_index_regression::@s5.queue, semantic_type = !obelisk.queue<!obelisk.string, 2>} {
                    }
                    obelisk.sv.expression.named_value attributes {is_signed = false, node_id = 25 : i64, referenced_path = "queue_index_regression.index", referenced_symbol = @s1.$root::@s3.queue_index_regression::@s4.queue_index_regression::@s6.index, semantic_type = !obelisk.ranged_packed_array<31 : 0 x !obelisk.integral<1, false, true, 0 : 0, logic>>} {
                    }
                  }
                  obelisk.sv.expression.conversion attributes {is_signed = false, node_id = 26 : i64, semantic_type = !obelisk.string} {
                    obelisk.sv.expression.string_literal attributes {constant_value = "stored", is_signed = false, node_id = 27 : i64, semantic_type = !obelisk.ranged_packed_array<47 : 0 x !obelisk.integral<1, false, false, 0 : 0, bit>>} {
                    }
                  }
                }
              }
              obelisk.sv.statement.expression_statement attributes {node_id = 28 : i64} {
                obelisk.sv.expression.assignment attributes {assignment_kind = 0 : i32, is_signed = false, node_id = 29 : i64, semantic_type = !obelisk.string} {
                  obelisk.sv.expression.named_value attributes {is_signed = false, node_id = 30 : i64, referenced_path = "queue_index_regression.value", referenced_symbol = @s1.$root::@s3.queue_index_regression::@s4.queue_index_regression::@s7.value, semantic_type = !obelisk.string} {
                  }
                  obelisk.sv.expression.element_select attributes {is_signed = false, node_id = 31 : i64, semantic_type = !obelisk.string} {
                    obelisk.sv.expression.named_value attributes {is_signed = false, node_id = 32 : i64, referenced_path = "queue_index_regression.queue", referenced_symbol = @s1.$root::@s3.queue_index_regression::@s4.queue_index_regression::@s5.queue, semantic_type = !obelisk.queue<!obelisk.string, 2>} {
                    }
                    obelisk.sv.expression.named_value attributes {is_signed = false, node_id = 33 : i64, referenced_path = "queue_index_regression.index", referenced_symbol = @s1.$root::@s3.queue_index_regression::@s4.queue_index_regression::@s6.index, semantic_type = !obelisk.ranged_packed_array<31 : 0 x !obelisk.integral<1, false, true, 0 : 0, logic>>} {
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
