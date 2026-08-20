// RUN: obelisk-opt %s '--lower-obelisk-to-sim=opt-level=0' | FileCheck %s

// Each $timeformat argument has its own default, so a call with none restores
// the IEEE defaults rather than lowering to nothing.

// CHECK-LABEL: obelisk_sim.func private @unit_0(
// CHECK-DAG: %[[SUFFIX:.*]] = obelisk_sim.bytes.constant "ns"
// CHECK-DAG: %[[EMPTY:.*]] = obelisk_sim.bytes.constant ""
// CHECK-DAG: %[[UNITS:.*]] = arith.constant -9 : i32
// CHECK-DAG: %[[DIGITS:.*]] = arith.constant 5 : i32
// CHECK-DAG: %[[WIDTH:.*]] = arith.constant 10 : i32
// CHECK-DAG: %[[ZERO:.*]] = arith.constant 0 : i32
// CHECK-DAG: %[[DEFAULT_WIDTH:.*]] = arith.constant 20 : i32
// CHECK: obelisk_sim.time.format {{.*}}, %[[UNITS]], %[[DIGITS]], %[[SUFFIX]], %[[WIDTH]]
// CHECK: obelisk_sim.time.format {{.*}}, %[[UNITS]], %[[ZERO]], %[[EMPTY]], %[[DEFAULT_WIDTH]]

module {
  obelisk.sv.symbol.definition attributes {definition_kind = 0 : i32, hierarchical_name = "top", name = "top", node_id = 0 : i64, sym_name = "s0.top"} {
  }
  obelisk.sv.symbol.root attributes {hierarchical_name = "\\$root ", name = "$root", node_id = 1 : i64, sym_name = "s1.$root"} {
    obelisk.sv.symbol.compilation_unit attributes {hierarchical_name = "$unit", node_id = 2 : i64, sym_name = "s2"} {
    }
    obelisk.sv.symbol.instance attributes {hierarchical_name = "top", is_uninstantiated = false, name = "top", node_id = 3 : i64, referenced_path = "top", referenced_symbol = @s0.top, sym_name = "s3.top"} {
      obelisk.sv.symbol.instance_body attributes {hierarchical_name = "top", name = "top", node_id = 4 : i64, sym_name = "s4.top", time_precision_fs = 1000000 : i64, time_unit_fs = 1000000 : i64} {
        obelisk.sv.symbol.procedural_block attributes {hierarchical_name = "top", node_id = 5 : i64, procedure_kind = 0 : i32, sym_name = "s5", time_precision_fs = 1000000 : i64, time_unit_fs = 1000000 : i64} {
          obelisk.sv.statement.block attributes {node_id = 6 : i64} {
            obelisk.sv.statement.list attributes {node_id = 7 : i64} {
              obelisk.sv.statement.expression_statement attributes {node_id = 8 : i64} {
                obelisk.sv.expression.call attributes {argument_count = 4 : i64, callee_name = "$timeformat", constraint_restrictions = [], defaulted_arguments = array<i64: 0, 0, 0, 0>, has_inline_constraints = false, has_iterator_expression = false, has_output_arguments = false, has_this_class = false, is_super_class = false, is_system_call = true, node_id = 9 : i64, semantic_type = !obelisk.void, subroutine_kind = 1 : i32, system_library_cell = "work.top", system_scope_path = "top", system_scope_symbol = @s1.$root::@s3.top::@s4.top} {
                  obelisk.sv.expression.unary_op attributes {node_id = 10 : i64, operator_kind = 1 : i32, semantic_type = !obelisk.integral<32, true, false, 31 : 0, int>} {
                    obelisk.sv.expression.integer_literal attributes {constant_value = "9", node_id = 11 : i64, semantic_type = !obelisk.integral<32, true, false, 31 : 0, int>} {
                    }
                  }
                  obelisk.sv.expression.integer_literal attributes {constant_value = "5", node_id = 12 : i64, semantic_type = !obelisk.integral<32, true, false, 31 : 0, int>} {
                  }
                  obelisk.sv.expression.conversion attributes {node_id = 13 : i64, semantic_type = !obelisk.string} {
                    obelisk.sv.expression.string_literal attributes {constant_value = "ns", node_id = 14 : i64, semantic_type = !obelisk.ranged_packed_array<15 : 0 x !obelisk.integral<1, false, false, 0 : 0, bit>>} {
                    }
                  }
                  obelisk.sv.expression.integer_literal attributes {constant_value = "10", node_id = 15 : i64, semantic_type = !obelisk.integral<32, true, false, 31 : 0, int>} {
                  }
                }
              }
              obelisk.sv.statement.expression_statement attributes {node_id = 16 : i64} {
                obelisk.sv.expression.call attributes {argument_count = 0 : i64, callee_name = "$timeformat", constraint_restrictions = [], defaulted_arguments = array<i64>, has_inline_constraints = false, has_iterator_expression = false, has_output_arguments = false, has_this_class = false, is_super_class = false, is_system_call = true, node_id = 17 : i64, semantic_type = !obelisk.void, subroutine_kind = 1 : i32, system_library_cell = "work.top", system_scope_path = "top", system_scope_symbol = @s1.$root::@s3.top::@s4.top} {
                }
              }
            }
          }
        }
      }
    }
  }
}
