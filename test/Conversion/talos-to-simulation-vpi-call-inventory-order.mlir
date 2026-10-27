// RUN: talos-opt %s '--lower-talos-to-sim=vpi=full opt-level=0' | FileCheck %s
// RUN: talos-opt %s '--lower-talos-to-sim=vpi=full opt-level=3' | FileCheck %s
// The statement list spills its child vector beyond inline storage. Each
// source call must retain one distinct statement alias in source order.
module {
  talos.sv.symbol.definition @def attributes {definition_kind = 0 : i32, hierarchical_name = "top", name = "top", node_id = 1 : i64} {}
  talos.sv.symbol.root @root attributes {hierarchical_name = "$root", name = "$root", node_id = 2 : i64} {
    talos.sv.symbol.compilation_unit @cu attributes {hierarchical_name = "$unit", node_id = 3 : i64} {}
    talos.sv.symbol.instance @top attributes {hierarchical_name = "top", is_uninstantiated = false, name = "top", node_id = 4 : i64, referenced_path = "top", referenced_symbol = @def} {
      talos.sv.symbol.instance_body @body attributes {hierarchical_name = "top", name = "top", node_id = 5 : i64} {
        talos.sv.symbol.subroutine @work attributes {hierarchical_name = "top.work", name = "work", node_id = 6 : i64, semantic_type = !talos.subroutine<() -> !talos.void, false>, subroutine_kind = 0 : i32} {
          talos.sv.statement.empty attributes {node_id = 7 : i64} {}
        }
        talos.sv.symbol.procedural_block @initial attributes {hierarchical_name = "top", node_id = 8 : i64, procedure_kind = 0 : i32} {
          talos.sv.statement.list attributes {node_id = 9 : i64} {
            talos.sv.statement.expression_statement attributes {node_id = 10 : i64} {
              talos.sv.expression.call attributes {node_id = 11 : i64, argument_count = 0 : i64, callee_name = "work", constraint_restrictions = [], defaulted_arguments = array<i64>, has_inline_constraints = false, has_iterator_expression = false, has_output_arguments = false, has_this_class = false, is_super_class = false, is_system_call = false, referenced_path = "top.work", referenced_symbol = @root::@top::@body::@work, semantic_type = !talos.void, subroutine_kind = 0 : i32} {}
            }
            talos.sv.statement.expression_statement attributes {node_id = 12 : i64} {
              talos.sv.expression.call attributes {node_id = 13 : i64, argument_count = 0 : i64, callee_name = "work", constraint_restrictions = [], defaulted_arguments = array<i64>, has_inline_constraints = false, has_iterator_expression = false, has_output_arguments = false, has_this_class = false, is_super_class = false, is_system_call = false, referenced_path = "top.work", referenced_symbol = @root::@top::@body::@work, semantic_type = !talos.void, subroutine_kind = 0 : i32} {}
            }
            talos.sv.statement.expression_statement attributes {node_id = 14 : i64} {
              talos.sv.expression.call attributes {node_id = 15 : i64, argument_count = 0 : i64, callee_name = "work", constraint_restrictions = [], defaulted_arguments = array<i64>, has_inline_constraints = false, has_iterator_expression = false, has_output_arguments = false, has_this_class = false, is_super_class = false, is_system_call = false, referenced_path = "top.work", referenced_symbol = @root::@top::@body::@work, semantic_type = !talos.void, subroutine_kind = 0 : i32} {}
            }
            talos.sv.statement.expression_statement attributes {node_id = 16 : i64} {
              talos.sv.expression.call attributes {node_id = 17 : i64, argument_count = 0 : i64, callee_name = "work", constraint_restrictions = [], defaulted_arguments = array<i64>, has_inline_constraints = false, has_iterator_expression = false, has_output_arguments = false, has_this_class = false, is_super_class = false, is_system_call = false, referenced_path = "top.work", referenced_symbol = @root::@top::@body::@work, semantic_type = !talos.void, subroutine_kind = 0 : i32} {}
            }
            talos.sv.statement.expression_statement attributes {node_id = 18 : i64} {
              talos.sv.expression.call attributes {node_id = 19 : i64, argument_count = 0 : i64, callee_name = "work", constraint_restrictions = [], defaulted_arguments = array<i64>, has_inline_constraints = false, has_iterator_expression = false, has_output_arguments = false, has_this_class = false, is_super_class = false, is_system_call = false, referenced_path = "top.work", referenced_symbol = @root::@top::@body::@work, semantic_type = !talos.void, subroutine_kind = 0 : i32} {}
            }
            talos.sv.statement.expression_statement attributes {node_id = 20 : i64} {
              talos.sv.expression.call attributes {node_id = 21 : i64, argument_count = 0 : i64, callee_name = "work", constraint_restrictions = [], defaulted_arguments = array<i64>, has_inline_constraints = false, has_iterator_expression = false, has_output_arguments = false, has_this_class = false, is_super_class = false, is_system_call = false, referenced_path = "top.work", referenced_symbol = @root::@top::@body::@work, semantic_type = !talos.void, subroutine_kind = 0 : i32} {}
            }
            talos.sv.statement.expression_statement attributes {node_id = 22 : i64} {
              talos.sv.expression.call attributes {node_id = 23 : i64, argument_count = 0 : i64, callee_name = "work", constraint_restrictions = [], defaulted_arguments = array<i64>, has_inline_constraints = false, has_iterator_expression = false, has_output_arguments = false, has_this_class = false, is_super_class = false, is_system_call = false, referenced_path = "top.work", referenced_symbol = @root::@top::@body::@work, semantic_type = !talos.void, subroutine_kind = 0 : i32} {}
            }
            talos.sv.statement.expression_statement attributes {node_id = 24 : i64} {
              talos.sv.expression.call attributes {node_id = 25 : i64, argument_count = 0 : i64, callee_name = "work", constraint_restrictions = [], defaulted_arguments = array<i64>, has_inline_constraints = false, has_iterator_expression = false, has_output_arguments = false, has_this_class = false, is_super_class = false, is_system_call = false, referenced_path = "top.work", referenced_symbol = @root::@top::@body::@work, semantic_type = !talos.void, subroutine_kind = 0 : i32} {}
            }
            talos.sv.statement.expression_statement attributes {node_id = 26 : i64} {
              talos.sv.expression.call attributes {node_id = 27 : i64, argument_count = 0 : i64, callee_name = "work", constraint_restrictions = [], defaulted_arguments = array<i64>, has_inline_constraints = false, has_iterator_expression = false, has_output_arguments = false, has_this_class = false, is_super_class = false, is_system_call = false, referenced_path = "top.work", referenced_symbol = @root::@top::@body::@work, semantic_type = !talos.void, subroutine_kind = 0 : i32} {}
            }
          }
        }
      }
    }
  }
}
// CHECK: simulation.vpi_call.decl @__talos_vpi_call_1 id 1 {{.*}} name "work" {{.*}}statement 3
// CHECK: simulation.vpi_call.decl @__talos_vpi_call_2 id 2 {{.*}} name "work" {{.*}}statement 4
// CHECK: simulation.vpi_call.decl @__talos_vpi_call_3 id 3 {{.*}} name "work" {{.*}}statement 5
// CHECK: simulation.vpi_call.decl @__talos_vpi_call_4 id 4 {{.*}} name "work" {{.*}}statement 6
// CHECK: simulation.vpi_call.decl @__talos_vpi_call_5 id 5 {{.*}} name "work" {{.*}}statement 7
// CHECK: simulation.vpi_call.decl @__talos_vpi_call_6 id 6 {{.*}} name "work" {{.*}}statement 8
// CHECK: simulation.vpi_call.decl @__talos_vpi_call_7 id 7 {{.*}} name "work" {{.*}}statement 9
// CHECK: simulation.vpi_call.decl @__talos_vpi_call_8 id 8 {{.*}} name "work" {{.*}}statement 10
// CHECK: simulation.vpi_call.decl @__talos_vpi_call_9 id 9 {{.*}} name "work" {{.*}}statement 11
// CHECK-NOT: simulation.vpi_call.decl
