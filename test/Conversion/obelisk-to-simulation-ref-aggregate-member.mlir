// RUN: obelisk-opt %s '--lower-obelisk-to-sim=early-symbol-dce=false' | FileCheck %s

!record = !obelisk.source_aggregate<"$unit", false, false, false, false, false, false, 0, 96, 96, 0, [{name = "kind", ordinal = 0 : i32, packed_offset = 0 : i64, type = !obelisk.integral<32, true, false, 31 : 0, int>}, {name = "address", ordinal = 1 : i32, packed_offset = 0 : i64, type = !obelisk.integral<64, false, false, 63 : 0, longint>}]>

module {
  obelisk.sv.symbol.root @s1.$root attributes {hierarchical_name = "\\$root ", name = "$root", node_id = 1 : i64} {
    obelisk.sv.symbol.compilation_unit @s2 attributes {hierarchical_name = "$unit", node_id = 2 : i64} {
      obelisk.sv.type.class_type @s4.base attributes {bitstream_width = 0 : i64, declared_interfaces = [], generic_parameter_paths = [], generic_parameter_symbols = [], has_base_constructor_call = false, has_cycles = false, hierarchical_name = "base", implemented_interfaces = [], is_abstract = false, is_final = false, is_interface = false, is_uninstantiated = false, name = "base", node_id = 4 : i64, semantic_type = !obelisk.class_handle<@s1.$root::@s2::@s4.base>, this_variable_path = "base::this", this_variable_symbol = @s1.$root::@s2::@s4.base::@s28.this} {
        obelisk.sv.symbol.subroutine @s5.update attributes {hierarchical_name = "base::update", is_declared_virtual, is_virtual, name = "update", node_id = 5 : i64, semantic_type = !obelisk.subroutine<(!record) -> !obelisk.void, false>, subroutine_kind = 0 : i32, this_variable_path = "base::update.this", this_variable_symbol = @s1.$root::@s2::@s4.base::@s5.update::@s7.this, time_precision_fs = 1000000 : i64, time_unit_fs = 1000000 : i64} {
          obelisk.sv.statement.expression_statement attributes {node_id = 6 : i64} {
            obelisk.sv.expression.assignment attributes {assignment_kind = 0 : i32, is_signed = true, node_id = 7 : i64, semantic_type = !obelisk.integral<32, true, false, 31 : 0, int>} {
              obelisk.sv.expression.member_access attributes {field_ordinal = 0 : i64, is_signed = true, member_name = "kind", node_id = 8 : i64, packed_offset = 0 : i64, referenced_path = ".kind", referenced_symbol = @s1.$root::@s2::@s25::@s26.kind, semantic_type = !obelisk.integral<32, true, false, 31 : 0, int>} {
                obelisk.sv.expression.named_value attributes {is_signed = false, node_id = 9 : i64, referenced_path = "base::update.operation", referenced_symbol = @s1.$root::@s2::@s4.base::@s5.update::@s6.operation, semantic_type = !record} {
                }
              }
              obelisk.sv.expression.integer_literal attributes {constant_value = "7", is_declared_unsized = true, is_signed = true, node_id = 10 : i64, semantic_type = !obelisk.integral<32, true, false, 31 : 0, int>} {
              }
            }
          }
          obelisk.sv.symbol.formal_argument @s6.operation attributes {direction = 3 : i32, hierarchical_name = "base::update.operation", name = "operation", node_id = 11 : i64, semantic_type = !record} {
          }
          obelisk.sv.symbol.variable @s7.this attributes {hierarchical_name = "base::update.this", is_compiler_generated, is_const, name = "this", node_id = 12 : i64, semantic_type = !obelisk.class_handle<@s1.$root::@s2::@s4.base>} {
          }
        }
        obelisk.sv.symbol.variable @s28.this attributes {hierarchical_name = "base::this", is_compiler_generated, is_const, name = "this", node_id = 45 : i64, semantic_type = !obelisk.class_handle<@s1.$root::@s2::@s4.base>} {
        }
      }
      obelisk.sv.type.unpacked_struct_type @s25 attributes {hierarchical_name = "$unit", node_id = 42 : i64, semantic_type = !record} {
        obelisk.sv.symbol.field @s26.kind attributes {bit_offset = 0 : i64, field_index = 0 : i64, hierarchical_name = ".kind", name = "kind", node_id = 43 : i64, semantic_type = !obelisk.integral<32, true, false, 31 : 0, int>} {
        }
        obelisk.sv.symbol.field @s27.address attributes {bit_offset = 32 : i64, field_index = 1 : i64, hierarchical_name = ".address", name = "address", node_id = 44 : i64, semantic_type = !obelisk.integral<64, false, false, 63 : 0, longint>} {
        }
      }
    }
  }
}

// CHECK-LABEL: simulation.func private @unit_0(
// CHECK-SAME: %[[REFERENCE:arg[0-9]+]]: !simulation.argument_ref<
// CHECK: %[[OLD:.*]] = simulation.argument_ref.load %[[REFERENCE]]
// CHECK: %[[UPDATED:.*]] = simulation.aggregate.insert {{.*}} into %[[OLD]][0]
// CHECK: simulation.argument_ref.store %[[UPDATED]] to %[[REFERENCE]]
