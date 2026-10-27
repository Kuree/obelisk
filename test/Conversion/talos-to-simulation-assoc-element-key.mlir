// RUN: talos-opt %s --lower-talos-to-sim | FileCheck %s
// Lowered simulation IR has to re-parse, so the round trip is part of the
// contract.
// RUN: talos-opt %s --lower-talos-to-sim | talos-opt | FileCheck %s

// IEEE 1800-2023 7.8.5: an associative array indexed by an unpacked structure
// or a dynamic array keys by value through the runtime's element key kind. A
// traversed index whose type holds a container is copied, since the array's
// stored key keeps its own members.
//
//   module top;
//     typedef struct {int v;} K;
//     typedef int D[];
//     int a[K];
//     int d[D];
//     initial begin
//       D x;
//       a['{0}] = 0;
//       void'(d.first(x));
//     end
//   endmodule

// CHECK: simulation.assoc.create {{.*}}key_kind = #simulation.assoc_key_kind<element>, key_width = 32 : i64{{.*}} -> !simulation.assoc_array<!simulation.unpacked_struct<[#simulation.field<name = "v", type = i32
// CHECK: simulation.assoc.write
// CHECK: simulation.assoc.create {{.*}}key_kind = #simulation.assoc_key_kind<element>, key_width = 64 : i64{{.*}} -> !simulation.assoc_array<!simulation.dynamic_array<i32>, i32, false, false>
// CHECK: %[[KEY:.*]], %{{.*}} = simulation.assoc.traverse
// CHECK-NEXT: simulation.container.clone %[[KEY]]

