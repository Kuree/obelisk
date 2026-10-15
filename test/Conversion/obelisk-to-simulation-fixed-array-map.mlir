// RUN: obelisk-opt %s --lower-obelisk-to-sim | FileCheck %s

// IEEE 1800-2023 7.12.5: map() on a fixed unpacked array returns a fixed array
// of the same range. Each iteration reads the source element and replaces the
// result element at the same declared index of the descending range 3:1.
//
//   module top;
//     int b[3:1];
//     initial b = b.map(x) with (x + x.index);
//   endmodule

// CHECK: %[[EMPTY:.*]] = simulation.aggregate.default : !simulation.unpacked_array<3 : 1 x i32>
// CHECK: cf.br ^[[HEADER:bb[0-9]+]](%{{.*}}, %[[EMPTY]] : i64, !simulation.unpacked_array<3 : 1 x i32>)
// CHECK: ^[[HEADER]](%[[ORDINAL:.*]]: i64, %[[MAPPED:.*]]: !simulation.unpacked_array<3 : 1 x i32>):
// CHECK: cf.cond_br %{{.*}}, ^{{bb[0-9]+}}, ^[[EXIT:bb[0-9]+]](%[[MAPPED]] : !simulation.unpacked_array<3 : 1 x i32>)
// CHECK: %[[INDEX:.*]] = arith.subi %{{.*}}, %[[ORDINAL]] : i64
// CHECK: simulation.array.extract_dynamic %{{.*}}[%[[INDEX]]]
// CHECK: %[[UPDATED:.*]] = simulation.array.insert_dynamic %{{.*}} into %[[MAPPED]][%[[INDEX]]]
// CHECK: cf.br ^[[HEADER]](%{{.*}}, %[[UPDATED]] : i64, !simulation.unpacked_array<3 : 1 x i32>)
// CHECK: ^[[EXIT]](%[[RESULT:.*]]: !simulation.unpacked_array<3 : 1 x i32>):
// CHECK: simulation.ref.store %[[RESULT]] to

module attributes {coverage.language_version = 2023 : i32} {
  obelisk.sv.symbol.definition @s0.top attributes {definition_kind = 0 : i32, hierarchical_name = "top", name = "top", node_id = 0 : i64} {
  }
  obelisk.sv.symbol.root @s1.$root attributes {hierarchical_name = "\\$root ", name = "$root", node_id = 1 : i64} {
    obelisk.sv.symbol.compilation_unit @s2 attributes {hierarchical_name = "$unit", node_id = 2 : i64, simulation.vpi_definition_name = "$unit"} {
    }
    obelisk.sv.symbol.instance @s3.top attributes {hierarchical_name = "top", is_uninstantiated = false, name = "top", node_id = 3 : i64, referenced_path = "top", referenced_symbol = @s0.top} {
      obelisk.sv.symbol.instance_body @s4.top attributes {hierarchical_name = "top", name = "top", node_id = 4 : i64, simulation.vpi_automatic = false, simulation.vpi_cell_instance = false, simulation.vpi_definition_name = "top", simulation.vpi_top = true, time_precision_fs = 1000000 : i64, time_unit_fs = 1000000 : i64, vpi_scope_kind = 32 : i32} {
        obelisk.sv.symbol.variable @s5.b attributes {hierarchical_name = "top.b", lifetime = 1 : i32, name = "b", node_id = 5 : i64, semantic_type = !obelisk.ranged_unpacked_array<3 : 1 x !obelisk.integral<32, true, false, 31 : 0, int>>} {
        }
        obelisk.sv.symbol.procedural_block @s6 attributes {hierarchical_name = "top", node_id = 6 : i64, procedure_kind = 0 : i32, time_precision_fs = 1000000 : i64, time_unit_fs = 1000000 : i64} {
          obelisk.sv.statement.expression_statement attributes {node_id = 7 : i64} {
            obelisk.sv.expression.assignment attributes {assignment_kind = 0 : i32, is_signed = false, node_id = 8 : i64, semantic_type = !obelisk.ranged_unpacked_array<3 : 1 x !obelisk.integral<32, true, false, 31 : 0, int>>} {
              obelisk.sv.expression.named_value attributes {is_signed = false, node_id = 9 : i64, referenced_path = "top.b", referenced_symbol = @s1.$root::@s3.top::@s4.top::@s5.b, semantic_type = !obelisk.ranged_unpacked_array<3 : 1 x !obelisk.integral<32, true, false, 31 : 0, int>>} {
              }
              obelisk.sv.expression.call attributes {argument_count = 1 : i64, callee_name = "map", constraint_restrictions = [], defaulted_arguments = array<i64: 0>, has_inline_constraints = false, has_iterator_expression = true, has_output_arguments = false, has_this_class = false, is_signed = false, is_super_class = false, is_system_call = true, iterator_variable_path = "top.x", iterator_variable_symbol = @s1.$root::@s3.top::@s4.top::@s7.x, node_id = 10 : i64, semantic_type = !obelisk.ranged_unpacked_array<3 : 1 x !obelisk.integral<32, true, false, 31 : 0, int>>, subroutine_kind = 0 : i32, system_library_cell = "work.top", system_scope_path = "top", system_scope_symbol = @s1.$root::@s3.top::@s4.top} {
                obelisk.sv.expression.binary_op attributes {is_signed = true, node_id = 11 : i64, operator_kind = 0 : i32, semantic_type = !obelisk.integral<32, true, false, 31 : 0, int>} {
                  obelisk.sv.expression.named_value attributes {is_signed = true, node_id = 12 : i64, referenced_path = "top.x", referenced_symbol = @s1.$root::@s3.top::@s4.top::@s7.x, semantic_type = !obelisk.integral<32, true, false, 31 : 0, int>} {
                  }
                  obelisk.sv.expression.call attributes {argument_count = 1 : i64, callee_name = "index", constraint_restrictions = [], defaulted_arguments = array<i64: 0>, has_inline_constraints = false, has_iterator_expression = false, has_output_arguments = false, has_this_class = false, is_signed = true, is_super_class = false, is_system_call = true, node_id = 13 : i64, semantic_type = !obelisk.integral<32, true, false, 31 : 0, int>, subroutine_kind = 0 : i32, system_library_cell = "work.top", system_scope_path = "top", system_scope_symbol = @s1.$root::@s3.top::@s4.top} {
                    obelisk.sv.expression.named_value attributes {is_signed = true, node_id = 14 : i64, referenced_path = "top.x", referenced_symbol = @s1.$root::@s3.top::@s4.top::@s7.x, semantic_type = !obelisk.integral<32, true, false, 31 : 0, int>} {
                    }
                  }
                }
                obelisk.sv.expression.named_value attributes {is_signed = false, node_id = 15 : i64, referenced_path = "top.b", referenced_symbol = @s1.$root::@s3.top::@s4.top::@s5.b, semantic_type = !obelisk.ranged_unpacked_array<3 : 1 x !obelisk.integral<32, true, false, 31 : 0, int>>} {
                }
              }
            }
          }
        }
        obelisk.sv.symbol.iterator @s7.x attributes {array_type = !obelisk.ranged_unpacked_array<3 : 1 x !obelisk.integral<32, true, false, 31 : 0, int>>, hierarchical_name = "top.x", index_method_name = "index", is_const, name = "x", node_id = 16 : i64, semantic_type = !obelisk.integral<32, true, false, 31 : 0, int>} {
        }
      }
    }
  }
}

