// RUN: talos-opt %s '--lower-talos-to-sim=opt-level=0' | FileCheck %s

// IEEE 1800-2023 21.3.4.3: when the str argument or the format string of
// $sscanf has an unknown bit, the call returns EOF (-1) without scanning or
// writing any destination.
//
//   module top;
//     int value, n, m; logic [7:0] src; logic [15:0] fmt;
//     initial begin
//       n = $sscanf(src, "%d", value);
//       m = $sscanf("1", fmt, value);
//     end
//   endmodule

// CHECK-LABEL: simulation.func private @unit_0(
// CHECK: %[[EOF:.*]] = arith.constant -1 : i32
// CHECK: %[[SOURCE_COUNT:.*]] = simulation.logic.count_bits %{{.*}} matching %{{.*}}, %{{.*}} : (!simulation.logic<8>,
// CHECK: %[[SOURCE_UNKNOWN:.*]] = arith.cmpi ne, %[[SOURCE_COUNT]],
// CHECK: cf.cond_br %[[SOURCE_UNKNOWN]], ^[[N:bb[0-9]+]](%[[EOF]] : i32), ^[[SCAN:bb[0-9]+]]
// CHECK: ^[[N]](%[[N_VALUE:.*]]: i32):
// CHECK-NEXT: simulation.ref.store %[[N_VALUE]] to %arg2
// CHECK: %[[FORMAT_COUNT:.*]] = simulation.logic.count_bits %{{.*}} matching %{{.*}}, %{{.*}} : (!simulation.logic<16>,
// CHECK: %[[FORMAT_UNKNOWN:.*]] = arith.cmpi ne, %[[FORMAT_COUNT]],
// CHECK: cf.cond_br %[[FORMAT_UNKNOWN]], ^[[M:bb[0-9]+]](%{{.*}} : i32), ^{{bb[0-9]+}}
// CHECK: ^[[SCAN]]:
// CHECK-NOT: {{\^bb[0-9]+:}}
// CHECK: simulation.string.scan_field
// CHECK: ^[[M]](%[[M_VALUE:.*]]: i32):
// CHECK-NEXT: simulation.ref.store %[[M_VALUE]] to %arg3

