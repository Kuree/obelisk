// RUN: obelisk-opt %s '--lower-obelisk-to-sim=opt-level=0' | FileCheck %s

// Anonymous enum declarations do not necessarily have an
// obelisk.sv.type.enum_type operation.  The frontend still freezes the exact
// declaration inventory on enum method calls.  Formatted output must reuse
// that inventory even when the method follows the output in source order.

// CHECK-LABEL: simulation.func private @unit_0(
// CHECK: %[[EMPTY:.*]] = simulation.string.literal ""
// CHECK: %[[A:.*]] = simulation.string.literal "A"
// CHECK: arith.select {{.*}}, %[[A]], %[[EMPTY]] : !simulation.string
// CHECK: %[[B:.*]] = simulation.string.literal "B"
// CHECK: arith.select {{.*}}, %[[B]], {{.*}} : !simulation.string
// CHECK: simulation.display

module {
  obelisk.sv.symbol.definition @s0.top attributes {
    definition_kind = 0 : i32, hierarchical_name = "top", name = "top",
    node_id = 0 : i64
  } {}
  obelisk.sv.symbol.root @s1.$root attributes {
    hierarchical_name = "\\$root ", name = "$root", node_id = 1 : i64
  } {
    obelisk.sv.symbol.compilation_unit @s2 attributes {
      hierarchical_name = "$unit", node_id = 2 : i64
    } {}
    obelisk.sv.symbol.instance @s3.top attributes {
      hierarchical_name = "top", is_uninstantiated = false, name = "top",
      node_id = 3 : i64, referenced_path = "top",
      referenced_symbol = @s0.top
    } {
      obelisk.sv.symbol.instance_body @s4.top attributes {
        hierarchical_name = "top", name = "top", node_id = 4 : i64,
        time_precision_fs = 1000000 : i64,
        time_unit_fs = 1000000 : i64
      } {
        obelisk.sv.symbol.variable @s5.value attributes {
          hierarchical_name = "top.value", lifetime = 1 : i32,
          name = "value", node_id = 5 : i64,
          semantic_type = !obelisk.enum<"top", !obelisk.ranged_packed_array<1 : 0 x !obelisk.integral<1, false, true, 0 : 0, logic>>>
        } {}
        obelisk.sv.symbol.procedural_block @s6 attributes {
          hierarchical_name = "top", node_id = 6 : i64,
          procedure_kind = 0 : i32,
          time_precision_fs = 1000000 : i64,
          time_unit_fs = 1000000 : i64
        } {
          obelisk.sv.statement.block attributes {node_id = 7 : i64} {
            obelisk.sv.statement.expression_statement attributes {
              node_id = 8 : i64
            } {
              obelisk.sv.expression.call attributes {
                argument_count = 2 : i64, callee_name = "$display",
                constraint_restrictions = [],
                defaulted_arguments = array<i64: 0, 0>,
                has_inline_constraints = false,
                has_iterator_expression = false,
                has_output_arguments = false, has_this_class = false,
                is_super_class = false, is_system_call = true,
                node_id = 9 : i64, semantic_type = !obelisk.void,
                subroutine_kind = 1 : i32, system_library_cell = "work.top",
                system_scope_path = "top",
                system_scope_symbol = @s1.$root::@s3.top::@s4.top
              } {
                obelisk.sv.expression.string_literal attributes {
                  constant_value = "%p", folded_constant = "16'h2570",
                  node_id = 10 : i64,
                  semantic_type = !obelisk.ranged_packed_array<15 : 0 x !obelisk.integral<1, false, false, 0 : 0, bit>>
                } {}
                obelisk.sv.expression.named_value attributes {
                  node_id = 11 : i64, referenced_path = "top.value",
                  referenced_symbol = @s1.$root::@s3.top::@s4.top::@s5.value,
                  semantic_type = !obelisk.enum<"top", !obelisk.ranged_packed_array<1 : 0 x !obelisk.integral<1, false, true, 0 : 0, logic>>>
                } {}
              }
            }
            obelisk.sv.statement.expression_statement attributes {
              node_id = 12 : i64
            } {
              obelisk.sv.expression.call attributes {
                argument_count = 1 : i64, callee_name = "next",
                constraint_restrictions = [],
                defaulted_arguments = array<i64: 0>,
                enum_method_names = ["A", "B"],
                enum_method_values = ["2'b01", "2'b10"],
                has_inline_constraints = false,
                has_iterator_expression = false,
                has_output_arguments = false, has_this_class = false,
                is_super_class = false, is_system_call = true,
                node_id = 13 : i64,
                semantic_type = !obelisk.enum<"top", !obelisk.ranged_packed_array<1 : 0 x !obelisk.integral<1, false, true, 0 : 0, logic>>>,
                subroutine_kind = 0 : i32
              } {
                obelisk.sv.expression.named_value attributes {
                  node_id = 14 : i64, referenced_path = "top.value",
                  referenced_symbol = @s1.$root::@s3.top::@s4.top::@s5.value,
                  semantic_type = !obelisk.enum<"top", !obelisk.ranged_packed_array<1 : 0 x !obelisk.integral<1, false, true, 0 : 0, logic>>>
                } {}
              }
            }
          }
        }
      }
    }
  }
}
