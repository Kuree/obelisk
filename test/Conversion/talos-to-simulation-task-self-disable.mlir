// RUN: talos-opt %s '--lower-talos-to-sim=opt-level=0' | FileCheck %s

// IEEE 1800-2023 9.6.2: a task that disables itself also ends the disabling
// activation. The runtime has already dropped that activation from the
// control stack, so the task returns without leaving it.
//
// module top;
//   task automatic early;
//     disable early;
//   endtask
//   initial early();
// endmodule

module attributes {coverage.language_version = 2023 : i32} {
  talos.sv.symbol.definition @s0.top attributes {definition_kind = 0 : i32, hierarchical_name = "top", name = "top", node_id = 0 : i64} {
  }
  talos.sv.symbol.root @s1.$root attributes {hierarchical_name = "\\$root ", name = "$root", node_id = 1 : i64} {
    talos.sv.symbol.compilation_unit @s2 attributes {hierarchical_name = "$unit", node_id = 2 : i64, simulation.vpi_definition_name = "$unit"} {
    }
    talos.sv.symbol.instance @s3.top attributes {hierarchical_name = "top", is_uninstantiated = false, name = "top", node_id = 3 : i64, referenced_path = "top", referenced_symbol = @s0.top} {
      talos.sv.symbol.instance_body @s4.top attributes {hierarchical_name = "top", name = "top", node_id = 4 : i64, simulation.vpi_automatic = false, simulation.vpi_cell_instance = false, simulation.vpi_definition_name = "top", simulation.vpi_top = true, time_precision_fs = 1000000 : i64, time_unit_fs = 1000000 : i64, vpi_scope_kind = 32 : i32} {
        talos.sv.symbol.subroutine @s5.early attributes {hierarchical_name = "top.early", name = "early", node_id = 5 : i64, semantic_type = !talos.subroutine<() -> (), true>, subroutine_kind = 1 : i32, time_precision_fs = 1000000 : i64, time_unit_fs = 1000000 : i64} {
          talos.sv.statement.disable attributes {node_id = 6 : i64, target_path = "top.early", target_symbol = @s1.$root::@s3.top::@s4.top::@s5.early} {
          }
        }
        talos.sv.symbol.procedural_block @s6 attributes {hierarchical_name = "top", node_id = 7 : i64, procedure_kind = 0 : i32, time_precision_fs = 1000000 : i64, time_unit_fs = 1000000 : i64} {
          talos.sv.statement.expression_statement attributes {node_id = 8 : i64} {
            talos.sv.expression.call attributes {argument_count = 0 : i64, callee_name = "early", constraint_restrictions = [], defaulted_arguments = array<i64>, has_inline_constraints = false, has_iterator_expression = false, has_output_arguments = false, has_this_class = false, is_signed = false, is_super_class = false, is_system_call = false, node_id = 9 : i64, referenced_path = "top.early", referenced_symbol = @s1.$root::@s3.top::@s4.top::@s5.early, semantic_type = !talos.void, subroutine_kind = 1 : i32} {
            }
          }
        }
      }
    }
  }
}


// CHECK-LABEL: simulation.func private @unit_0(
// CHECK: simulation.control.enter 1
// CHECK-NEXT: simulation.control.disable 1 {hierarchical = true}
// CHECK-NEXT: simulation.return
// CHECK-NEXT: }
