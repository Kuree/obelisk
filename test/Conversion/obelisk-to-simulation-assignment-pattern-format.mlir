// RUN: obelisk-opt %s '--lower-obelisk-to-sim=opt-level=0' | FileCheck %s

// IEEE 1800-2023 21.2.1.6: %p formats a singular operand as an element of an
// assignment pattern. A packed structure prints as a pattern with named
// elements, an enum by its name (also inside an aggregate and for a cast),
// a string or string literal in quotes and a null chandle as null. A queue of
// unpacked structures is rendered element by element, since the element
// names come from the source type.
//
//   typedef enum {ONE} E;
//   typedef struct packed {
//     bit [1:0] a;
//     bit b;
//   } P;
//   typedef struct {
//     E e;
//   } U;
//   module top;
//     P p;
//     U q[$];
//     string s;
//     chandle h;
//     initial $display("%p %p %p %p %p %p", p, q, s, "lit", h, E'(0));
//   endmodule

// CHECK-LABEL: simulation.func private @unit_0(
// CHECK: %[[QUOTED:.*]] = simulation.bytes.constant "\22%p\22"
// CHECK: %[[ELEMENT:.*]] = simulation.bytes.constant "'{e:%p}"
// CHECK: %[[PACKED:.*]] = simulation.bytes.constant "'{a:%p, b:%p}"
// CHECK: %[[P:.*]] = simulation.string.output_format %arg0(%[[PACKED]], %{{.*}}, %{{.*}}) radix = <decimal> flags = [32, 0, 0]
// CHECK: simulation.container.size
// CHECK: simulation.container.read
// CHECK: simulation.string.literal "ONE"
// CHECK: simulation.string.output_format %arg0(%{{.*}}, %{{.*}}, %{{.*}}) radix = <decimal> flags = [32, 1025]
// CHECK: %[[Q:.*]] = simulation.string.concat %{{.*}}, %{{.*}} : (!simulation.string, !simulation.string) -> !simulation.string
// CHECK: %[[S:.*]] = simulation.string.output_format %arg0(%{{.*}}, %{{.*}}) radix = <decimal> flags = [32, 8]
// CHECK: %[[LIT:.*]] = simulation.string.output_format %arg0(%{{.*}}, %{{.*}}) radix = <decimal> flags = [32, 8]
// CHECK: simulation.chandle.equal
// CHECK: simulation.string.literal "null"
// CHECK: %[[H:.*]] = arith.select
// CHECK: %[[CAST:.*]] = simulation.string.literal "ONE"
// CHECK: simulation.display %arg0 to %{{.*}}(%{{.*}}, %[[P]], %[[Q]], %[[S]], %[[LIT]], %[[H]], %{{.*}}, %[[CAST]]) newline = true radix = <decimal> flags = [0, 8, 8, 8, 8, 8, 1025]

