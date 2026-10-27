// RUN: talos-opt %s '--lower-talos-to-sim=opt-level=0' | FileCheck %s

// IEEE 1800-2023 9.2.2.2.1 and 9.4.2.2: what an immediate assertion's action
// block reads, what a task body reads, and what a function body reads under
// @* add no implicit sensitivity. A function body still does for always_comb.
//
// module top;
//   logic c, a, h, o, p, q, r;
//   task automatic t(input logic v);
//     p = v | h;
//   endtask
//   function automatic logic f(input logic v);
//     return v | h;
//   endfunction
//   always_comb assert (c) o = a;
//   always_comb t(c);
//   always @* q = f(c);
//   always_comb r = f(c);
// endmodule

module attributes {coverage.language_version = 2023 : i32} {
  talos.sv.symbol.definition @s0.top attributes {definition_kind = 0 : i32, hierarchical_name = "top", name = "top", node_id = 0 : i64} {
  }
  talos.sv.symbol.root @s1.$root attributes {hierarchical_name = "\\$root ", name = "$root", node_id = 1 : i64} {
    talos.sv.symbol.compilation_unit @s2 attributes {hierarchical_name = "$unit", node_id = 2 : i64, simulation.vpi_definition_name = "$unit"} {
    }
    talos.sv.symbol.instance @s3.top attributes {hierarchical_name = "top", is_uninstantiated = false, name = "top", node_id = 3 : i64, referenced_path = "top", referenced_symbol = @s0.top} {
      talos.sv.symbol.instance_body @s4.top attributes {hierarchical_name = "top", name = "top", node_id = 4 : i64, simulation.vpi_automatic = false, simulation.vpi_cell_instance = false, simulation.vpi_definition_name = "top", simulation.vpi_top = true, time_precision_fs = 1000000 : i64, time_unit_fs = 1000000 : i64, vpi_scope_kind = 32 : i32} {
        talos.sv.symbol.variable @s5.c attributes {hierarchical_name = "top.c", lifetime = 1 : i32, name = "c", node_id = 5 : i64, semantic_type = !talos.integral<1, false, true, 0 : 0, logic>} {
        }
        talos.sv.symbol.variable @s6.a attributes {hierarchical_name = "top.a", lifetime = 1 : i32, name = "a", node_id = 6 : i64, semantic_type = !talos.integral<1, false, true, 0 : 0, logic>} {
        }
        talos.sv.symbol.variable @s7.h attributes {hierarchical_name = "top.h", lifetime = 1 : i32, name = "h", node_id = 7 : i64, semantic_type = !talos.integral<1, false, true, 0 : 0, logic>} {
        }
        talos.sv.symbol.variable @s8.o attributes {hierarchical_name = "top.o", lifetime = 1 : i32, name = "o", node_id = 8 : i64, semantic_type = !talos.integral<1, false, true, 0 : 0, logic>} {
        }
        talos.sv.symbol.variable @s9.p attributes {hierarchical_name = "top.p", lifetime = 1 : i32, name = "p", node_id = 9 : i64, semantic_type = !talos.integral<1, false, true, 0 : 0, logic>} {
        }
        talos.sv.symbol.variable @s10.q attributes {hierarchical_name = "top.q", lifetime = 1 : i32, name = "q", node_id = 10 : i64, semantic_type = !talos.integral<1, false, true, 0 : 0, logic>} {
        }
        talos.sv.symbol.variable @s11.r attributes {hierarchical_name = "top.r", lifetime = 1 : i32, name = "r", node_id = 11 : i64, semantic_type = !talos.integral<1, false, true, 0 : 0, logic>} {
        }
        talos.sv.symbol.subroutine @s12.t attributes {hierarchical_name = "top.t", name = "t", node_id = 12 : i64, semantic_type = !talos.subroutine<(!talos.integral<1, false, true, 0 : 0, logic>) -> (), true>, subroutine_kind = 1 : i32, time_precision_fs = 1000000 : i64, time_unit_fs = 1000000 : i64} {
          talos.sv.statement.expression_statement attributes {node_id = 13 : i64} {
            talos.sv.expression.assignment attributes {assignment_kind = 0 : i32, is_signed = false, node_id = 14 : i64, semantic_type = !talos.integral<1, false, true, 0 : 0, logic>} {
              talos.sv.expression.named_value attributes {is_signed = false, node_id = 15 : i64, referenced_path = "top.p", referenced_symbol = @s1.$root::@s3.top::@s4.top::@s9.p, semantic_type = !talos.integral<1, false, true, 0 : 0, logic>} {
              }
              talos.sv.expression.binary_op attributes {is_signed = false, node_id = 16 : i64, operator_kind = 6 : i32, semantic_type = !talos.integral<1, false, true, 0 : 0, logic>} {
                talos.sv.expression.named_value attributes {is_signed = false, node_id = 17 : i64, referenced_path = "top.t.v", referenced_symbol = @s1.$root::@s3.top::@s4.top::@s12.t::@s13.v, semantic_type = !talos.integral<1, false, true, 0 : 0, logic>} {
                }
                talos.sv.expression.named_value attributes {is_signed = false, node_id = 18 : i64, referenced_path = "top.h", referenced_symbol = @s1.$root::@s3.top::@s4.top::@s7.h, semantic_type = !talos.integral<1, false, true, 0 : 0, logic>} {
                }
              }
            }
          }
          talos.sv.symbol.formal_argument @s13.v attributes {direction = 0 : i32, hierarchical_name = "top.t.v", name = "v", node_id = 19 : i64, semantic_type = !talos.integral<1, false, true, 0 : 0, logic>} {
          }
        }
        talos.sv.symbol.subroutine @s14.f attributes {hierarchical_name = "top.f", name = "f", node_id = 20 : i64, return_variable_path = "top.f.f", return_variable_symbol = @s1.$root::@s3.top::@s4.top::@s14.f::@s16.f, semantic_type = !talos.subroutine<(!talos.integral<1, false, true, 0 : 0, logic>) -> !talos.integral<1, false, true, 0 : 0, logic>, false>, subroutine_kind = 0 : i32, time_precision_fs = 1000000 : i64, time_unit_fs = 1000000 : i64} {
          talos.sv.statement.return attributes {node_id = 21 : i64} {
            talos.sv.expression.binary_op attributes {is_signed = false, node_id = 22 : i64, operator_kind = 6 : i32, semantic_type = !talos.integral<1, false, true, 0 : 0, logic>} {
              talos.sv.expression.named_value attributes {is_signed = false, node_id = 23 : i64, referenced_path = "top.f.v", referenced_symbol = @s1.$root::@s3.top::@s4.top::@s14.f::@s15.v, semantic_type = !talos.integral<1, false, true, 0 : 0, logic>} {
              }
              talos.sv.expression.named_value attributes {is_signed = false, node_id = 24 : i64, referenced_path = "top.h", referenced_symbol = @s1.$root::@s3.top::@s4.top::@s7.h, semantic_type = !talos.integral<1, false, true, 0 : 0, logic>} {
              }
            }
          }
          talos.sv.symbol.formal_argument @s15.v attributes {direction = 0 : i32, hierarchical_name = "top.f.v", name = "v", node_id = 25 : i64, semantic_type = !talos.integral<1, false, true, 0 : 0, logic>} {
          }
          talos.sv.symbol.variable @s16.f attributes {hierarchical_name = "top.f.f", is_compiler_generated, name = "f", node_id = 26 : i64, semantic_type = !talos.integral<1, false, true, 0 : 0, logic>} {
          }
        }
        talos.sv.symbol.procedural_block @s17 attributes {hierarchical_name = "top", node_id = 27 : i64, procedure_kind = 3 : i32, time_precision_fs = 1000000 : i64, time_unit_fs = 1000000 : i64} {
          talos.sv.statement.immediate_assertion attributes {assertion_kind = 0 : i32, has_fail_action = false, has_pass_action = true, is_deferred = false, is_final = false, node_id = 28 : i64} {
            talos.sv.expression.named_value attributes {is_signed = false, node_id = 29 : i64, referenced_path = "top.c", referenced_symbol = @s1.$root::@s3.top::@s4.top::@s5.c, semantic_type = !talos.integral<1, false, true, 0 : 0, logic>} {
            }
            talos.sv.statement.expression_statement attributes {node_id = 30 : i64} {
              talos.sv.expression.assignment attributes {assignment_kind = 0 : i32, is_signed = false, node_id = 31 : i64, semantic_type = !talos.integral<1, false, true, 0 : 0, logic>} {
                talos.sv.expression.named_value attributes {is_signed = false, node_id = 32 : i64, referenced_path = "top.o", referenced_symbol = @s1.$root::@s3.top::@s4.top::@s8.o, semantic_type = !talos.integral<1, false, true, 0 : 0, logic>} {
                }
                talos.sv.expression.named_value attributes {is_signed = false, node_id = 33 : i64, referenced_path = "top.a", referenced_symbol = @s1.$root::@s3.top::@s4.top::@s6.a, semantic_type = !talos.integral<1, false, true, 0 : 0, logic>} {
                }
              }
            }
          }
        }
        talos.sv.symbol.procedural_block @s18 attributes {hierarchical_name = "top", node_id = 34 : i64, procedure_kind = 3 : i32, time_precision_fs = 1000000 : i64, time_unit_fs = 1000000 : i64} {
          talos.sv.statement.expression_statement attributes {node_id = 35 : i64} {
            talos.sv.expression.call attributes {argument_count = 1 : i64, callee_name = "t", constraint_restrictions = [], defaulted_arguments = array<i64: 0>, has_inline_constraints = false, has_iterator_expression = false, has_output_arguments = false, has_this_class = false, is_signed = false, is_super_class = false, is_system_call = false, node_id = 36 : i64, referenced_path = "top.t", referenced_symbol = @s1.$root::@s3.top::@s4.top::@s12.t, semantic_type = !talos.void, subroutine_kind = 1 : i32} {
              talos.sv.expression.named_value attributes {is_signed = false, node_id = 37 : i64, referenced_path = "top.c", referenced_symbol = @s1.$root::@s3.top::@s4.top::@s5.c, semantic_type = !talos.integral<1, false, true, 0 : 0, logic>} {
              }
            }
          }
        }
        talos.sv.symbol.procedural_block @s19 attributes {hierarchical_name = "top", node_id = 38 : i64, procedure_kind = 2 : i32, time_precision_fs = 1000000 : i64, time_unit_fs = 1000000 : i64} {
          talos.sv.statement.timed attributes {node_id = 39 : i64} {
            talos.sv.timing.implicit_event attributes {node_id = 40 : i64} {
            }
            talos.sv.statement.expression_statement attributes {node_id = 41 : i64} {
              talos.sv.expression.assignment attributes {assignment_kind = 0 : i32, is_signed = false, node_id = 42 : i64, semantic_type = !talos.integral<1, false, true, 0 : 0, logic>} {
                talos.sv.expression.named_value attributes {is_signed = false, node_id = 43 : i64, referenced_path = "top.q", referenced_symbol = @s1.$root::@s3.top::@s4.top::@s10.q, semantic_type = !talos.integral<1, false, true, 0 : 0, logic>} {
                }
                talos.sv.expression.call attributes {argument_count = 1 : i64, callee_name = "f", constraint_restrictions = [], defaulted_arguments = array<i64: 0>, has_inline_constraints = false, has_iterator_expression = false, has_output_arguments = false, has_this_class = false, is_signed = false, is_super_class = false, is_system_call = false, node_id = 44 : i64, referenced_path = "top.f", referenced_symbol = @s1.$root::@s3.top::@s4.top::@s14.f, semantic_type = !talos.integral<1, false, true, 0 : 0, logic>, subroutine_kind = 0 : i32} {
                  talos.sv.expression.named_value attributes {is_signed = false, node_id = 45 : i64, referenced_path = "top.c", referenced_symbol = @s1.$root::@s3.top::@s4.top::@s5.c, semantic_type = !talos.integral<1, false, true, 0 : 0, logic>} {
                  }
                }
              }
            }
          }
        }
        talos.sv.symbol.procedural_block @s20 attributes {hierarchical_name = "top", node_id = 46 : i64, procedure_kind = 3 : i32, time_precision_fs = 1000000 : i64, time_unit_fs = 1000000 : i64} {
          talos.sv.statement.expression_statement attributes {node_id = 47 : i64} {
            talos.sv.expression.assignment attributes {assignment_kind = 0 : i32, is_signed = false, node_id = 48 : i64, semantic_type = !talos.integral<1, false, true, 0 : 0, logic>} {
              talos.sv.expression.named_value attributes {is_signed = false, node_id = 49 : i64, referenced_path = "top.r", referenced_symbol = @s1.$root::@s3.top::@s4.top::@s11.r, semantic_type = !talos.integral<1, false, true, 0 : 0, logic>} {
              }
              talos.sv.expression.call attributes {argument_count = 1 : i64, callee_name = "f", constraint_restrictions = [], defaulted_arguments = array<i64: 0>, has_inline_constraints = false, has_iterator_expression = false, has_output_arguments = false, has_this_class = false, is_signed = false, is_super_class = false, is_system_call = false, node_id = 50 : i64, referenced_path = "top.f", referenced_symbol = @s1.$root::@s3.top::@s4.top::@s14.f, semantic_type = !talos.integral<1, false, true, 0 : 0, logic>, subroutine_kind = 0 : i32} {
                talos.sv.expression.named_value attributes {is_signed = false, node_id = 51 : i64, referenced_path = "top.c", referenced_symbol = @s1.$root::@s3.top::@s4.top::@s5.c, semantic_type = !talos.integral<1, false, true, 0 : 0, logic>} {
                }
              }
            }
          }
        }
      }
    }
  }
}


