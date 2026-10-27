// RUN: talos-opt %s '--lower-talos-to-sim=opt-level=0' | FileCheck %s
// The event and its explicit alias are automatic declarations in a repeated
// named block. Each iteration allocates exactly one synchronization object.
// CHECK-LABEL: simulation.func private @unit_0
// CHECK-NOT: simulation.event.create
// CHECK: cf.br ^[[HEADER:bb[0-9]+]]
// CHECK: ^[[HEADER]]
// CHECK: cf.cond_br {{.*}}, ^[[BODY:bb[0-9]+]],
// CHECK: ^[[BODY]]:
// CHECK: %[[EVENT:.*]] = simulation.event.create
// CHECK-NOT: simulation.event.create
// CHECK: simulation.event.trigger %[[EVENT]]
// CHECK-NOT: simulation.event.create
// CHECK: cf.br ^[[HEADER]]
module {
  talos.sv.symbol.definition @s0.event_auto attributes {definition_kind = 0 : i32, hierarchical_name = "event_auto", name = "event_auto", node_id = 0 : i64} {
  }
  talos.sv.symbol.root @s1.$root attributes {hierarchical_name = "\\$root ", name = "$root", node_id = 1 : i64} {
    talos.sv.symbol.compilation_unit @s2 attributes {hierarchical_name = "$unit", node_id = 2 : i64} {
    }
    talos.sv.symbol.instance @s3.event_auto attributes {hierarchical_name = "event_auto", is_uninstantiated = false, name = "event_auto", node_id = 3 : i64, referenced_path = "event_auto", referenced_symbol = @s0.event_auto} {
      talos.sv.symbol.instance_body @s4.event_auto attributes {hierarchical_name = "event_auto", name = "event_auto", node_id = 4 : i64, time_precision_fs = 1000000 : i64, time_unit_fs = 1000000 : i64} {
        talos.sv.symbol.subroutine @s5.alias_local attributes {hierarchical_name = "event_auto.alias_local", name = "alias_local", node_id = 5 : i64, semantic_type = !talos.subroutine<() -> (), true>, subroutine_kind = 1 : i32, time_precision_fs = 1000000 : i64, time_unit_fs = 1000000 : i64} {
          talos.sv.statement.repeat_loop attributes {node_id = 21 : i64} {
            talos.sv.expression.integer_literal attributes {constant_value = "2", node_id = 22 : i64, semantic_type = !talos.integral<32, true, false, 31 : 0, int>} {}
            talos.sv.statement.block attributes {block_path = "event_auto.alias_local.block", block_symbol = @s1.$root::@s3.event_auto::@s4.event_auto::@s5.alias_local::@block, node_id = 23 : i64} {
          talos.sv.statement.list attributes {node_id = 20 : i64} {
            talos.sv.statement.variable_declaration attributes {node_id = 7 : i64, referenced_path = "event_auto.alias_local.block.source", referenced_symbol = @s1.$root::@s3.event_auto::@s4.event_auto::@s5.alias_local::@block::@s6.source} {
            }
            talos.sv.statement.variable_declaration attributes {node_id = 8 : i64, referenced_path = "event_auto.alias_local.block.alias", referenced_symbol = @s1.$root::@s3.event_auto::@s4.event_auto::@s5.alias_local::@block::@s7.alias} {
            }
            talos.sv.statement.event_trigger attributes {node_id = 9 : i64} {
              talos.sv.expression.named_value attributes {node_id = 10 : i64, referenced_path = "event_auto.alias_local.block.alias", referenced_symbol = @s1.$root::@s3.event_auto::@s4.event_auto::@s5.alias_local::@block::@s7.alias, semantic_type = !talos.event} {
              }
            }
          }
            }
          }
          talos.sv.symbol.statement_block @block attributes {block_kind = 0 : i32, hierarchical_name = "event_auto.alias_local.block", name = "block", node_id = 24 : i64} {
          talos.sv.symbol.variable @s6.source attributes {hierarchical_name = "event_auto.alias_local.block.source", name = "source", node_id = 11 : i64, semantic_type = !talos.event} {
          }
          talos.sv.symbol.variable @s7.alias attributes {hierarchical_name = "event_auto.alias_local.block.alias", name = "alias", node_id = 12 : i64, semantic_type = !talos.event} {
            talos.sv.expression.named_value attributes {node_id = 13 : i64, referenced_path = "event_auto.alias_local.block.source", referenced_symbol = @s1.$root::@s3.event_auto::@s4.event_auto::@s5.alias_local::@block::@s6.source, semantic_type = !talos.event} {
            }
          }
          }
        }
        talos.sv.symbol.procedural_block @s8 attributes {hierarchical_name = "event_auto", node_id = 14 : i64, procedure_kind = 0 : i32, time_precision_fs = 1000000 : i64, time_unit_fs = 1000000 : i64} {
          talos.sv.statement.expression_statement attributes {node_id = 15 : i64} {
            talos.sv.expression.call attributes {argument_count = 0 : i64, callee_name = "alias_local", constraint_restrictions = [], defaulted_arguments = array<i64>, has_inline_constraints = false, has_iterator_expression = false, has_output_arguments = false, has_this_class = false, is_super_class = false, is_system_call = false, node_id = 16 : i64, referenced_path = "event_auto.alias_local", referenced_symbol = @s1.$root::@s3.event_auto::@s4.event_auto::@s5.alias_local, semantic_type = !talos.void, subroutine_kind = 1 : i32} {
            }
          }
        }
      }
    }
  }
}
