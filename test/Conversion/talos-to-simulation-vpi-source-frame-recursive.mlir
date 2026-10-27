// RUN: talos-opt %s '--lower-talos-to-sim=opt-level=0 vpi=full instrument-vpi-frames=true' -o %t.mlir
// RUN: FileCheck %s < %t.mlir
// RUN: env TALOS_TEST_INPUT=%t.mlir %obj_root/test/talos-design-database-dump-test --gtest_filter=GeneratedDesignDatabase.SourceFrameRecursiveExit
// RUN: talos-opt %s '--lower-talos-to-sim=opt-level=3 vpi=full instrument-vpi-frames=true' -o %t.opt.mlir
// RUN: talos-opt %t.mlir --convert-talos-sim-processes-to-llvm-coroutines | FileCheck %s --check-prefix=NATIVE
// RUN: env TALOS_TEST_INPUT=%t.opt.mlir %obj_root/test/talos-design-database-dump-test --gtest_filter=GeneratedDesignDatabase.SourceFrameRecursiveExit

// Two recursive instances of inner enter the same source block. Hierarchical
// disable must select the outer dynamic activation, bypass both trailing
// probe calls, and retain the enclosing function frame until normal return.
// CHECK: simulation.vpi.frame.enter_prepared
// CHECK: simulation.vpi.frame.checkpoint
// CHECK: simulation.vpi.call.prepare
// CHECK: simulation.vpi.frame.enter_foreign
// CHECK: simulation.dpi.call
// CHECK: simulation.vpi.frame.leave
// CHECK: simulation.vpi.call.finish
// CHECK: simulation.vpi.frame.enter {{.*}} within
// NATIVE-DAG: llvm.call @talos_rt_v1_vpi_frame_enter_prepared
// NATIVE-DAG: llvm.call @talos_rt_v1_vpi_frame_checkpoint
// NATIVE-DAG: llvm.call @talos_rt_v1_vpi_call_finish