// CHECK-LABEL: simulation.func private @unit_2(
// CHECK-SAME: %[[ASSERT_C:[a-z0-9]+]]: !simulation.ref<!simulation.logic<1>> {simulation.capture_kind = 3 : i32, simulation.descriptor_id = 0 : i64}
// CHECK-SAME: %[[ASSERT_A:[a-z0-9]+]]: !simulation.ref<!simulation.logic<1>> {simulation.capture_kind = 3 : i32, simulation.descriptor_id = 1 : i64}
// CHECK: simulation.ref.load %[[ASSERT_A]]
// CHECK: simulation.suspend.change %[[ASSERT_C]] to

// CHECK-LABEL: simulation.func private @unit_3(
// CHECK-SAME: %[[TASK_C:[a-z0-9]+]]: !simulation.ref<!simulation.logic<1>> {simulation.capture_kind = 3 : i32, simulation.descriptor_id = 0 : i64})
// CHECK: simulation.task.call @unit_0
// CHECK: simulation.suspend.change %[[TASK_C]] to

// CHECK-LABEL: simulation.func private @unit_4(
// CHECK-SAME: %[[STAR_C:[a-z0-9]+]]: !simulation.ref<!simulation.logic<1>> {simulation.capture_kind = 3 : i32, simulation.descriptor_id = 0 : i64}
// CHECK: simulation.suspend.change %[[STAR_C]] to
// CHECK: simulation.call @unit_1

// CHECK-LABEL: simulation.func private @unit_5(
// CHECK-SAME: %[[COMB_C:[a-z0-9]+]]: !simulation.ref<!simulation.logic<1>> {simulation.capture_kind = 3 : i32, simulation.descriptor_id = 0 : i64}
// CHECK-SAME: %[[COMB_H:[a-z0-9]+]]: !simulation.ref<!simulation.logic<1>> {simulation.capture_kind = 3 : i32, simulation.descriptor_id = 2 : i64}
// CHECK: simulation.call @unit_1
// CHECK: simulation.suspend.any %[[COMB_C]], %[[COMB_H]] edges [0, 0]
