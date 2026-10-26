// RUN: obelisk-opt %s '--lower-obelisk-to-sim=vpi=full opt-level=0' | FileCheck %s
// RUN: obelisk-opt %s '--lower-obelisk-to-sim=vpi=off opt-level=0' | FileCheck %s --check-prefix=OFF
// Automatic declaration identity follows the exact named source block and
// source order, independently from repeated runtime allocation.
// CHECK: simulation.code_unit.decl [[TASK:[0-9]+]] in 1 task hierarchy "event_auto.alias_local"
// CHECK: simulation.statement.decl [[BLOCK:[0-9]+]] in [[TASK]] scope 1 type 33 {{.*}}name "block" {is_scope}
// CHECK: simulation.vpi_local.decl @[[SOURCE:.*]] id 1 in [[TASK]] scope 1 within [[BLOCK]] ordinal 0 hierarchy "event_auto.alias_local.block.source" debug "source" : !simulation.event
// CHECK-NEXT: simulation.vpi_local.decl @[[ALIAS:.*]] id 2 in [[TASK]] scope 1 within [[BLOCK]] ordinal 1 hierarchy "event_auto.alias_local.block.alias" debug "alias" : !simulation.event
// CHECK-NEXT: simulation.vpi_local.decl @[[VALUE:.*]] id 3 in [[TASK]] scope 1 within [[BLOCK]] ordinal 2 hierarchy "event_auto.alias_local.block.value" debug "value" : i32
// CHECK-NOT: simulation.vpi_local.decl
// OFF-NOT: simulation.vpi_local.decl
// OFF-NOT: simulation.vpi.frame.bind
module {
  obelisk.sv.symbol.definition @s0.event_auto attributes {definition_kind = 0 : i32, hierarchical_name = "event_auto", name = "event_auto", node_id = 0 : i64} {
  }
  obelisk.sv.symbol.root @s1.$root attributes {hierarchical_name = "\\$root ", name = "$root", node_id = 1 : i64} {
    obelisk.sv.symbol.compilation_unit @s2 attributes {hierarchical_name = "$unit", node_id = 2 : i64} {
    }
    obelisk.sv.symbol.instance @s3.event_auto attributes {hierarchical_name = "event_auto", is_uninstantiated = false, name = "event_auto", node_id = 3 : i64, referenced_path = "event_auto", referenced_symbol = @s0.event_auto} {
      obelisk.sv.symbol.instance_body @s4.event_auto attributes {hierarchical_name = "event_auto", name = "event_auto", node_id = 4 : i64, time_precision_fs = 1000000 : i64, time_unit_fs = 1000000 : i64} {
        obelisk.sv.symbol.subroutine @s5.alias_local attributes {hierarchical_name = "event_auto.alias_local", name = "alias_local", node_id = 5 : i64, semantic_type = !obelisk.subroutine<() -> (), true>, subroutine_kind = 1 : i32, time_precision_fs = 1000000 : i64, time_unit_fs = 1000000 : i64} {
          obelisk.sv.statement.repeat_loop attributes {node_id = 21 : i64} {
            obelisk.sv.expression.integer_literal attributes {constant_value = "2", node_id = 22 : i64, semantic_type = !obelisk.integral<32, true, false, 31 : 0, int>} {}
            obelisk.sv.statement.block attributes {block_path = "event_auto.alias_local.block", block_symbol = @s1.$root::@s3.event_auto::@s4.event_auto::@s5.alias_local::@block, node_id = 23 : i64} {
          obelisk.sv.statement.list attributes {node_id = 20 : i64} {
            obelisk.sv.statement.variable_declaration attributes {node_id = 7 : i64, referenced_path = "event_auto.alias_local.block.source", referenced_symbol = @s1.$root::@s3.event_auto::@s4.event_auto::@s5.alias_local::@block::@s6.source} {
            }
            obelisk.sv.statement.variable_declaration attributes {node_id = 8 : i64, referenced_path = "event_auto.alias_local.block.alias", referenced_symbol = @s1.$root::@s3.event_auto::@s4.event_auto::@s5.alias_local::@block::@s7.alias} {
            }
            obelisk.sv.statement.variable_declaration attributes {node_id = 25 : i64, referenced_path = "event_auto.alias_local.block.value", referenced_symbol = @s1.$root::@s3.event_auto::@s4.event_auto::@s5.alias_local::@block::@value} {}
            obelisk.sv.statement.variable_declaration attributes {node_id = 26 : i64, referenced_path = "event_auto.alias_local.block.saved", referenced_symbol = @s1.$root::@s3.event_auto::@s4.event_auto::@s5.alias_local::@block::@saved} {}
            obelisk.sv.statement.event_trigger attributes {node_id = 9 : i64} {
              obelisk.sv.expression.named_value attributes {node_id = 10 : i64, referenced_path = "event_auto.alias_local.block.alias", referenced_symbol = @s1.$root::@s3.event_auto::@s4.event_auto::@s5.alias_local::@block::@s7.alias, semantic_type = !obelisk.event} {
              }
            }
          }
            }
          }
          obelisk.sv.symbol.statement_block @block attributes {block_kind = 0 : i32, hierarchical_name = "event_auto.alias_local.block", name = "block", node_id = 24 : i64} {
          obelisk.sv.symbol.variable @value attributes {hierarchical_name = "event_auto.alias_local.block.value", name = "value", node_id = 27 : i64, semantic_type = !obelisk.integral<32, true, false, 31 : 0, int>} {
            obelisk.sv.expression.integer_literal attributes {constant_value = "23", node_id = 28 : i64, semantic_type = !obelisk.integral<32, true, false, 31 : 0, int>} {}
          }
          obelisk.sv.symbol.variable @saved attributes {hierarchical_name = "event_auto.alias_local.block.saved", name = "saved", lifetime = 1 : i32, node_id = 29 : i64, semantic_type = !obelisk.integral<32, true, false, 31 : 0, int>} {}
          obelisk.sv.symbol.variable @s6.source attributes {hierarchical_name = "event_auto.alias_local.block.source", name = "source", node_id = 11 : i64, semantic_type = !obelisk.event} {
          }
          obelisk.sv.symbol.variable @s7.alias attributes {hierarchical_name = "event_auto.alias_local.block.alias", name = "alias", node_id = 12 : i64, semantic_type = !obelisk.event} {
            obelisk.sv.expression.named_value attributes {node_id = 13 : i64, referenced_path = "event_auto.alias_local.block.source", referenced_symbol = @s1.$root::@s3.event_auto::@s4.event_auto::@s5.alias_local::@block::@s6.source, semantic_type = !obelisk.event} {
            }
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