module attributes {coverage.language_version = 2023 : i32, obelisk.enum_domains = [{names = ["ONE"], type = !obelisk.enum<"E", !obelisk.integral<32, true, false, 31 : 0, int>>, values = ["0"]}]} {
  obelisk.sv.symbol.definition @s0.top attributes {definition_kind = 0 : i32, hierarchical_name = "top", name = "top", node_id = 0 : i64} {
  }
  obelisk.sv.symbol.root @s1.$root attributes {hierarchical_name = "\\$root ", name = "$root", node_id = 1 : i64} {
    obelisk.sv.symbol.compilation_unit @s2 attributes {hierarchical_name = "$unit", node_id = 2 : i64, simulation.vpi_definition_name = "$unit"} {
      obelisk.sv.symbol.transparent_member @s3.ONE attributes {hierarchical_name = "ONE", name = "ONE", node_id = 3 : i64} {
      }
      obelisk.sv.type.type_alias @s4.E attributes {hierarchical_name = "E", name = "E", node_id = 4 : i64, semantic_type = !obelisk.enum<"E", !obelisk.integral<32, true, false, 31 : 0, int>>, vpi_source_type_identity = 0 : i64, vpi_typedef_layers = [{aliases = [@s1.$root::@s2::@s4.E], path = array<i64>}]} {
      }
      obelisk.sv.type.type_alias @s5.P attributes {hierarchical_name = "P", name = "P", node_id = 5 : i64, semantic_type = !obelisk.source_aggregate<"$unit", true, false, false, false, false, false, 3, 3, 3, 0, [{name = "a", ordinal = 0 : i32, packed_offset = 1 : i64, rand_mode = 0 : i32, type = !obelisk.ranged_packed_array<1 : 0 x !obelisk.integral<1, false, false, 0 : 0, bit>>}, {name = "b", ordinal = 1 : i32, packed_offset = 0 : i64, rand_mode = 0 : i32, type = !obelisk.integral<1, false, false, 0 : 0, bit>}]>, vpi_source_type_identity = 1 : i64, vpi_typedef_layers = [{aliases = [@s1.$root::@s2::@s5.P], path = array<i64>}]} {
      }
      obelisk.sv.type.type_alias @s6.U attributes {hierarchical_name = "U", name = "U", node_id = 6 : i64, semantic_type = !obelisk.source_aggregate<"$unit", false, false, false, false, false, false, 0, 32, 32, 0, [{name = "e", ordinal = 0 : i32, packed_offset = 0 : i64, rand_mode = 0 : i32, type = !obelisk.enum<"E", !obelisk.integral<32, true, false, 31 : 0, int>>}]>, vpi_source_type_identity = 2 : i64, vpi_typedef_layers = [{aliases = [@s1.$root::@s2::@s6.U], path = array<i64>}, {aliases = [@s1.$root::@s2::@s4.E], path = array<i64: 0>}]} {
      }
    }
    obelisk.sv.symbol.instance @s7.top attributes {hierarchical_name = "top", is_uninstantiated = false, name = "top", node_id = 7 : i64, referenced_path = "top", referenced_symbol = @s0.top} {
      obelisk.sv.symbol.instance_body @s8.top attributes {hierarchical_name = "top", name = "top", node_id = 8 : i64, simulation.vpi_automatic = false, simulation.vpi_cell_instance = false, simulation.vpi_definition_name = "top", simulation.vpi_top = true, time_precision_fs = 1000000 : i64, time_unit_fs = 1000000 : i64, vpi_scope_kind = 32 : i32} {
        obelisk.sv.symbol.variable @s9.p attributes {hierarchical_name = "top.p", lifetime = 1 : i32, name = "p", node_id = 9 : i64, semantic_type = !obelisk.source_aggregate<"$unit", true, false, false, false, false, false, 3, 3, 3, 0, [{name = "a", ordinal = 0 : i32, packed_offset = 1 : i64, rand_mode = 0 : i32, type = !obelisk.ranged_packed_array<1 : 0 x !obelisk.integral<1, false, false, 0 : 0, bit>>}, {name = "b", ordinal = 1 : i32, packed_offset = 0 : i64, rand_mode = 0 : i32, type = !obelisk.integral<1, false, false, 0 : 0, bit>}]>, vpi_typedef_layers = [{aliases = [@s1.$root::@s2::@s5.P], path = array<i64>}]} {
        }
        obelisk.sv.symbol.variable @s10.q attributes {hierarchical_name = "top.q", lifetime = 1 : i32, name = "q", node_id = 10 : i64, semantic_type = !obelisk.queue<!obelisk.source_aggregate<"$unit", false, false, false, false, false, false, 0, 32, 32, 0, [{name = "e", ordinal = 0 : i32, packed_offset = 0 : i64, rand_mode = 0 : i32, type = !obelisk.enum<"E", !obelisk.integral<32, true, false, 31 : 0, int>>}]>, 0>, vpi_typedef_layers = [{aliases = [@s1.$root::@s2::@s6.U], path = array<i64: 0>}, {aliases = [@s1.$root::@s2::@s4.E], path = array<i64: 0, 0>}]} {
        }
        obelisk.sv.symbol.variable @s11.s attributes {hierarchical_name = "top.s", lifetime = 1 : i32, name = "s", node_id = 11 : i64, semantic_type = !obelisk.string} {
        }
        obelisk.sv.symbol.variable @s12.h attributes {hierarchical_name = "top.h", lifetime = 1 : i32, name = "h", node_id = 12 : i64, semantic_type = !obelisk.chandle} {
        }
        obelisk.sv.symbol.procedural_block @s13 attributes {hierarchical_name = "top", node_id = 13 : i64, procedure_kind = 0 : i32, time_precision_fs = 1000000 : i64, time_unit_fs = 1000000 : i64} {
          obelisk.sv.statement.expression_statement attributes {node_id = 14 : i64} {
            obelisk.sv.expression.call attributes {argument_count = 7 : i64, callee_name = "$display", constraint_restrictions = [], defaulted_arguments = array<i64: 0, 0, 0, 0, 0, 0, 0>, has_inline_constraints = false, has_iterator_expression = false, has_output_arguments = false, has_this_class = false, is_signed = false, is_super_class = false, is_system_call = true, node_id = 15 : i64, semantic_type = !obelisk.void, subroutine_kind = 1 : i32, system_library_cell = "work.top", system_scope_path = "top", system_scope_symbol = @s1.$root::@s7.top::@s8.top} {
              obelisk.sv.expression.string_literal attributes {constant_value = "%p %p %p %p %p %p", is_signed = false, node_id = 16 : i64, semantic_type = !obelisk.ranged_packed_array<135 : 0 x !obelisk.integral<1, false, false, 0 : 0, bit>>} {
              }
              obelisk.sv.expression.named_value attributes {is_signed = false, node_id = 17 : i64, referenced_path = "top.p", referenced_symbol = @s1.$root::@s7.top::@s8.top::@s9.p, semantic_type = !obelisk.source_aggregate<"$unit", true, false, false, false, false, false, 3, 3, 3, 0, [{name = "a", ordinal = 0 : i32, packed_offset = 1 : i64, rand_mode = 0 : i32, type = !obelisk.ranged_packed_array<1 : 0 x !obelisk.integral<1, false, false, 0 : 0, bit>>}, {name = "b", ordinal = 1 : i32, packed_offset = 0 : i64, rand_mode = 0 : i32, type = !obelisk.integral<1, false, false, 0 : 0, bit>}]>, vpi_typedef_layers = [{aliases = [@s1.$root::@s2::@s5.P], path = array<i64>}]} {
              }
              obelisk.sv.expression.named_value attributes {is_signed = false, node_id = 18 : i64, referenced_path = "top.q", referenced_symbol = @s1.$root::@s7.top::@s8.top::@s10.q, semantic_type = !obelisk.queue<!obelisk.source_aggregate<"$unit", false, false, false, false, false, false, 0, 32, 32, 0, [{name = "e", ordinal = 0 : i32, packed_offset = 0 : i64, rand_mode = 0 : i32, type = !obelisk.enum<"E", !obelisk.integral<32, true, false, 31 : 0, int>>}]>, 0>, vpi_typedef_layers = [{aliases = [@s1.$root::@s2::@s6.U], path = array<i64: 0>}, {aliases = [@s1.$root::@s2::@s4.E], path = array<i64: 0, 0>}]} {
              }
              obelisk.sv.expression.named_value attributes {is_signed = false, node_id = 19 : i64, referenced_path = "top.s", referenced_symbol = @s1.$root::@s7.top::@s8.top::@s11.s, semantic_type = !obelisk.string} {
              }
              obelisk.sv.expression.string_literal attributes {constant_value = "lit", is_signed = false, node_id = 20 : i64, semantic_type = !obelisk.ranged_packed_array<23 : 0 x !obelisk.integral<1, false, false, 0 : 0, bit>>} {
              }
              obelisk.sv.expression.named_value attributes {is_signed = false, node_id = 21 : i64, referenced_path = "top.h", referenced_symbol = @s1.$root::@s7.top::@s8.top::@s12.h, semantic_type = !obelisk.chandle} {
              }
              obelisk.sv.expression.conversion attributes {is_implicit = false, is_signed = true, node_id = 22 : i64, semantic_type = !obelisk.enum<"E", !obelisk.integral<32, true, false, 31 : 0, int>>, vpi_source_type_identity = 0 : i64, vpi_typedef_layers = [{aliases = [@s1.$root::@s2::@s4.E], path = array<i64>}]} {
                obelisk.sv.expression.integer_literal attributes {constant_value = "0", is_declared_unsized = true, is_signed = true, node_id = 23 : i64, semantic_type = !obelisk.integral<32, true, false, 31 : 0, int>} {
                }
              }
            }
          }
        }
      }
    }
  }
}

