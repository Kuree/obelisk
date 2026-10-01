// RUN: obelisk-opt %s '--lower-obelisk-to-sim=opt-level=0' | FileCheck %s

// IEEE 1800-2017 20.6.2: $bits of a dynamic array, queue, or string is a
// runtime inquiry over the live size, multiplied by the fixed element width.
module {
  obelisk.sv.symbol.definition @s0.top attributes {definition_kind = 0 : i32, hierarchical_name = "top", name = "top", node_id = 0 : i64} {
  }
  obelisk.sv.symbol.root @s1.$root attributes {hierarchical_name = "\\$root ", name = "$root", node_id = 1 : i64} {
    obelisk.sv.symbol.compilation_unit @s2 attributes {hierarchical_name = "$unit", node_id = 2 : i64} {
    }
    obelisk.sv.symbol.instance @s3.top attributes {hierarchical_name = "top", is_uninstantiated = false, name = "top", node_id = 3 : i64, referenced_path = "top", referenced_symbol = @s0.top} {
      obelisk.sv.symbol.instance_body @s4.top attributes {hierarchical_name = "top", name = "top", node_id = 4 : i64, time_precision_fs = 1000000 : i64, time_unit_fs = 1000000 : i64} {
        obelisk.sv.symbol.variable @s5.dynamic_value attributes {hierarchical_name = "top.dynamic_value", lifetime = 1 : i32, name = "dynamic_value", node_id = 5 : i64, semantic_type = !obelisk.dynarray<!obelisk.integral<5, false, false, 4 : 0, bit>>} {
        }
        obelisk.sv.symbol.variable @s6.queue_value attributes {hierarchical_name = "top.queue_value", lifetime = 1 : i32, name = "queue_value", node_id = 6 : i64, semantic_type = !obelisk.queue<!obelisk.integral<8, false, false, 7 : 0, byte>, 0>} {
        }
        obelisk.sv.symbol.variable @s7.string_value attributes {hierarchical_name = "top.string_value", lifetime = 1 : i32, name = "string_value", node_id = 7 : i64, semantic_type = !obelisk.string} {
        }
        obelisk.sv.symbol.variable @s8.result attributes {hierarchical_name = "top.result", lifetime = 1 : i32, name = "result", node_id = 8 : i64, semantic_type = !obelisk.integral<32, true, false, 31 : 0, int>} {
        }
        obelisk.sv.symbol.procedural_block @s9 attributes {hierarchical_name = "top", node_id = 9 : i64, procedure_kind = 0 : i32, time_precision_fs = 1000000 : i64, time_unit_fs = 1000000 : i64} {
          obelisk.sv.statement.block attributes {node_id = 10 : i64} {
            obelisk.sv.statement.list attributes {node_id = 11 : i64} {
              obelisk.sv.statement.expression_statement attributes {node_id = 12 : i64} {
                obelisk.sv.expression.assignment attributes {assignment_kind = 0 : i32, is_signed = true, node_id = 13 : i64, semantic_type = !obelisk.integral<32, true, false, 31 : 0, int>} {
                  obelisk.sv.expression.named_value attributes {is_signed = true, node_id = 14 : i64, referenced_path = "top.result", referenced_symbol = @s1.$root::@s3.top::@s4.top::@s8.result, semantic_type = !obelisk.integral<32, true, false, 31 : 0, int>} {
                  }
                  obelisk.sv.expression.call attributes {argument_count = 1 : i64, callee_name = "$bits", constraint_restrictions = [], defaulted_arguments = array<i64: 0>, has_inline_constraints = false, has_iterator_expression = false, has_output_arguments = false, has_this_class = false, is_signed = true, is_super_class = false, is_system_call = true, node_id = 15 : i64, semantic_type = !obelisk.integral<32, true, true, 31 : 0, integer>, subroutine_kind = 0 : i32, system_library_cell = "work.top", system_scope_path = "top", system_scope_symbol = @s1.$root::@s3.top::@s4.top} {
                    obelisk.sv.expression.named_value attributes {is_signed = false, node_id = 16 : i64, referenced_path = "top.dynamic_value", referenced_symbol = @s1.$root::@s3.top::@s4.top::@s5.dynamic_value, semantic_type = !obelisk.dynarray<!obelisk.integral<5, false, false, 4 : 0, bit>>} {
                    }
                  }
                }
              }
              obelisk.sv.statement.expression_statement attributes {node_id = 17 : i64} {
                obelisk.sv.expression.assignment attributes {assignment_kind = 0 : i32, is_signed = true, node_id = 18 : i64, semantic_type = !obelisk.integral<32, true, false, 31 : 0, int>} {
                  obelisk.sv.expression.named_value attributes {is_signed = true, node_id = 19 : i64, referenced_path = "top.result", referenced_symbol = @s1.$root::@s3.top::@s4.top::@s8.result, semantic_type = !obelisk.integral<32, true, false, 31 : 0, int>} {
                  }
                  obelisk.sv.expression.call attributes {argument_count = 1 : i64, callee_name = "$bits", constraint_restrictions = [], defaulted_arguments = array<i64: 0>, has_inline_constraints = false, has_iterator_expression = false, has_output_arguments = false, has_this_class = false, is_signed = true, is_super_class = false, is_system_call = true, node_id = 20 : i64, semantic_type = !obelisk.integral<32, true, true, 31 : 0, integer>, subroutine_kind = 0 : i32, system_library_cell = "work.top", system_scope_path = "top", system_scope_symbol = @s1.$root::@s3.top::@s4.top} {
                    obelisk.sv.expression.named_value attributes {is_signed = false, node_id = 21 : i64, referenced_path = "top.queue_value", referenced_symbol = @s1.$root::@s3.top::@s4.top::@s6.queue_value, semantic_type = !obelisk.queue<!obelisk.integral<8, false, false, 7 : 0, byte>, 0>} {
                    }
                  }
                }
              }
              obelisk.sv.statement.expression_statement attributes {node_id = 22 : i64} {
                obelisk.sv.expression.assignment attributes {assignment_kind = 0 : i32, is_signed = true, node_id = 23 : i64, semantic_type = !obelisk.integral<32, true, false, 31 : 0, int>} {
                  obelisk.sv.expression.named_value attributes {is_signed = true, node_id = 24 : i64, referenced_path = "top.result", referenced_symbol = @s1.$root::@s3.top::@s4.top::@s8.result, semantic_type = !obelisk.integral<32, true, false, 31 : 0, int>} {
                  }
                  obelisk.sv.expression.call attributes {argument_count = 1 : i64, callee_name = "$bits", constraint_restrictions = [], defaulted_arguments = array<i64: 0>, has_inline_constraints = false, has_iterator_expression = false, has_output_arguments = false, has_this_class = false, is_signed = true, is_super_class = false, is_system_call = true, node_id = 25 : i64, semantic_type = !obelisk.integral<32, true, true, 31 : 0, integer>, subroutine_kind = 0 : i32, system_library_cell = "work.top", system_scope_path = "top", system_scope_symbol = @s1.$root::@s3.top::@s4.top} {
                    obelisk.sv.expression.named_value attributes {is_signed = false, node_id = 26 : i64, referenced_path = "top.string_value", referenced_symbol = @s1.$root::@s3.top::@s4.top::@s7.string_value, semantic_type = !obelisk.string} {
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

// CHECK-DAG: %[[FIVE:.*]] = arith.constant 5 : i32
// CHECK-DAG: %[[EIGHT:.*]] = arith.constant 8 : i32
// CHECK: %[[DYNAMIC:.*]] = simulation.ref.load {{.*}} -> !simulation.dynamic_array<i5>
// CHECK: %[[DYNAMIC_SIZE:.*]] = simulation.container.size %[[DYNAMIC]]
// CHECK: %[[DYNAMIC_SIZE32:.*]] = arith.trunci %[[DYNAMIC_SIZE]] : i64 to i32
// CHECK: arith.muli %[[DYNAMIC_SIZE32]], %[[FIVE]] : i32
// CHECK: %[[QUEUE:.*]] = simulation.ref.load {{.*}} -> !simulation.queue<i8, 0>
// CHECK: %[[QUEUE_SIZE:.*]] = simulation.container.size %[[QUEUE]]
// CHECK: arith.muli {{.*}}, %[[EIGHT]] : i32
// CHECK: %[[STRING:.*]] = simulation.ref.load {{.*}} -> !simulation.string
// CHECK: %[[STRING_SIZE:.*]] = simulation.string.length %[[STRING]]
// CHECK: arith.muli {{.*}}, %[[EIGHT]] : i32
