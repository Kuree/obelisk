// RUN: talos-opt %s '--lower-talos-to-sim=opt-level=0' | FileCheck %s

// IEEE 1800-2023 21.3.4.3: the str argument of $sscanf may be an unpacked
// array of byte, read as its text from the left bound to the right bound.
// A fixed array packs its elements; a dynamic array or queue is rendered by
// the runtime as %s renders it.
//
//   module top;
//     int n, m, v; byte f[2]; byte d[];
//     initial begin
//       n = $sscanf(f, "%d", v);
//       m = $sscanf(d, "%d", v);
//     end
//   endmodule

// CHECK-LABEL: simulation.func private @unit_0(
// CHECK: %[[PACKED:.*]] = simulation.logic.concat %{{.*}}, %{{.*}} : (!simulation.logic<8>, !simulation.logic<8>) -> !simulation.logic<16>
// CHECK: %[[FIXED:.*]] = simulation.string.from_packed %[[PACKED]]
// CHECK: simulation.string.scan_field %[[FIXED]],
// CHECK: %[[DYNAMIC:.*]] = simulation.ref.load %arg5
// CHECK: %[[TEXT:.*]] = simulation.string.output_format %arg0(%{{.*}}, %[[DYNAMIC]]) radix = <decimal> flags = [32, 16] :
// CHECK: simulation.string.scan_field %[[TEXT]],

