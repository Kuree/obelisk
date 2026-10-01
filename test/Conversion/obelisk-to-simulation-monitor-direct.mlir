// RUN: obelisk-opt %s '--lower-obelisk-to-sim=opt-level=0' | FileCheck %s

// A direct $monitor argument changes exactly when its declaration changes.
// Keep that fixed descriptor wait out of the computed-observer runtime path.
module {
  obelisk.sv.symbol.definition @s0.top attributes {definition_kind = 0 : i32, hierarchical_name = "top", name = "top", node_id = 0 : i64} {
  }
  obelisk.sv.symbol.root @s1.$root attributes {hierarchical_name = "\\$root ", name = "$root", node_id = 1 : i64} {
    obelisk.sv.symbol.compilation_unit @s2 attributes {hierarchical_name = "$unit", node_id = 2 : i64} {
    }
    obelisk.sv.symbol.instance @s3.top attributes {hierarchical_name = "top", is_uninstantiated = false, name = "top", node_id = 3 : i64, referenced_path = "top", referenced_symbol = @s0.top} {
      obelisk.sv.symbol.instance_body @s4.top attributes {hierarchical_name = "top", name = "top", node_id = 4 : i64} {
        obelisk.sv.symbol.variable @s5.value attributes {hierarchical_name = "top.value", lifetime = 1 : i32, name = "value", node_id = 5 : i64, semantic_type = !obelisk.integral<1, false, true, 0 : 0, reg>} {
        }
        obelisk.sv.symbol.procedural_block @s6 attributes {hierarchical_name = "top", node_id = 6 : i64, procedure_kind = 0 : i32, time_precision_fs = 1000000 : i64, time_unit_fs = 1000000 : i64} {
          obelisk.sv.statement.expression_statement attributes {node_id = 7 : i64} {
            obelisk.sv.expression.call attributes {argument_count = 1 : i64, callee_name = "$monitor", constraint_restrictions = [], has_inline_constraints = false, has_iterator_expression = false, has_output_arguments = false, has_this_class = false, is_super_class = false, is_system_call = true, node_id = 8 : i64, semantic_type = !obelisk.void, subroutine_kind = 1 : i32, system_library_cell = "work.top", system_scope_path = "top", system_scope_symbol = @s1.$root::@s3.top::@s4.top} {
              obelisk.sv.expression.named_value attributes {is_signed = false, node_id = 9 : i64, referenced_path = "top.value", referenced_symbol = @s1.$root::@s3.top::@s4.top::@s5.value, semantic_type = !obelisk.integral<1, false, true, 0 : 0, reg>} {
              }
            }
          }
        }
      }
    }
  }
}

// CHECK: simulation.func private @[[CALLBACK:unit_0[.][^(]+]](
// CHECK-SAME: simulation.persistent_monitor
// CHECK: simulation.display
// CHECK: simulation.suspend.change
// CHECK-NOT: simulation.suspend.observe
// CHECK-LABEL: simulation.func private @unit_0(
// CHECK: %[[PROCESS:.*]] = simulation.spawn @[[CALLBACK]]
// CHECK: simulation.monitor.register %[[PROCESS]]
