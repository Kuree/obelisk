// RUN: obelisk-opt %s '--lower-obelisk-to-sim=opt-level=0' | FileCheck %s

// IEEE 1800-2017 6.21: a variable declared inside a static task defaults to a
// static lifetime, so the named event of 15.5 is created once with the design.
// Its declaration statement therefore only brings the name into scope and has
// nothing to allocate or initialize.

module {
  obelisk.sv.symbol.definition @s0.static_task_event attributes {definition_kind = 0 : i32, hierarchical_name = "static_task_event", name = "static_task_event", node_id = 0 : i64} {
  }
  obelisk.sv.symbol.root @s1.$root attributes {hierarchical_name = "\\$root ", name = "$root", node_id = 1 : i64} {
    obelisk.sv.symbol.compilation_unit @s2 attributes {hierarchical_name = "$unit", node_id = 2 : i64} {
    }
    obelisk.sv.symbol.instance @s3.static_task_event attributes {hierarchical_name = "static_task_event", is_uninstantiated = false, name = "static_task_event", node_id = 3 : i64, referenced_path = "static_task_event", referenced_symbol = @s0.static_task_event} {
      obelisk.sv.symbol.instance_body @s4.static_task_event attributes {hierarchical_name = "static_task_event", name = "static_task_event", node_id = 4 : i64, time_precision_fs = 1000000 : i64, time_unit_fs = 1000000 : i64} {
        obelisk.sv.symbol.subroutine @s5.fire attributes {default_lifetime = 1 : i32, hierarchical_name = "static_task_event.fire", name = "fire", node_id = 5 : i64, semantic_type = !obelisk.subroutine<() -> (), true>, subroutine_kind = 1 : i32, time_precision_fs = 1000000 : i64, time_unit_fs = 1000000 : i64} {
          obelisk.sv.statement.list attributes {node_id = 6 : i64} {
            obelisk.sv.statement.variable_declaration attributes {node_id = 7 : i64, referenced_path = "static_task_event.fire.step", referenced_symbol = @s1.$root::@s3.static_task_event::@s4.static_task_event::@s5.fire::@s6.step} {
            }
            obelisk.sv.statement.event_trigger attributes {node_id = 8 : i64} {
              obelisk.sv.expression.named_value attributes {is_signed = false, node_id = 9 : i64, referenced_path = "static_task_event.fire.step", referenced_symbol = @s1.$root::@s3.static_task_event::@s4.static_task_event::@s5.fire::@s6.step, semantic_type = !obelisk.event} {
              }
            }
          }
          obelisk.sv.symbol.variable @s6.step attributes {hierarchical_name = "static_task_event.fire.step", lifetime = 1 : i32, name = "step", node_id = 10 : i64, semantic_type = !obelisk.event} {
          }
        }
        obelisk.sv.symbol.procedural_block @s7 attributes {hierarchical_name = "static_task_event", node_id = 11 : i64, procedure_kind = 0 : i32, time_precision_fs = 1000000 : i64, time_unit_fs = 1000000 : i64} {
          obelisk.sv.statement.expression_statement attributes {node_id = 12 : i64} {
            obelisk.sv.expression.call attributes {argument_count = 0 : i64, callee_name = "fire", constraint_restrictions = [], defaulted_arguments = array<i64>, has_inline_constraints = false, has_iterator_expression = false, has_output_arguments = false, has_this_class = false, is_signed = false, is_super_class = false, is_system_call = false, node_id = 13 : i64, referenced_path = "static_task_event.fire", referenced_symbol = @s1.$root::@s3.static_task_event::@s4.static_task_event::@s5.fire, semantic_type = !obelisk.void, subroutine_kind = 1 : i32} {
            }
          }
        }
      }
    }
  }
}

// CHECK: simulation.vpi_object.anchor {{.*}} type 59 {{.*}} hierarchy "static_task_event.fire" {{.*}}{backing = #simulation.vpi_backing<kind = code_unit, id = {{[0-9]+}} : i64>}
// CHECK: simulation.code_unit.decl {{[0-9]+}} in {{[0-9]+}} task hierarchy "static_task_event.fire"
// CHECK: simulation.func @__obelisk_root
// CHECK: simulation.context.event %{{.*}}[0] : !simulation.event

// CHECK: simulation.func private @unit_0(
// CHECK-SAME: %[[EVENT:[^:]*]]: !simulation.event
// CHECK-NOT: simulation.ref.alloc
// CHECK: simulation.event.trigger %[[EVENT]]