module attributes {coverage.language_version = 2023 : i32} {
  talos.sv.symbol.definition @s0.top attributes {definition_kind = 0 : i32, hierarchical_name = "top", name = "top", node_id = 0 : i64} {
  }
  talos.sv.symbol.root @s1.$root attributes {hierarchical_name = "\\$root ", name = "$root", node_id = 1 : i64} {
    talos.sv.symbol.compilation_unit @s2 attributes {hierarchical_name = "$unit", node_id = 2 : i64, simulation.vpi_definition_name = "$unit"} {
    }
    talos.sv.symbol.instance @s3.top attributes {hierarchical_name = "top", is_uninstantiated = false, name = "top", node_id = 3 : i64, referenced_path = "top", referenced_symbol = @s0.top} {
      talos.sv.symbol.instance_body @s4.top attributes {hierarchical_name = "top", name = "top", node_id = 4 : i64, simulation.vpi_automatic = false, simulation.vpi_cell_instance = false, simulation.vpi_definition_name = "top", simulation.vpi_top = true, time_precision_fs = 1000000 : i64, time_unit_fs = 1000000 : i64, vpi_scope_kind = 32 : i32} {
        talos.sv.symbol.variable @s5.n attributes {hierarchical_name = "top.n", lifetime = 1 : i32, name = "n", node_id = 5 : i64, semantic_type = !talos.integral<32, true, false, 31 : 0, int>} {
        }
        talos.sv.symbol.variable @s6.m attributes {hierarchical_name = "top.m", lifetime = 1 : i32, name = "m", node_id = 6 : i64, semantic_type = !talos.integral<32, true, false, 31 : 0, int>} {
        }
        talos.sv.symbol.variable @s7.v attributes {hierarchical_name = "top.v", lifetime = 1 : i32, name = "v", node_id = 7 : i64, semantic_type = !talos.integral<32, true, false, 31 : 0, int>} {
        }
        talos.sv.symbol.variable @s8.f attributes {hierarchical_name = "top.f", lifetime = 1 : i32, name = "f", node_id = 8 : i64, semantic_type = !talos.ranged_unpacked_array<0 : 1 x !talos.integral<8, true, false, 7 : 0, byte>>} {
        }
        talos.sv.symbol.variable @s9.d attributes {hierarchical_name = "top.d", lifetime = 1 : i32, name = "d", node_id = 9 : i64, semantic_type = !talos.dynarray<!talos.integral<8, true, false, 7 : 0, byte>>} {
        }
        talos.sv.symbol.procedural_block @s10 attributes {hierarchical_name = "top", node_id = 10 : i64, procedure_kind = 0 : i32, time_precision_fs = 1000000 : i64, time_unit_fs = 1000000 : i64} {
          talos.sv.statement.block attributes {node_id = 11 : i64} {
            talos.sv.statement.list attributes {node_id = 12 : i64} {
              talos.sv.statement.expression_statement attributes {node_id = 13 : i64} {
                talos.sv.expression.assignment attributes {assignment_kind = 0 : i32, is_signed = true, node_id = 14 : i64, semantic_type = !talos.integral<32, true, false, 31 : 0, int>} {
                  talos.sv.expression.named_value attributes {is_signed = true, node_id = 15 : i64, referenced_path = "top.n", referenced_symbol = @s1.$root::@s3.top::@s4.top::@s5.n, semantic_type = !talos.integral<32, true, false, 31 : 0, int>} {
                  }
                  talos.sv.expression.conversion attributes {is_implicit = true, is_signed = true, node_id = 16 : i64, semantic_type = !talos.integral<32, true, false, 31 : 0, int>} {
                    talos.sv.expression.call attributes {argument_count = 3 : i64, callee_name = "$sscanf", constraint_restrictions = [], defaulted_arguments = array<i64: 0, 0, 0>, has_inline_constraints = false, has_iterator_expression = false, has_output_arguments = true, has_this_class = false, is_signed = true, is_super_class = false, is_system_call = true, node_id = 17 : i64, semantic_type = !talos.integral<32, true, true, 31 : 0, integer>, subroutine_kind = 0 : i32, system_library_cell = "work.top", system_scope_path = "top", system_scope_symbol = @s1.$root::@s3.top::@s4.top} {
                      talos.sv.expression.named_value attributes {is_signed = false, node_id = 18 : i64, referenced_path = "top.f", referenced_symbol = @s1.$root::@s3.top::@s4.top::@s8.f, semantic_type = !talos.ranged_unpacked_array<0 : 1 x !talos.integral<8, true, false, 7 : 0, byte>>} {
                      }
                      talos.sv.expression.string_literal attributes {constant_value = "%d", is_signed = false, node_id = 19 : i64, semantic_type = !talos.ranged_packed_array<15 : 0 x !talos.integral<1, false, false, 0 : 0, bit>>} {
                      }
                      talos.sv.expression.assignment attributes {assignment_kind = 0 : i32, is_signed = true, node_id = 20 : i64, semantic_type = !talos.integral<32, true, false, 31 : 0, int>} {
                        talos.sv.expression.named_value attributes {is_signed = true, node_id = 21 : i64, referenced_path = "top.v", referenced_symbol = @s1.$root::@s3.top::@s4.top::@s7.v, semantic_type = !talos.integral<32, true, false, 31 : 0, int>} {
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
                  talos.sv.expression.named_value attributes {is_signed = true, node_id = 25 : i64, referenced_path = "top.m", referenced_symbol = @s1.$root::@s3.top::@s4.top::@s6.m, semantic_type = !talos.integral<32, true, false, 31 : 0, int>} {
                  }
                  talos.sv.expression.conversion attributes {is_implicit = true, is_signed = true, node_id = 26 : i64, semantic_type = !talos.integral<32, true, false, 31 : 0, int>} {
                    talos.sv.expression.call attributes {argument_count = 3 : i64, callee_name = "$sscanf", constraint_restrictions = [], defaulted_arguments = array<i64: 0, 0, 0>, has_inline_constraints = false, has_iterator_expression = false, has_output_arguments = true, has_this_class = false, is_signed = true, is_super_class = false, is_system_call = true, node_id = 27 : i64, semantic_type = !talos.integral<32, true, true, 31 : 0, integer>, subroutine_kind = 0 : i32, system_library_cell = "work.top", system_scope_path = "top", system_scope_symbol = @s1.$root::@s3.top::@s4.top} {
                      talos.sv.expression.named_value attributes {is_signed = false, node_id = 28 : i64, referenced_path = "top.d", referenced_symbol = @s1.$root::@s3.top::@s4.top::@s9.d, semantic_type = !talos.dynarray<!talos.integral<8, true, false, 7 : 0, byte>>} {
                      }
                      talos.sv.expression.string_literal attributes {constant_value = "%d", is_signed = false, node_id = 29 : i64, semantic_type = !talos.ranged_packed_array<15 : 0 x !talos.integral<1, false, false, 0 : 0, bit>>} {
                      }
                      talos.sv.expression.assignment attributes {assignment_kind = 0 : i32, is_signed = true, node_id = 30 : i64, semantic_type = !talos.integral<32, true, false, 31 : 0, int>} {
                        talos.sv.expression.named_value attributes {is_signed = true, node_id = 31 : i64, referenced_path = "top.v", referenced_symbol = @s1.$root::@s3.top::@s4.top::@s7.v, semantic_type = !talos.integral<32, true, false, 31 : 0, int>} {
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

