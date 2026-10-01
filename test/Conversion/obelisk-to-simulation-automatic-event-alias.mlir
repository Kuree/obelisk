// RUN: obelisk-opt %s '--lower-obelisk-to-sim=opt-level=0' | FileCheck %s

// IEEE 1800-2017 6.17: an uninitialized automatic event gets one new
// synchronization object on each declaration execution, while an explicitly
// initialized automatic event copies its initializer's handle. The alias must
// therefore trigger the source, without allocating a second event.

// CHECK-LABEL: simulation.func private @unit_0
// CHECK: %[[SOURCE:.*]] = simulation.event.create
// CHECK-NEXT: simulation.event.trigger %[[SOURCE]]
// CHECK-NOT: obelisk.sv.

module {
  obelisk.sv.symbol.definition @s0.event_auto attributes {definition_kind = 0 : i32, hierarchical_name = "event_auto", name = "event_auto", node_id = 0 : i64} {
  }
  obelisk.sv.symbol.root @s1.$root attributes {hierarchical_name = "\\$root ", name = "$root", node_id = 1 : i64} {
    obelisk.sv.symbol.compilation_unit @s2 attributes {hierarchical_name = "$unit", node_id = 2 : i64} {
    }
    obelisk.sv.symbol.instance @s3.event_auto attributes {hierarchical_name = "event_auto", is_uninstantiated = false, name = "event_auto", node_id = 3 : i64, referenced_path = "event_auto", referenced_symbol = @s0.event_auto} {
      obelisk.sv.symbol.instance_body @s4.event_auto attributes {hierarchical_name = "event_auto", name = "event_auto", node_id = 4 : i64, time_precision_fs = 1000000 : i64, time_unit_fs = 1000000 : i64} {
        obelisk.sv.symbol.subroutine @s5.alias_local attributes {hierarchical_name = "event_auto.alias_local", name = "alias_local", node_id = 5 : i64, semantic_type = !obelisk.subroutine<() -> (), true>, subroutine_kind = 1 : i32, time_precision_fs = 1000000 : i64, time_unit_fs = 1000000 : i64} {
          obelisk.sv.statement.list attributes {node_id = 6 : i64} {
            obelisk.sv.statement.variable_declaration attributes {node_id = 7 : i64, referenced_path = "event_auto.alias_local.source", referenced_symbol = @s1.$root::@s3.event_auto::@s4.event_auto::@s5.alias_local::@s6.source} {
            }
            obelisk.sv.statement.variable_declaration attributes {node_id = 8 : i64, referenced_path = "event_auto.alias_local.alias", referenced_symbol = @s1.$root::@s3.event_auto::@s4.event_auto::@s5.alias_local::@s7.alias} {
            }
            obelisk.sv.statement.event_trigger attributes {node_id = 9 : i64} {
              obelisk.sv.expression.named_value attributes {node_id = 10 : i64, referenced_path = "event_auto.alias_local.alias", referenced_symbol = @s1.$root::@s3.event_auto::@s4.event_auto::@s5.alias_local::@s7.alias, semantic_type = !obelisk.event} {
              }
            }
          }
          obelisk.sv.symbol.variable @s6.source attributes {hierarchical_name = "event_auto.alias_local.source", name = "source", node_id = 11 : i64, semantic_type = !obelisk.event} {
          }
          obelisk.sv.symbol.variable @s7.alias attributes {hierarchical_name = "event_auto.alias_local.alias", name = "alias", node_id = 12 : i64, semantic_type = !obelisk.event} {
            obelisk.sv.expression.named_value attributes {node_id = 13 : i64, referenced_path = "event_auto.alias_local.source", referenced_symbol = @s1.$root::@s3.event_auto::@s4.event_auto::@s5.alias_local::@s6.source, semantic_type = !obelisk.event} {
            }
          }
        }
        obelisk.sv.symbol.procedural_block @s8 attributes {hierarchical_name = "event_auto", node_id = 14 : i64, procedure_kind = 0 : i32, time_precision_fs = 1000000 : i64, time_unit_fs = 1000000 : i64} {
          obelisk.sv.statement.expression_statement attributes {node_id = 15 : i64} {
            obelisk.sv.expression.call attributes {argument_count = 0 : i64, callee_name = "alias_local", constraint_restrictions = [], defaulted_arguments = array<i64>, has_inline_constraints = false, has_iterator_expression = false, has_output_arguments = false, has_this_class = false, is_super_class = false, is_system_call = false, node_id = 16 : i64, referenced_path = "event_auto.alias_local", referenced_symbol = @s1.$root::@s3.event_auto::@s4.event_auto::@s5.alias_local, semantic_type = !obelisk.void, subroutine_kind = 1 : i32} {
            }
          }
        }
      }
    }
  }
}
