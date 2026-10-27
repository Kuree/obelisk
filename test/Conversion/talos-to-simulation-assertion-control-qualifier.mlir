// RUN: talos-opt %s '--lower-talos-to-sim=opt-level=0' | FileCheck %s

// IEEE 1800-2017 20.12 defines 255 as the all-types assertion-control mask
// and includes unique/unique0/priority violation reports. A selected qualifier
// is a real control target; designs without qualifiers get no qualifier query.
//
//   module top;
//     initial begin
//       $assertcontrol(4, 255, 7);
//       unique if (1'b0) ;
//     end
//   endmodule

module {
  talos.sv.symbol.definition @s0.top attributes {definition_kind = 0 : i32, hierarchical_name = "top", name = "top", node_id = 0 : i64} {
  }
  talos.sv.symbol.root @s1.$root attributes {hierarchical_name = "\\$root ", name = "$root", node_id = 1 : i64} {
    talos.sv.symbol.compilation_unit @s2 attributes {hierarchical_name = "$unit", node_id = 2 : i64} {
    }
    talos.sv.symbol.instance @s3.top attributes {hierarchical_name = "top", is_uninstantiated = false, name = "top", node_id = 3 : i64, referenced_path = "top", referenced_symbol = @s0.top} {
      talos.sv.symbol.instance_body @s4.top attributes {hierarchical_name = "top", name = "top", node_id = 4 : i64, time_precision_fs = 1000000 : i64, time_unit_fs = 1000000 : i64} {
        talos.sv.symbol.procedural_block @s5 attributes {hierarchical_name = "top", node_id = 5 : i64, procedure_kind = 0 : i32, time_precision_fs = 1000000 : i64, time_unit_fs = 1000000 : i64} {
          talos.sv.statement.block attributes {node_id = 6 : i64} {
            talos.sv.statement.list attributes {node_id = 7 : i64} {
              talos.sv.statement.expression_statement attributes {node_id = 8 : i64} {
                talos.sv.expression.call attributes {argument_count = 3 : i64, callee_name = "$assertcontrol", constraint_restrictions = [], defaulted_arguments = array<i64: 0, 0, 0>, has_inline_constraints = false, has_iterator_expression = false, has_output_arguments = false, has_this_class = false, is_signed = false, is_super_class = false, is_system_call = true, node_id = 9 : i64, semantic_type = !talos.void, subroutine_kind = 1 : i32, system_library_cell = "work.top", system_scope_path = "top", system_scope_symbol = @s1.$root::@s3.top::@s4.top} {
                  talos.sv.expression.integer_literal attributes {constant_value = "4", is_declared_unsized = true, is_signed = true, node_id = 10 : i64, semantic_type = !talos.integral<32, true, false, 31 : 0, int>} {
                  }
                  talos.sv.expression.integer_literal attributes {constant_value = "255", is_declared_unsized = true, is_signed = true, node_id = 11 : i64, semantic_type = !talos.integral<32, true, false, 31 : 0, int>} {
                  }
                  talos.sv.expression.integer_literal attributes {constant_value = "7", is_declared_unsized = true, is_signed = true, node_id = 12 : i64, semantic_type = !talos.integral<32, true, false, 31 : 0, int>} {
                  }
                }
              }
              talos.sv.statement.conditional attributes {check_kind = 1 : i32, condition_count = 1 : i64, condition_pattern_flags = array<i64: 0>, has_else = false, node_id = 13 : i64} {
                talos.sv.expression.integer_literal attributes {constant_value = "1'b0", is_signed = false, node_id = 14 : i64, semantic_type = !talos.ranged_packed_array<0 : 0 x !talos.integral<1, false, false, 0 : 0, bit>>} {
                }
                talos.sv.statement.empty attributes {node_id = 15 : i64} {
                }
              }
            }
          }
        }
      }
    }
  }
}

// The violation is checked under the control state, then reported from the
// Observed region (IEEE 1800-2023 12.4.2.1), so the warning text lives in the
// report's evaluator.
// CHECK-LABEL: simulation.func private @unit_0.violation.
// CHECK: simulation.bytes.constant "{{.*}}unique if violation: no match"
// CHECK: simulation.assert.deferred_mature
// CHECK-LABEL: simulation.func private @unit_0(
// CHECK: simulation.assert.control {{.*}} action <off> assertion [[ID:[0-9]+]]
// CHECK: %[[ENABLED:.*]] = simulation.assert.enabled {{.*}} assertion [[ID]]
// CHECK: cf.cond_br %[[ENABLED]]
// CHECK: simulation.assert.deferred_enqueue {{[0-9]+}} {simulation.assertion_control_target_id = [[ID]] : i64
// CHECK: simulation.spawn @unit_0.violation.
