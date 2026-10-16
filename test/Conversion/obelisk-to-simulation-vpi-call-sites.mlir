// RUN: obelisk-opt %s '--lower-obelisk-to-sim=opt-level=0 vpi=full' | FileCheck %s
// RUN: obelisk-opt %s '--lower-obelisk-to-sim=opt-level=3 vpi=full' '--encode-obelisk-sim-to-bytecode=vpi=full' -o /dev/null

// A nested return expression has a separate call identity, while discarded
// function/task calls alias their exact statement. IDs remain lexical through
// executable lowering; source names never substitute for identity.
module attributes {llvm.data_layout = "e-m:e-p:64:64-i64:64-n8:16:32:64-S128", llvm.target_triple = "x86_64-unknown-linux-gnu"} {
  obelisk.sv.symbol.definition @definition attributes {definition_kind = 0 : i32, hierarchical_name = "top", name = "top", node_id = 0 : i64} {}
  obelisk.sv.symbol.root @root attributes {hierarchical_name = "$root", name = "$root", node_id = 1 : i64} {
    obelisk.sv.symbol.instance @top attributes {hierarchical_name = "top", name = "top", is_uninstantiated = false, referenced_path = "top", referenced_symbol = @definition, node_id = 2 : i64} {
      obelisk.sv.symbol.instance_body @body attributes {hierarchical_name = "top", name = "top", time_precision_fs = 1 : i64, time_unit_fs = 1 : i64, node_id = 3 : i64} {
        obelisk.sv.symbol.subroutine @inner attributes {hierarchical_name = "top.inner", name = "inner", subroutine_kind = 0 : i32, semantic_type = !obelisk.subroutine<() -> !obelisk.integral<32, true, false, 31 : 0, int>, false>, time_precision_fs = 1 : i64, time_unit_fs = 1 : i64, node_id = 4 : i64} {
          obelisk.sv.statement.return attributes {node_id = 5 : i64} {
            obelisk.sv.expression.integer_literal attributes {constant_value = "1", semantic_type = !obelisk.integral<32, true, false, 31 : 0, int>, node_id = 6 : i64} {}
          }
        }
        obelisk.sv.symbol.subroutine @wrapper attributes {hierarchical_name = "top.wrapper", name = "wrapper", subroutine_kind = 0 : i32, semantic_type = !obelisk.subroutine<() -> !obelisk.integral<32, true, false, 31 : 0, int>, false>, time_precision_fs = 1 : i64, time_unit_fs = 1 : i64, node_id = 7 : i64} {
          obelisk.sv.statement.return attributes {node_id = 8 : i64} {
            obelisk.sv.expression.call attributes {argument_count = 0 : i64, callee_name = "inner", constraint_restrictions = [], defaulted_arguments = array<i64>, has_inline_constraints = false, has_iterator_expression = false, has_output_arguments = false, has_this_class = false, is_signed = true, is_super_class = false, is_system_call = false, referenced_path = "top.inner", referenced_symbol = @root::@top::@body::@inner, semantic_type = !obelisk.integral<32, true, false, 31 : 0, int>, subroutine_kind = 0 : i32, node_id = 9 : i64} {}
          }
        }
        obelisk.sv.symbol.subroutine @worker attributes {hierarchical_name = "top.worker", name = "worker", subroutine_kind = 1 : i32, semantic_type = !obelisk.subroutine<() -> (), true>, time_precision_fs = 1 : i64, time_unit_fs = 1 : i64, node_id = 10 : i64} {
          obelisk.sv.statement.expression_statement attributes {node_id = 11 : i64} {
            obelisk.sv.expression.call attributes {argument_count = 0 : i64, callee_name = "wrapper", constraint_restrictions = [], defaulted_arguments = array<i64>, has_inline_constraints = false, has_iterator_expression = false, has_output_arguments = false, has_this_class = false, is_signed = true, is_super_class = false, is_system_call = false, referenced_path = "top.wrapper", referenced_symbol = @root::@top::@body::@wrapper, semantic_type = !obelisk.integral<32, true, false, 31 : 0, int>, subroutine_kind = 0 : i32, node_id = 12 : i64} {}
          }
        }
        obelisk.sv.symbol.procedural_block @initial attributes {hierarchical_name = "top", procedure_kind = 0 : i32, time_precision_fs = 1 : i64, time_unit_fs = 1 : i64, node_id = 13 : i64} {
          obelisk.sv.statement.expression_statement attributes {node_id = 14 : i64} {
            obelisk.sv.expression.call attributes {argument_count = 0 : i64, callee_name = "worker", constraint_restrictions = [], defaulted_arguments = array<i64>, has_inline_constraints = false, has_iterator_expression = false, has_output_arguments = false, has_this_class = false, is_signed = false, is_super_class = false, is_system_call = false, referenced_path = "top.worker", referenced_symbol = @root::@top::@body::@worker, semantic_type = !obelisk.void, subroutine_kind = 1 : i32, node_id = 15 : i64} {}
          }
        }
      }
    }
  }
}
// CHECK-DAG: simulation.code_unit.decl [[INNER:[0-9]+]] in 1 function hierarchy "top.inner"
// CHECK-DAG: simulation.code_unit.decl [[WRAPPER:[0-9]+]] in 1 function hierarchy "top.wrapper"
// CHECK-DAG: simulation.code_unit.decl [[WORKER:[0-9]+]] in 1 task hierarchy "top.worker"
// CHECK-DAG: simulation.vpi_call.decl @[[NESTED:[^ ]+]] id {{[0-9]+}} in [[WRAPPER]] scope 1 type 19 name "inner" within {{[0-9]+}} callee [[INNER]]
// CHECK-DAG: simulation.vpi_call.decl @[[DISCARDED:[^ ]+]] id {{[0-9]+}} in [[WORKER]] scope 1 type 19 name "wrapper" statement [[STMT:[0-9]+]] within [[STMT]] callee [[WRAPPER]]
// CHECK-DAG: simulation.vpi_call.decl @[[TASK:[^ ]+]] id {{[0-9]+}} in {{[0-9]+}} scope 1 type 60 name "worker" statement [[TASKSTMT:[0-9]+]] within [[TASKSTMT]] callee [[WORKER]]
// CHECK-DAG: simulation.call {{.*}}simulation.vpi_call_site = @[[NESTED]]
// CHECK-DAG: simulation.call {{.*}}simulation.vpi_call_site = @[[DISCARDED]]
// CHECK-DAG: simulation.task.call {{.*}}simulation.vpi_call_site = @[[TASK]]
