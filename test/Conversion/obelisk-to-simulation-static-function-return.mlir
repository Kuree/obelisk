// RUN: obelisk-opt %s '--lower-obelisk-to-sim=opt-level=0' | FileCheck %s

// IEEE 1800-2017 13.4.2: the declarations of a static function -- including
// the implicit variable that holds its return value -- are static, so the
// return variable keeps design storage and its value persists between calls.

module {
  obelisk.sv.symbol.definition attributes {definition_kind = 0 : i32, hierarchical_name = "static_function_return", name = "static_function_return", node_id = 0 : i64, sym_name = "s0.static_function_return"} {
  }
  obelisk.sv.symbol.root attributes {hierarchical_name = "\\$root ", name = "$root", node_id = 1 : i64, sym_name = "s1.$root"} {
    obelisk.sv.symbol.compilation_unit attributes {hierarchical_name = "$unit", node_id = 2 : i64, sym_name = "s2"} {
    }
    obelisk.sv.symbol.instance attributes {hierarchical_name = "static_function_return", is_uninstantiated = false, name = "static_function_return", node_id = 3 : i64, referenced_path = "static_function_return", referenced_symbol = @s0.static_function_return, sym_name = "s3.static_function_return"} {
      obelisk.sv.symbol.instance_body attributes {hierarchical_name = "static_function_return", name = "static_function_return", node_id = 4 : i64, sym_name = "s4.static_function_return", time_precision_fs = 1000000 : i64, time_unit_fs = 1000000 : i64} {
        obelisk.sv.symbol.subroutine attributes {default_lifetime = 1 : i32, hierarchical_name = "static_function_return.accumulate", name = "accumulate", node_id = 5 : i64, return_variable_path = "static_function_return.accumulate.accumulate", return_variable_symbol = @s1.$root::@s3.static_function_return::@s4.static_function_return::@s5.accumulate::@s7.accumulate, semantic_type = !obelisk.subroutine<(!obelisk.integral<32, true, false, 31 : 0, int>) -> !obelisk.integral<32, true, false, 31 : 0, int>, false>, subroutine_kind = 0 : i32, sym_name = "s5.accumulate", time_precision_fs = 1000000 : i64, time_unit_fs = 1000000 : i64} {
          obelisk.sv.statement.expression_statement attributes {node_id = 6 : i64} {
            obelisk.sv.expression.assignment attributes {assignment_kind = 0 : i32, is_signed = true, node_id = 7 : i64, semantic_type = !obelisk.integral<32, true, false, 31 : 0, int>} {
              obelisk.sv.expression.named_value attributes {is_signed = true, node_id = 8 : i64, referenced_path = "static_function_return.accumulate.accumulate", referenced_symbol = @s1.$root::@s3.static_function_return::@s4.static_function_return::@s5.accumulate::@s7.accumulate, semantic_type = !obelisk.integral<32, true, false, 31 : 0, int>} {
              }
              obelisk.sv.expression.binary_op attributes {is_signed = true, node_id = 9 : i64, operator_kind = 0 : i32, semantic_type = !obelisk.integral<32, true, false, 31 : 0, int>} {
                obelisk.sv.expression.named_value attributes {is_signed = true, node_id = 10 : i64, referenced_path = "static_function_return.accumulate.accumulate", referenced_symbol = @s1.$root::@s3.static_function_return::@s4.static_function_return::@s5.accumulate::@s7.accumulate, semantic_type = !obelisk.integral<32, true, false, 31 : 0, int>} {
                }
                obelisk.sv.expression.named_value attributes {is_signed = true, node_id = 11 : i64, referenced_path = "static_function_return.accumulate.step", referenced_symbol = @s1.$root::@s3.static_function_return::@s4.static_function_return::@s5.accumulate::@s6.step, semantic_type = !obelisk.integral<32, true, false, 31 : 0, int>} {
                }
              }
            }
          }
          obelisk.sv.symbol.formal_argument attributes {direction = 0 : i32, hierarchical_name = "static_function_return.accumulate.step", lifetime = 1 : i32, name = "step", node_id = 12 : i64, semantic_type = !obelisk.integral<32, true, false, 31 : 0, int>, sym_name = "s6.step"} {
          }
          obelisk.sv.symbol.variable attributes {hierarchical_name = "static_function_return.accumulate.accumulate", is_compiler_generated, name = "accumulate", node_id = 13 : i64, semantic_type = !obelisk.integral<32, true, false, 31 : 0, int>, sym_name = "s7.accumulate"} {
          }
        }
        obelisk.sv.symbol.variable attributes {hierarchical_name = "static_function_return.value", lifetime = 1 : i32, name = "value", node_id = 14 : i64, semantic_type = !obelisk.integral<32, true, false, 31 : 0, int>, sym_name = "s8.value"} {
        }
        obelisk.sv.symbol.procedural_block attributes {hierarchical_name = "static_function_return", node_id = 15 : i64, procedure_kind = 0 : i32, sym_name = "s9", time_precision_fs = 1000000 : i64, time_unit_fs = 1000000 : i64} {
          obelisk.sv.statement.expression_statement attributes {node_id = 16 : i64} {
            obelisk.sv.expression.assignment attributes {assignment_kind = 0 : i32, is_signed = true, node_id = 17 : i64, semantic_type = !obelisk.integral<32, true, false, 31 : 0, int>} {
              obelisk.sv.expression.named_value attributes {is_signed = true, node_id = 18 : i64, referenced_path = "static_function_return.value", referenced_symbol = @s1.$root::@s3.static_function_return::@s4.static_function_return::@s8.value, semantic_type = !obelisk.integral<32, true, false, 31 : 0, int>} {
              }
              obelisk.sv.expression.call attributes {argument_count = 1 : i64, callee_name = "accumulate", constraint_restrictions = [], defaulted_arguments = array<i64: 0>, has_inline_constraints = false, has_iterator_expression = false, has_output_arguments = false, has_this_class = false, is_signed = true, is_super_class = false, is_system_call = false, node_id = 19 : i64, referenced_path = "static_function_return.accumulate", referenced_symbol = @s1.$root::@s3.static_function_return::@s4.static_function_return::@s5.accumulate, semantic_type = !obelisk.integral<32, true, false, 31 : 0, int>, subroutine_kind = 0 : i32} {
                obelisk.sv.expression.integer_literal attributes {constant_value = "1", is_declared_unsized = true, is_signed = true, node_id = 20 : i64, semantic_type = !obelisk.integral<32, true, false, 31 : 0, int>} {
                }
              }
            }
          }
        }
      }
    }
  }
}


// CHECK: obelisk_sim.storage.decl {{[0-9]+}} {{.*}} hierarchy "static_function_return.accumulate.accumulate"
// CHECK-NOT: obelisk.sv.