module attributes {llvm.data_layout = "e-m:e-p:64:64-i64:64-n8:16:32:64-S128", llvm.target_triple = "x86_64-unknown-linux-gnu"} {
  talos.sv.symbol.definition @definition attributes {definition_kind = 0 : i32, hierarchical_name = "top", name = "top", node_id = 0 : i64} {}
  talos.sv.symbol.root @root attributes {hierarchical_name = "$root", name = "$root", node_id = 1 : i64} {
    talos.sv.symbol.instance @top attributes {hierarchical_name = "top", name = "top", is_uninstantiated = false, referenced_path = "top", referenced_symbol = @definition, node_id = 2 : i64} {
      talos.sv.symbol.instance_body @body attributes {hierarchical_name = "top", name = "top", time_precision_fs = 1 : i64, time_unit_fs = 1 : i64, node_id = 3 : i64} {
        talos.sv.symbol.subroutine @probe attributes {dpi_c_identifier = "frame_source_probe", hierarchical_name = "top.probe", is_dpi_context, is_dpi_import, name = "probe", node_id = 20 : i64, semantic_type = !talos.subroutine<() -> !talos.integral<32, true, false, 31 : 0, int>, false>, subroutine_kind = 0 : i32, time_precision_fs = 1 : i64, time_unit_fs = 1 : i64} {}
        talos.sv.symbol.subroutine @inner attributes {hierarchical_name = "top.inner", name = "inner", subroutine_kind = 0 : i32, semantic_type = !talos.subroutine<() -> !talos.integral<32, true, false, 31 : 0, int>, false>, time_precision_fs = 1 : i64, time_unit_fs = 1 : i64, node_id = 4 : i64} {
          talos.sv.statement.block attributes {block_path = "top.inner.block", block_symbol = @root::@top::@body::@inner::@block, node_id = 26 : i64} {
            talos.sv.statement.conditional attributes {check_kind = 0 : i32, condition_count = 1 : i64, condition_pattern_flags = array<i64: 0>, has_else = true, node_id = 28 : i64} {
              talos.sv.expression.call attributes {argument_count = 0 : i64, callee_name = "probe", constraint_restrictions = [], defaulted_arguments = array<i64>, has_inline_constraints = false, has_iterator_expression = false, has_output_arguments = false, has_this_class = false, is_signed = true, is_super_class = false, is_system_call = false, referenced_path = "top.probe", referenced_symbol = @root::@top::@body::@probe, semantic_type = !talos.integral<32, true, false, 31 : 0, int>, subroutine_kind = 0 : i32, node_id = 6 : i64} {}
              talos.sv.statement.expression_statement attributes {node_id = 29 : i64} {
                talos.sv.expression.call attributes {argument_count = 0 : i64, callee_name = "inner", constraint_restrictions = [], defaulted_arguments = array<i64>, has_inline_constraints = false, has_iterator_expression = false, has_output_arguments = false, has_this_class = false, is_signed = true, is_super_class = false, is_system_call = false, referenced_path = "top.inner", referenced_symbol = @root::@top::@body::@inner, semantic_type = !talos.integral<32, true, false, 31 : 0, int>, subroutine_kind = 0 : i32, node_id = 30 : i64} {}
              }
              talos.sv.statement.disable attributes {node_id = 27 : i64, is_hierarchical = true, target_path = "top.inner.block", target_symbol = @root::@top::@body::@inner::@block} {}
            }
            // Neither recursion level may reach this after the shared target
            // is disabled. A static-ID comparison would stop at the inner one.
            talos.sv.statement.expression_statement attributes {node_id = 31 : i64} {
              talos.sv.expression.call attributes {argument_count = 0 : i64, callee_name = "probe", constraint_restrictions = [], defaulted_arguments = array<i64>, has_inline_constraints = false, has_iterator_expression = false, has_output_arguments = false, has_this_class = false, is_signed = true, is_super_class = false, is_system_call = false, referenced_path = "top.probe", referenced_symbol = @root::@top::@body::@probe, semantic_type = !talos.integral<32, true, false, 31 : 0, int>, subroutine_kind = 0 : i32, node_id = 32 : i64} {}
            }
          }
          talos.sv.symbol.statement_block @block attributes {block_kind = 0 : i32, hierarchical_name = "top.inner.block", name = "block", node_id = 33 : i64} {}
        }
        talos.sv.symbol.subroutine @wrapper attributes {hierarchical_name = "top.wrapper", name = "wrapper", subroutine_kind = 0 : i32, semantic_type = !talos.subroutine<() -> !talos.integral<32, true, false, 31 : 0, int>, false>, time_precision_fs = 1 : i64, time_unit_fs = 1 : i64, node_id = 7 : i64} {
          talos.sv.statement.return attributes {node_id = 8 : i64} {
            talos.sv.expression.call attributes {argument_count = 0 : i64, callee_name = "inner", constraint_restrictions = [], defaulted_arguments = array<i64>, has_inline_constraints = false, has_iterator_expression = false, has_output_arguments = false, has_this_class = false, is_signed = true, is_super_class = false, is_system_call = false, referenced_path = "top.inner", referenced_symbol = @root::@top::@body::@inner, semantic_type = !talos.integral<32, true, false, 31 : 0, int>, subroutine_kind = 0 : i32, node_id = 9 : i64} {}
          }
        }
        talos.sv.symbol.subroutine @worker attributes {hierarchical_name = "top.worker", name = "worker", subroutine_kind = 1 : i32, semantic_type = !talos.subroutine<() -> (), true>, time_precision_fs = 1 : i64, time_unit_fs = 1 : i64, node_id = 10 : i64} {
          talos.sv.statement.block attributes {block_path = "top.worker.block", block_symbol = @root::@top::@body::@worker::@block, node_id = 21 : i64} {
          talos.sv.statement.expression_statement attributes {node_id = 11 : i64} {
            talos.sv.expression.call attributes {argument_count = 0 : i64, callee_name = "wrapper", constraint_restrictions = [], defaulted_arguments = array<i64>, has_inline_constraints = false, has_iterator_expression = false, has_output_arguments = false, has_this_class = false, is_signed = true, is_super_class = false, is_system_call = false, referenced_path = "top.wrapper", referenced_symbol = @root::@top::@body::@wrapper, semantic_type = !talos.integral<32, true, false, 31 : 0, int>, subroutine_kind = 0 : i32, node_id = 12 : i64} {}
          }
          }
          talos.sv.symbol.statement_block @block attributes {block_kind = 0 : i32, hierarchical_name = "top.worker.block", name = "block", node_id = 22 : i64} {}
        }
        talos.sv.symbol.procedural_block @initial attributes {hierarchical_name = "top", procedure_kind = 0 : i32, time_precision_fs = 1 : i64, time_unit_fs = 1 : i64, node_id = 13 : i64} {
          talos.sv.statement.list attributes {node_id = 25 : i64} {
          talos.sv.statement.expression_statement attributes {node_id = 14 : i64} {
            talos.sv.expression.call attributes {argument_count = 0 : i64, callee_name = "worker", constraint_restrictions = [], defaulted_arguments = array<i64>, has_inline_constraints = false, has_iterator_expression = false, has_output_arguments = false, has_this_class = false, is_signed = false, is_super_class = false, is_system_call = false, referenced_path = "top.worker", referenced_symbol = @root::@top::@body::@worker, semantic_type = !talos.void, subroutine_kind = 1 : i32, node_id = 15 : i64} {}
          }
          talos.sv.statement.expression_statement attributes {node_id = 23 : i64} {
            talos.sv.expression.call attributes {argument_count = 0 : i64, callee_name = "worker", constraint_restrictions = [], defaulted_arguments = array<i64>, has_inline_constraints = false, has_iterator_expression = false, has_output_arguments = false, has_this_class = false, is_signed = false, is_super_class = false, is_system_call = false, referenced_path = "top.worker", referenced_symbol = @root::@top::@body::@worker, semantic_type = !talos.void, subroutine_kind = 1 : i32, node_id = 24 : i64} {}
          }
          }
        }
      }
    }
  }
}