module attributes {coverage.language_version = 2023 : i32} {
  talos.sv.symbol.definition @s0.top attributes {definition_kind = 0 : i32, hierarchical_name = "top", name = "top", node_id = 0 : i64} {
  }
  talos.sv.symbol.root @s1.$root attributes {hierarchical_name = "\\$root ", name = "$root", node_id = 1 : i64} {
    talos.sv.symbol.compilation_unit @s2 attributes {hierarchical_name = "$unit", node_id = 2 : i64, simulation.vpi_definition_name = "$unit"} {
    }
    talos.sv.symbol.instance @s3.top attributes {hierarchical_name = "top", is_uninstantiated = false, name = "top", node_id = 3 : i64, referenced_path = "top", referenced_symbol = @s0.top} {
      talos.sv.symbol.instance_body @s4.top attributes {hierarchical_name = "top", name = "top", node_id = 4 : i64, simulation.vpi_automatic = false, simulation.vpi_cell_instance = false, simulation.vpi_definition_name = "top", simulation.vpi_top = true, time_precision_fs = 1000000 : i64, time_unit_fs = 1000000 : i64, vpi_scope_kind = 32 : i32} {
        talos.sv.type.type_alias @s5.K attributes {hierarchical_name = "top.K", name = "K", node_id = 5 : i64, semantic_type = !talos.source_aggregate<"top", false, false, false, false, false, false, 0, 32, 32, 0, [{name = "v", ordinal = 0 : i32, packed_offset = 0 : i64, rand_mode = 0 : i32, type = !talos.integral<32, true, false, 31 : 0, int>}]>, vpi_source_type_identity = 0 : i64, vpi_typedef_layers = [{aliases = [@s1.$root::@s4.top::@s5.K], path = array<i64>}]} {
        }
        talos.sv.type.type_alias @s6.D attributes {hierarchical_name = "top.D", name = "D", node_id = 6 : i64, semantic_type = !talos.dynarray<!talos.integral<32, true, false, 31 : 0, int>>, vpi_source_type_identity = 1 : i64, vpi_typedef_layers = [{aliases = [@s1.$root::@s4.top::@s6.D], path = array<i64>}]} {
        }
        talos.sv.symbol.variable @s7.a attributes {hierarchical_name = "top.a", lifetime = 1 : i32, name = "a", node_id = 7 : i64, semantic_type = !talos.assoc<!talos.source_aggregate<"top", false, false, false, false, false, false, 0, 32, 32, 0, [{name = "v", ordinal = 0 : i32, packed_offset = 0 : i64, rand_mode = 0 : i32, type = !talos.integral<32, true, false, 31 : 0, int>}]>, !talos.integral<32, true, false, 31 : 0, int>, false>, vpi_typedef_layers = [{aliases = [@s1.$root::@s4.top::@s5.K], path = array<i64: 0>}]} {
        }
        talos.sv.symbol.variable @s8.d attributes {hierarchical_name = "top.d", lifetime = 1 : i32, name = "d", node_id = 8 : i64, semantic_type = !talos.assoc<!talos.dynarray<!talos.integral<32, true, false, 31 : 0, int>>, !talos.integral<32, true, false, 31 : 0, int>, false>, vpi_typedef_layers = [{aliases = [@s1.$root::@s4.top::@s6.D], path = array<i64: 0>}]} {
        }
        talos.sv.symbol.statement_block @s9 attributes {block_kind = 0 : i32, hierarchical_name = "top", node_id = 9 : i64} {
          talos.sv.symbol.variable @s10.x attributes {hierarchical_name = "top.x", lifetime = 1 : i32, name = "x", node_id = 10 : i64, semantic_type = !talos.dynarray<!talos.integral<32, true, false, 31 : 0, int>>, vpi_typedef_layers = [{aliases = [@s1.$root::@s4.top::@s6.D], path = array<i64>}]} {
          }
        }
        talos.sv.symbol.procedural_block @s11 attributes {hierarchical_name = "top", node_id = 11 : i64, procedure_kind = 0 : i32, time_precision_fs = 1000000 : i64, time_unit_fs = 1000000 : i64} {
          talos.sv.statement.block attributes {node_id = 12 : i64} {
            talos.sv.statement.list attributes {node_id = 13 : i64} {
              talos.sv.statement.variable_declaration attributes {node_id = 14 : i64, referenced_path = "top.x", referenced_symbol = @s1.$root::@s3.top::@s4.top::@s9::@s10.x} {
              }
              talos.sv.statement.expression_statement attributes {node_id = 15 : i64} {
                talos.sv.expression.assignment attributes {assignment_kind = 0 : i32, is_signed = true, node_id = 16 : i64, semantic_type = !talos.integral<32, true, false, 31 : 0, int>} {
                  talos.sv.expression.element_select attributes {is_signed = true, node_id = 17 : i64, semantic_type = !talos.integral<32, true, false, 31 : 0, int>} {
                    talos.sv.expression.named_value attributes {is_signed = false, node_id = 18 : i64, referenced_path = "top.a", referenced_symbol = @s1.$root::@s3.top::@s4.top::@s7.a, semantic_type = !talos.assoc<!talos.source_aggregate<"top", false, false, false, false, false, false, 0, 32, 32, 0, [{name = "v", ordinal = 0 : i32, packed_offset = 0 : i64, rand_mode = 0 : i32, type = !talos.integral<32, true, false, 31 : 0, int>}]>, !talos.integral<32, true, false, 31 : 0, int>, false>, vpi_typedef_layers = [{aliases = [@s1.$root::@s4.top::@s5.K], path = array<i64: 0>}]} {
                    }
                    talos.sv.expression.simple_assignment_pattern attributes {is_signed = false, node_id = 19 : i64, semantic_type = !talos.source_aggregate<"top", false, false, false, false, false, false, 0, 32, 32, 0, [{name = "v", ordinal = 0 : i32, packed_offset = 0 : i64, rand_mode = 0 : i32, type = !talos.integral<32, true, false, 31 : 0, int>}]>, vpi_typedef_layers = [{aliases = [@s1.$root::@s4.top::@s5.K], path = array<i64>}]} {
                      talos.sv.expression.integer_literal attributes {constant_value = "0", is_declared_unsized = true, is_signed = true, node_id = 20 : i64, semantic_type = !talos.integral<32, true, false, 31 : 0, int>} {
                      }
                    }
                  }
                  talos.sv.expression.integer_literal attributes {constant_value = "0", is_declared_unsized = true, is_signed = true, node_id = 21 : i64, semantic_type = !talos.integral<32, true, false, 31 : 0, int>} {
                  }
                }
              }
              talos.sv.statement.expression_statement attributes {node_id = 22 : i64} {
                talos.sv.expression.call attributes {argument_count = 2 : i64, callee_name = "first", constraint_restrictions = [], defaulted_arguments = array<i64: 0, 0>, has_inline_constraints = false, has_iterator_expression = false, has_output_arguments = true, has_this_class = false, is_signed = true, is_super_class = false, is_system_call = true, is_void_casted, node_id = 23 : i64, semantic_type = !talos.integral<32, true, false, 31 : 0, int>, subroutine_kind = 0 : i32, system_library_cell = "work.top", system_scope_path = "top", system_scope_symbol = @s1.$root::@s3.top::@s4.top::@s9} {
                  talos.sv.expression.named_value attributes {is_signed = false, node_id = 24 : i64, referenced_path = "top.d", referenced_symbol = @s1.$root::@s3.top::@s4.top::@s8.d, semantic_type = !talos.assoc<!talos.dynarray<!talos.integral<32, true, false, 31 : 0, int>>, !talos.integral<32, true, false, 31 : 0, int>, false>, vpi_typedef_layers = [{aliases = [@s1.$root::@s4.top::@s6.D], path = array<i64: 0>}]} {
                  }
                  talos.sv.expression.named_value attributes {is_signed = false, node_id = 25 : i64, referenced_path = "top.x", referenced_symbol = @s1.$root::@s3.top::@s4.top::@s9::@s10.x, semantic_type = !talos.dynarray<!talos.integral<32, true, false, 31 : 0, int>>, vpi_typedef_layers = [{aliases = [@s1.$root::@s4.top::@s6.D], path = array<i64>}]} {
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

