// RUN: talos-opt %s '--lower-talos-to-sim=opt-level=0' | FileCheck %s

// IEEE 1800-2023 6.24.1, 6.19.4: a cast to an enum converts its operand to
// the base type, and an unbased unsized literal assigned there fills every
// bit with its value (5.7.1). Slang leaves the literal self-determined (one
// bit) under the enum conversion, so the lowering must fill, not
// zero-extend:
//   typedef enum logic [2:0] {S0 = 3'd0, S7 = 3'd7} e_t;
//   a = e_t'('1); b = e_t'('x); c = e_t'('0);

// CHECK-DAG: %[[ZERO:.+]] = simulation.logic.constant 0 : i3, 0 : i3 : !simulation.logic<3>
// CHECK-DAG: %[[UNKNOWN:.+]] = simulation.logic.constant 0 : i3, -1 : i3 : !simulation.logic<3>
// CHECK-DAG: %[[ONES:.+]] = simulation.logic.constant -1 : i3, 0 : i3 : !simulation.logic<3>
// CHECK: simulation.ref.store %[[ONES]] to %arg1
// CHECK: simulation.ref.store %[[UNKNOWN]] to %arg2
// CHECK: simulation.ref.store %[[ZERO]] to %arg3

module attributes {coverage.language_version = 2023 : i32, talos.enum_domains = [{names = ["S0", "S7"], type = !talos.enum<"enum_cast_fill.e_t", !talos.ranged_packed_array<2 : 0 x !talos.integral<1, false, true, 0 : 0, logic>>>, values = ["3'b0", "3'b111"]}]} {
  talos.sv.symbol.definition @s0.enum_cast_fill attributes {definition_kind = 0 : i32, hierarchical_name = "enum_cast_fill", name = "enum_cast_fill", node_id = 0 : i64} {
  }
  talos.sv.symbol.root @s1.$root attributes {hierarchical_name = "\\$root ", name = "$root", node_id = 1 : i64} {
    talos.sv.symbol.compilation_unit @s2 attributes {hierarchical_name = "$unit", node_id = 2 : i64} {
    }
    talos.sv.symbol.instance @s3.enum_cast_fill attributes {hierarchical_name = "enum_cast_fill", is_uninstantiated = false, name = "enum_cast_fill", node_id = 3 : i64, referenced_path = "enum_cast_fill", referenced_symbol = @s0.enum_cast_fill} {
      talos.sv.symbol.instance_body @s4.enum_cast_fill attributes {hierarchical_name = "enum_cast_fill", name = "enum_cast_fill", node_id = 4 : i64, time_precision_fs = 1000000 : i64, time_unit_fs = 1000000 : i64, vpi_scope_kind = 32 : i32} {
        talos.sv.symbol.transparent_member @s5.S0 attributes {hierarchical_name = "enum_cast_fill.S0", name = "S0", node_id = 5 : i64} {
        }
        talos.sv.symbol.transparent_member @s6.S7 attributes {hierarchical_name = "enum_cast_fill.S7", name = "S7", node_id = 6 : i64} {
        }
        talos.sv.type.type_alias @s7.e_t attributes {hierarchical_name = "enum_cast_fill.e_t", name = "e_t", node_id = 7 : i64, semantic_type = !talos.enum<"enum_cast_fill.e_t", !talos.ranged_packed_array<2 : 0 x !talos.integral<1, false, true, 0 : 0, logic>>>, vpi_source_type_identity = 0 : i64, vpi_typedef_layers = [{aliases = [@s1.$root::@s4.enum_cast_fill::@s7.e_t], path = array<i64>}]} {
        }
        talos.sv.symbol.variable @s8.a attributes {hierarchical_name = "enum_cast_fill.a", lifetime = 1 : i32, name = "a", node_id = 8 : i64, semantic_type = !talos.enum<"enum_cast_fill.e_t", !talos.ranged_packed_array<2 : 0 x !talos.integral<1, false, true, 0 : 0, logic>>>, vpi_source_type_identity = 0 : i64, vpi_typedef_layers = [{aliases = [@s1.$root::@s4.enum_cast_fill::@s7.e_t], path = array<i64>}]} {
        }
        talos.sv.symbol.variable @s9.b attributes {hierarchical_name = "enum_cast_fill.b", lifetime = 1 : i32, name = "b", node_id = 9 : i64, semantic_type = !talos.enum<"enum_cast_fill.e_t", !talos.ranged_packed_array<2 : 0 x !talos.integral<1, false, true, 0 : 0, logic>>>, vpi_source_type_identity = 0 : i64, vpi_typedef_layers = [{aliases = [@s1.$root::@s4.enum_cast_fill::@s7.e_t], path = array<i64>}]} {
        }
        talos.sv.symbol.variable @s10.c attributes {hierarchical_name = "enum_cast_fill.c", lifetime = 1 : i32, name = "c", node_id = 10 : i64, semantic_type = !talos.enum<"enum_cast_fill.e_t", !talos.ranged_packed_array<2 : 0 x !talos.integral<1, false, true, 0 : 0, logic>>>, vpi_source_type_identity = 0 : i64, vpi_typedef_layers = [{aliases = [@s1.$root::@s4.enum_cast_fill::@s7.e_t], path = array<i64>}]} {
        }
        talos.sv.symbol.procedural_block @s11 attributes {hierarchical_name = "enum_cast_fill", node_id = 11 : i64, procedure_kind = 0 : i32, time_precision_fs = 1000000 : i64, time_unit_fs = 1000000 : i64} {
          talos.sv.statement.block attributes {node_id = 12 : i64} {
            talos.sv.statement.list attributes {node_id = 13 : i64} {
              talos.sv.statement.expression_statement attributes {node_id = 14 : i64} {
                talos.sv.expression.assignment attributes {assignment_kind = 0 : i32, is_signed = false, node_id = 15 : i64, semantic_type = !talos.enum<"enum_cast_fill.e_t", !talos.ranged_packed_array<2 : 0 x !talos.integral<1, false, true, 0 : 0, logic>>>, vpi_source_type_identity = 0 : i64, vpi_typedef_layers = [{aliases = [@s1.$root::@s4.enum_cast_fill::@s7.e_t], path = array<i64>}]} {
                  talos.sv.expression.named_value attributes {is_signed = false, node_id = 16 : i64, referenced_path = "enum_cast_fill.a", referenced_symbol = @s1.$root::@s3.enum_cast_fill::@s4.enum_cast_fill::@s8.a, semantic_type = !talos.enum<"enum_cast_fill.e_t", !talos.ranged_packed_array<2 : 0 x !talos.integral<1, false, true, 0 : 0, logic>>>, vpi_source_type_identity = 0 : i64, vpi_typedef_layers = [{aliases = [@s1.$root::@s4.enum_cast_fill::@s7.e_t], path = array<i64>}]} {
                  }
                  talos.sv.expression.conversion attributes {is_implicit = false, is_signed = false, node_id = 17 : i64, semantic_type = !talos.enum<"enum_cast_fill.e_t", !talos.ranged_packed_array<2 : 0 x !talos.integral<1, false, true, 0 : 0, logic>>>, vpi_source_type_identity = 0 : i64, vpi_typedef_layers = [{aliases = [@s1.$root::@s4.enum_cast_fill::@s7.e_t], path = array<i64>}]} {
                    talos.sv.expression.unbased_unsized_integer_literal attributes {constant_value = "1'b1", is_signed = false, node_id = 18 : i64, semantic_type = !talos.ranged_packed_array<0 : 0 x !talos.integral<1, false, false, 0 : 0, bit>>} {
                    }
                  }
                }
              }
              talos.sv.statement.expression_statement attributes {node_id = 19 : i64} {
                talos.sv.expression.assignment attributes {assignment_kind = 0 : i32, is_signed = false, node_id = 20 : i64, semantic_type = !talos.enum<"enum_cast_fill.e_t", !talos.ranged_packed_array<2 : 0 x !talos.integral<1, false, true, 0 : 0, logic>>>, vpi_source_type_identity = 0 : i64, vpi_typedef_layers = [{aliases = [@s1.$root::@s4.enum_cast_fill::@s7.e_t], path = array<i64>}]} {
                  talos.sv.expression.named_value attributes {is_signed = false, node_id = 21 : i64, referenced_path = "enum_cast_fill.b", referenced_symbol = @s1.$root::@s3.enum_cast_fill::@s4.enum_cast_fill::@s9.b, semantic_type = !talos.enum<"enum_cast_fill.e_t", !talos.ranged_packed_array<2 : 0 x !talos.integral<1, false, true, 0 : 0, logic>>>, vpi_source_type_identity = 0 : i64, vpi_typedef_layers = [{aliases = [@s1.$root::@s4.enum_cast_fill::@s7.e_t], path = array<i64>}]} {
                  }
                  talos.sv.expression.conversion attributes {is_implicit = false, is_signed = false, node_id = 22 : i64, semantic_type = !talos.enum<"enum_cast_fill.e_t", !talos.ranged_packed_array<2 : 0 x !talos.integral<1, false, true, 0 : 0, logic>>>, vpi_source_type_identity = 0 : i64, vpi_typedef_layers = [{aliases = [@s1.$root::@s4.enum_cast_fill::@s7.e_t], path = array<i64>}]} {
                    talos.sv.expression.unbased_unsized_integer_literal attributes {constant_value = "1'bx", is_signed = false, node_id = 23 : i64, semantic_type = !talos.ranged_packed_array<0 : 0 x !talos.integral<1, false, true, 0 : 0, logic>>} {
                    }
                  }
                }
              }
              talos.sv.statement.expression_statement attributes {node_id = 24 : i64} {
                talos.sv.expression.assignment attributes {assignment_kind = 0 : i32, is_signed = false, node_id = 25 : i64, semantic_type = !talos.enum<"enum_cast_fill.e_t", !talos.ranged_packed_array<2 : 0 x !talos.integral<1, false, true, 0 : 0, logic>>>, vpi_source_type_identity = 0 : i64, vpi_typedef_layers = [{aliases = [@s1.$root::@s4.enum_cast_fill::@s7.e_t], path = array<i64>}]} {
                  talos.sv.expression.named_value attributes {is_signed = false, node_id = 26 : i64, referenced_path = "enum_cast_fill.c", referenced_symbol = @s1.$root::@s3.enum_cast_fill::@s4.enum_cast_fill::@s10.c, semantic_type = !talos.enum<"enum_cast_fill.e_t", !talos.ranged_packed_array<2 : 0 x !talos.integral<1, false, true, 0 : 0, logic>>>, vpi_source_type_identity = 0 : i64, vpi_typedef_layers = [{aliases = [@s1.$root::@s4.enum_cast_fill::@s7.e_t], path = array<i64>}]} {
                  }
                  talos.sv.expression.conversion attributes {is_implicit = false, is_signed = false, node_id = 27 : i64, semantic_type = !talos.enum<"enum_cast_fill.e_t", !talos.ranged_packed_array<2 : 0 x !talos.integral<1, false, true, 0 : 0, logic>>>, vpi_source_type_identity = 0 : i64, vpi_typedef_layers = [{aliases = [@s1.$root::@s4.enum_cast_fill::@s7.e_t], path = array<i64>}]} {
                    talos.sv.expression.unbased_unsized_integer_literal attributes {constant_value = "1'b0", is_signed = false, node_id = 28 : i64, semantic_type = !talos.ranged_packed_array<0 : 0 x !talos.integral<1, false, false, 0 : 0, bit>>} {
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