module attributes {coverage.language_version = 2023 : i32} {
  talos.sv.symbol.definition @s0.top attributes {definition_kind = 0 : i32, hierarchical_name = "top", name = "top", node_id = 0 : i64} {
  }
  talos.sv.symbol.root @s1.$root attributes {hierarchical_name = "\\$root ", name = "$root", node_id = 1 : i64} {
    talos.sv.symbol.compilation_unit @s2 attributes {hierarchical_name = "$unit", node_id = 2 : i64, simulation.vpi_definition_name = "$unit"} {
    }
    talos.sv.symbol.instance @s3.top attributes {hierarchical_name = "top", is_uninstantiated = false, name = "top", node_id = 3 : i64, referenced_path = "top", referenced_symbol = @s0.top} {
      talos.sv.symbol.instance_body @s4.top attributes {hierarchical_name = "top", name = "top", node_id = 4 : i64, simulation.vpi_automatic = false, simulation.vpi_cell_instance = false, simulation.vpi_definition_name = "top", simulation.vpi_top = true, time_precision_fs = 1000000 : i64, time_unit_fs = 1000000 : i64, vpi_scope_kind = 32 : i32} {
        talos.sv.symbol.variable @s5.value attributes {hierarchical_name = "top.value", lifetime = 1 : i32, name = "value", node_id = 5 : i64, semantic_type = !talos.integral<32, true, false, 31 : 0, int>} {
        }
        talos.sv.symbol.variable @s6.n attributes {hierarchical_name = "top.n", lifetime = 1 : i32, name = "n", node_id = 6 : i64, semantic_type = !talos.integral<32, true, false, 31 : 0, int>} {
        }
        talos.sv.symbol.variable @s7.m attributes {hierarchical_name = "top.m", lifetime = 1 : i32, name = "m", node_id = 7 : i64, semantic_type = !talos.integral<32, true, false, 31 : 0, int>} {
        }
        talos.sv.symbol.variable @s8.src attributes {hierarchical_name = "top.src", lifetime = 1 : i32, name = "src", node_id = 8 : i64, semantic_type = !talos.ranged_packed_array<7 : 0 x !talos.integral<1, false, true, 0 : 0, logic>>} {
        }
        talos.sv.symbol.variable @s9.fmt attributes {hierarchical_name = "top.fmt", lifetime = 1 : i32, name = "fmt", node_id = 9 : i64, semantic_type = !talos.ranged_packed_array<15 : 0 x !talos.integral<1, false, true, 0 : 0, logic>>} {
        }
        talos.sv.symbol.procedural_block @s10 attributes {hierarchical_name = "top", node_id = 10 : i64, procedure_kind = 0 : i32, time_precision_fs = 1000000 : i64, time_unit_fs = 1000000 : i64} {
          talos.sv.statement.block attributes {node_id = 11 : i64} {
            talos.sv.statement.list attributes {node_id = 12 : i64} {
              talos.sv.statement.expression_statement attributes {node_id = 13 : i64} {
                talos.sv.expression.assignment attributes {assignment_kind = 0 : i32, is_signed = true, node_id = 14 : i64, semantic_type = !talos.integral<32, true, false, 31 : 0, int>} {
                  talos.sv.expression.named_value attributes {is_signed = true, node_id = 15 : i64, referenced_path = "top.n", referenced_symbol = @s1.$root::@s3.top::@s4.top::@s6.n, semantic_type = !talos.integral<32, true, false, 31 : 0, int>} {
                  }
                  talos.sv.expression.conversion attributes {is_implicit = true, is_signed = true, node_id = 16 : i64, semantic_type = !talos.integral<32, true, false, 31 : 0, int>} {
                    talos.sv.expression.call attributes {argument_count = 3 : i64, callee_name = "$sscanf", constraint_restrictions = [], defaulted_arguments = array<i64: 0, 0, 0>, has_inline_constraints = false, has_iterator_expression = false, has_output_arguments = true, has_this_class = false, is_signed = true, is_super_class = false, is_system_call = true, node_id = 17 : i64, semantic_type = !talos.integral<32, true, true, 31 : 0, integer>, subroutine_kind = 0 : i32, system_library_cell = "work.top", system_scope_path = "top", system_scope_symbol = @s1.$root::@s3.top::@s4.top} {
                      talos.sv.expression.named_value attributes {is_signed = false, node_id = 18 : i64, referenced_path = "top.src", referenced_symbol = @s1.$root::@s3.top::@s4.top::@s8.src, semantic_type = !talos.ranged_packed_array<7 : 0 x !talos.integral<1, false, true, 0 : 0, logic>>} {
                      }
                      talos.sv.expression.string_literal attributes {constant_value = "%d", is_signed = false, node_id = 19 : i64, semantic_type = !talos.ranged_packed_array<15 : 0 x !talos.integral<1, false, false, 0 : 0, bit>>} {
                      }
                      talos.sv.expression.assignment attributes {assignment_kind = 0 : i32, is_signed = true, node_id = 20 : i64, semantic_type = !talos.integral<32, true, false, 31 : 0, int>} {
                        talos.sv.expression.named_value attributes {is_signed = true, node_id = 21 : i64, referenced_path = "top.value", referenced_symbol = @s1.$root::@s3.top::@s4.top::@s5.value, semantic_type = !talos.integral<32, true, false, 31 : 0, int>} {
                        }
                        talos.sv.expression.empty_argument attributes {is_signed = true, node_id = 22 : i64, semantic_type = !talos.integral<32, true, false, 31 : 0, int>} {
                        }
                      }
                    }
                  }
                }
              }
              talos.sv.statement.expression_statement attributes {node_id = 23 : i64} {
                talos.sv.expression.assignment attributes {assignment_kind = 0 : i32, is_signed = true, node_id = 24 : i64, semantic_type = !talos.integral<32, true, false, 31 : 0, int>} {
                  talos.sv.expression.named_value attributes {is_signed = true, node_id = 25 : i64, referenced_path = "top.m", referenced_symbol = @s1.$root::@s3.top::@s4.top::@s7.m, semantic_type = !talos.integral<32, true, false, 31 : 0, int>} {
                  }
                  talos.sv.expression.conversion attributes {is_implicit = true, is_signed = true, node_id = 26 : i64, semantic_type = !talos.integral<32, true, false, 31 : 0, int>} {
                    talos.sv.expression.call attributes {argument_count = 3 : i64, callee_name = "$sscanf", constraint_restrictions = [], defaulted_arguments = array<i64: 0, 0, 0>, has_inline_constraints = false, has_iterator_expression = false, has_output_arguments = true, has_this_class = false, is_signed = true, is_super_class = false, is_system_call = true, node_id = 27 : i64, semantic_type = !talos.integral<32, true, true, 31 : 0, integer>, subroutine_kind = 0 : i32, system_library_cell = "work.top", system_scope_path = "top", system_scope_symbol = @s1.$root::@s3.top::@s4.top} {
                      talos.sv.expression.string_literal attributes {constant_value = "1", is_signed = false, node_id = 28 : i64, semantic_type = !talos.ranged_packed_array<7 : 0 x !talos.integral<1, false, false, 0 : 0, bit>>} {
                      }
                      talos.sv.expression.named_value attributes {is_signed = false, node_id = 29 : i64, referenced_path = "top.fmt", referenced_symbol = @s1.$root::@s3.top::@s4.top::@s9.fmt, semantic_type = !talos.ranged_packed_array<15 : 0 x !talos.integral<1, false, true, 0 : 0, logic>>} {
                      }
                      talos.sv.expression.assignment attributes {assignment_kind = 0 : i32, is_signed = true, node_id = 30 : i64, semantic_type = !talos.integral<32, true, false, 31 : 0, int>} {
                        talos.sv.expression.named_value attributes {is_signed = true, node_id = 31 : i64, referenced_path = "top.value", referenced_symbol = @s1.$root::@s3.top::@s4.top::@s5.value, semantic_type = !talos.integral<32, true, false, 31 : 0, int>} {
                        }
                        talos.sv.expression.empty_argument attributes {is_signed = true, node_id = 32 : i64, semantic_type = !talos.integral<32, true, false, 31 : 0, int>} {
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
  }
}

