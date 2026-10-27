// RUN: talos-opt %s '--lower-talos-to-sim=opt-level=0' | FileCheck %s

// IEEE 1800-2023 20.4.1, 20.4.2: an explicit $unit names the compilation
// unit's time scale, which the frontend froze on the call, and
// $printtimescale names a $root target "$root".
//
//   timeunit 100ps / 10fs;
//   module top;
//     timeunit 1ms / 1us;
//     int u, p;
//     initial begin
//       u = $timeunit($unit);
//       p = $timeprecision($unit);
//       $printtimescale($unit);
//       $printtimescale($root);
//     end
//   endmodule

// CHECK-DAG: simulation.bytes.constant "Time scale of ($unit) is 100ps / 10fs"
// CHECK-DAG: simulation.bytes.constant "Time scale of ($root) is 10fs / 10fs"
// CHECK-DAG: %[[UNIT:.*]] = arith.constant -10 : i32
// CHECK-DAG: %[[PRECISION:.*]] = arith.constant -14 : i32
// CHECK: simulation.ref.store %[[UNIT]] to %arg1
// CHECK: simulation.ref.store %[[PRECISION]] to %arg2

module attributes {coverage.language_version = 2023 : i32} {
  talos.sv.symbol.definition @s0.top attributes {definition_kind = 0 : i32, hierarchical_name = "top", name = "top", node_id = 0 : i64} {
  }
  talos.sv.symbol.root @s1.$root attributes {declared_time_precision_fs = 10 : i64, hierarchical_name = "\\$root ", name = "$root", node_id = 1 : i64} {
    talos.sv.symbol.compilation_unit @s2 attributes {hierarchical_name = "$unit", node_id = 2 : i64, simulation.vpi_definition_name = "$unit"} {
    }
    talos.sv.symbol.instance @s3.top attributes {hierarchical_name = "top", is_uninstantiated = false, name = "top", node_id = 3 : i64, referenced_path = "top", referenced_symbol = @s0.top} {
      talos.sv.symbol.instance_body @s4.top attributes {hierarchical_name = "top", name = "top", node_id = 4 : i64, simulation.vpi_automatic = false, simulation.vpi_cell_instance = false, simulation.vpi_definition_name = "top", simulation.vpi_top = true, time_precision_fs = 1000000000 : i64, time_unit_fs = 1000000000000 : i64, vpi_scope_kind = 32 : i32} {
        talos.sv.symbol.variable @s5.u attributes {hierarchical_name = "top.u", lifetime = 1 : i32, name = "u", node_id = 5 : i64, semantic_type = !talos.integral<32, true, false, 31 : 0, int>} {
        }
        talos.sv.symbol.variable @s6.p attributes {hierarchical_name = "top.p", lifetime = 1 : i32, name = "p", node_id = 6 : i64, semantic_type = !talos.integral<32, true, false, 31 : 0, int>} {
        }
        talos.sv.symbol.procedural_block @s7 attributes {hierarchical_name = "top", node_id = 7 : i64, procedure_kind = 0 : i32, time_precision_fs = 1000000000 : i64, time_unit_fs = 1000000000000 : i64} {
          talos.sv.statement.block attributes {node_id = 8 : i64} {
            talos.sv.statement.list attributes {node_id = 9 : i64} {
              talos.sv.statement.expression_statement attributes {node_id = 10 : i64} {
                talos.sv.expression.assignment attributes {assignment_kind = 0 : i32, is_signed = true, node_id = 11 : i64, semantic_type = !talos.integral<32, true, false, 31 : 0, int>} {
                  talos.sv.expression.named_value attributes {is_signed = true, node_id = 12 : i64, referenced_path = "top.u", referenced_symbol = @s1.$root::@s3.top::@s4.top::@s5.u, semantic_type = !talos.integral<32, true, false, 31 : 0, int>} {
                  }
                  talos.sv.expression.call attributes {argument_count = 1 : i64, callee_name = "$timeunit", constraint_restrictions = [], defaulted_arguments = array<i64: 0>, has_inline_constraints = false, has_iterator_expression = false, has_output_arguments = false, has_this_class = false, is_signed = true, is_super_class = false, is_system_call = true, node_id = 13 : i64, semantic_type = !talos.integral<32, true, false, 31 : 0, int>, subroutine_kind = 0 : i32, system_library_cell = "work.top", system_scope_path = "top", system_scope_symbol = @s1.$root::@s3.top::@s4.top, system_scope_time_precision_fs = 1000000000 : i64, system_scope_time_unit_fs = 1000000000000 : i64, system_time_scope_path = "top", target_time_precision_fs = 10 : i64, target_time_unit_fs = 100000 : i64} {
                    talos.sv.expression.arbitrary_symbol attributes {is_signed = false, node_id = 14 : i64, referenced_path = "$unit", referenced_symbol = @s1.$root::@s2, semantic_type = !talos.void} {
                    }
                  }
                }
              }
              talos.sv.statement.expression_statement attributes {node_id = 15 : i64} {
                talos.sv.expression.assignment attributes {assignment_kind = 0 : i32, is_signed = true, node_id = 16 : i64, semantic_type = !talos.integral<32, true, false, 31 : 0, int>} {
                  talos.sv.expression.named_value attributes {is_signed = true, node_id = 17 : i64, referenced_path = "top.p", referenced_symbol = @s1.$root::@s3.top::@s4.top::@s6.p, semantic_type = !talos.integral<32, true, false, 31 : 0, int>} {
                  }
                  talos.sv.expression.call attributes {argument_count = 1 : i64, callee_name = "$timeprecision", constraint_restrictions = [], defaulted_arguments = array<i64: 0>, has_inline_constraints = false, has_iterator_expression = false, has_output_arguments = false, has_this_class = false, is_signed = true, is_super_class = false, is_system_call = true, node_id = 18 : i64, semantic_type = !talos.integral<32, true, false, 31 : 0, int>, subroutine_kind = 0 : i32, system_library_cell = "work.top", system_scope_path = "top", system_scope_symbol = @s1.$root::@s3.top::@s4.top, system_scope_time_precision_fs = 1000000000 : i64, system_scope_time_unit_fs = 1000000000000 : i64, system_time_scope_path = "top", target_time_precision_fs = 10 : i64, target_time_unit_fs = 100000 : i64} {
                    talos.sv.expression.arbitrary_symbol attributes {is_signed = false, node_id = 19 : i64, referenced_path = "$unit", referenced_symbol = @s1.$root::@s2, semantic_type = !talos.void} {
                    }
                  }
                }
              }
              talos.sv.statement.expression_statement attributes {node_id = 20 : i64} {
                talos.sv.expression.call attributes {argument_count = 1 : i64, callee_name = "$printtimescale", constraint_restrictions = [], defaulted_arguments = array<i64: 0>, has_inline_constraints = false, has_iterator_expression = false, has_output_arguments = false, has_this_class = false, is_signed = false, is_super_class = false, is_system_call = true, node_id = 21 : i64, semantic_type = !talos.void, subroutine_kind = 1 : i32, system_library_cell = "work.top", system_scope_path = "top", system_scope_symbol = @s1.$root::@s3.top::@s4.top, system_scope_time_precision_fs = 1000000000 : i64, system_scope_time_unit_fs = 1000000000000 : i64, system_time_scope_path = "top", target_time_precision_fs = 10 : i64, target_time_unit_fs = 100000 : i64} {
                  talos.sv.expression.arbitrary_symbol attributes {is_signed = false, node_id = 22 : i64, referenced_path = "$unit", referenced_symbol = @s1.$root::@s2, semantic_type = !talos.void} {
                  }
                }
              }
              talos.sv.statement.expression_statement attributes {node_id = 23 : i64} {
                talos.sv.expression.call attributes {argument_count = 1 : i64, callee_name = "$printtimescale", constraint_restrictions = [], defaulted_arguments = array<i64: 0>, has_inline_constraints = false, has_iterator_expression = false, has_output_arguments = false, has_this_class = false, is_signed = false, is_super_class = false, is_system_call = true, node_id = 24 : i64, semantic_type = !talos.void, subroutine_kind = 1 : i32, system_library_cell = "work.top", system_scope_path = "top", system_scope_symbol = @s1.$root::@s3.top::@s4.top, system_scope_time_precision_fs = 1000000000 : i64, system_scope_time_unit_fs = 1000000000000 : i64, system_time_scope_path = "top"} {
                  talos.sv.expression.arbitrary_symbol attributes {is_signed = false, node_id = 25 : i64, referenced_path = "\\$root ", referenced_symbol = @s1.$root, semantic_type = !talos.void} {
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

