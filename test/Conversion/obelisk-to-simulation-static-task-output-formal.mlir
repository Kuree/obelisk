// RUN: obelisk-opt %s '--lower-obelisk-to-sim=opt-level=0' | FileCheck %s

// IEEE 1800-2017 13.5.1 copies an input formal in at the call and 13.5.2
// copies an output formal out at the return -- an output formal is never
// copied in. In a static subroutine the formal is a static variable (13.3.1),
// so what it holds between calls is the value the last call left there, not
// whatever the caller happened to pass for an argument it never reads.

module {
  obelisk.sv.symbol.definition attributes {definition_kind = 0 : i32, hierarchical_name = "static_task_output_formal", name = "static_task_output_formal", node_id = 0 : i64, sym_name = "s0.static_task_output_formal"} {
  }
  obelisk.sv.symbol.root attributes {hierarchical_name = "\\$root ", name = "$root", node_id = 1 : i64, sym_name = "s1.$root"} {
    obelisk.sv.symbol.compilation_unit attributes {hierarchical_name = "$unit", node_id = 2 : i64, sym_name = "s2"} {
    }
    obelisk.sv.symbol.instance attributes {hierarchical_name = "static_task_output_formal", is_uninstantiated = false, name = "static_task_output_formal", node_id = 3 : i64, referenced_path = "static_task_output_formal", referenced_symbol = @s0.static_task_output_formal, sym_name = "s3.static_task_output_formal"} {
      obelisk.sv.symbol.instance_body attributes {hierarchical_name = "static_task_output_formal", name = "static_task_output_formal", node_id = 4 : i64, sym_name = "s4.static_task_output_formal", time_precision_fs = 1000000 : i64, time_unit_fs = 1000000 : i64} {
        obelisk.sv.symbol.subroutine attributes {default_lifetime = 1 : i32, hierarchical_name = "static_task_output_formal.step", name = "step", node_id = 5 : i64, semantic_type = !obelisk.subroutine<(!obelisk.integral<32, true, false, 31 : 0, int>, !obelisk.integral<32, true, false, 31 : 0, int>) -> (), true>, subroutine_kind = 1 : i32, sym_name = "s5.step", time_precision_fs = 1000000 : i64, time_unit_fs = 1000000 : i64} {
          obelisk.sv.statement.expression_statement attributes {node_id = 6 : i64} {
            obelisk.sv.expression.assignment attributes {assignment_kind = 0 : i32, is_signed = true, node_id = 7 : i64, semantic_type = !obelisk.integral<32, true, false, 31 : 0, int>} {
              obelisk.sv.expression.named_value attributes {is_signed = true, node_id = 8 : i64, referenced_path = "static_task_output_formal.step.produced", referenced_symbol = @s1.$root::@s3.static_task_output_formal::@s4.static_task_output_formal::@s5.step::@s6.produced, semantic_type = !obelisk.integral<32, true, false, 31 : 0, int>} {
              }
              obelisk.sv.expression.binary_op attributes {is_signed = true, node_id = 9 : i64, operator_kind = 0 : i32, semantic_type = !obelisk.integral<32, true, false, 31 : 0, int>} {
                obelisk.sv.expression.named_value attributes {is_signed = true, node_id = 10 : i64, referenced_path = "static_task_output_formal.step.produced", referenced_symbol = @s1.$root::@s3.static_task_output_formal::@s4.static_task_output_formal::@s5.step::@s6.produced, semantic_type = !obelisk.integral<32, true, false, 31 : 0, int>} {
                }
                obelisk.sv.expression.named_value attributes {is_signed = true, node_id = 11 : i64, referenced_path = "static_task_output_formal.step.added", referenced_symbol = @s1.$root::@s3.static_task_output_formal::@s4.static_task_output_formal::@s5.step::@s7.added, semantic_type = !obelisk.integral<32, true, false, 31 : 0, int>} {
                }
              }
            }
          }
          obelisk.sv.symbol.formal_argument attributes {direction = 1 : i32, hierarchical_name = "static_task_output_formal.step.produced", lifetime = 1 : i32, name = "produced", node_id = 12 : i64, semantic_type = !obelisk.integral<32, true, false, 31 : 0, int>, sym_name = "s6.produced"} {
          }
          obelisk.sv.symbol.formal_argument attributes {direction = 0 : i32, hierarchical_name = "static_task_output_formal.step.added", lifetime = 1 : i32, name = "added", node_id = 13 : i64, semantic_type = !obelisk.integral<32, true, false, 31 : 0, int>, sym_name = "s7.added"} {
          }
        }
        obelisk.sv.symbol.variable attributes {hierarchical_name = "static_task_output_formal.result", lifetime = 1 : i32, name = "result", node_id = 14 : i64, semantic_type = !obelisk.integral<32, true, false, 31 : 0, int>, sym_name = "s8.result"} {
        }
        obelisk.sv.symbol.procedural_block attributes {hierarchical_name = "static_task_output_formal", node_id = 15 : i64, procedure_kind = 0 : i32, sym_name = "s9", time_precision_fs = 1000000 : i64, time_unit_fs = 1000000 : i64} {
          obelisk.sv.statement.expression_statement attributes {node_id = 16 : i64} {
            obelisk.sv.expression.call attributes {argument_count = 2 : i64, callee_name = "step", constraint_restrictions = [], defaulted_arguments = array<i64: 0, 0>, has_inline_constraints = false, has_iterator_expression = false, has_output_arguments = true, has_this_class = false, is_signed = false, is_super_class = false, is_system_call = false, node_id = 17 : i64, referenced_path = "static_task_output_formal.step", referenced_symbol = @s1.$root::@s3.static_task_output_formal::@s4.static_task_output_formal::@s5.step, semantic_type = !obelisk.void, subroutine_kind = 1 : i32} {
              obelisk.sv.expression.assignment attributes {assignment_kind = 0 : i32, is_signed = true, node_id = 18 : i64, semantic_type = !obelisk.integral<32, true, false, 31 : 0, int>} {
                obelisk.sv.expression.named_value attributes {is_signed = true, node_id = 19 : i64, referenced_path = "static_task_output_formal.result", referenced_symbol = @s1.$root::@s3.static_task_output_formal::@s4.static_task_output_formal::@s8.result, semantic_type = !obelisk.integral<32, true, false, 31 : 0, int>} {
                }
                obelisk.sv.expression.empty_argument attributes {is_signed = true, node_id = 20 : i64, semantic_type = !obelisk.integral<32, true, false, 31 : 0, int>} {
                }
              }
              obelisk.sv.expression.integer_literal attributes {constant_value = "1", is_declared_unsized = true, is_signed = true, node_id = 21 : i64, semantic_type = !obelisk.integral<32, true, false, 31 : 0, int>} {
              }
            }
          }
        }
      }
    }
  }
}


// CHECK-LABEL: obelisk_sim.func private @unit_0
// CHECK:      %[[PRODUCED:.*]] = obelisk_sim.context.storage %arg0[0]
// CHECK-NOT:  obelisk_sim.ref.store {{.*}} to %[[PRODUCED]]
// CHECK:      %[[ADDED:.*]] = obelisk_sim.context.storage %arg0[1]
// CHECK-NEXT: obelisk_sim.ref.store {{.*}} to %[[ADDED]]
// CHECK:      %[[SUM:.*]] = arith.addi
// CHECK:      obelisk_sim.ref.store %[[SUM]] to %[[PRODUCED]]
