// RUN: obelisk-opt %s '--lower-obelisk-to-sim=opt-level=0' | FileCheck %s

module {
  obelisk.sv.symbol.definition @s0.queue_push_front_lowering attributes {
    definition_kind = 0 : i32,
    hierarchical_name = "queue_push_front_lowering",
    name = "queue_push_front_lowering",
    node_id = 0 : i64
  } {
  }
  obelisk.sv.symbol.root @s1.$root attributes {
    hierarchical_name = "\\$root ",
    name = "$root",
    node_id = 1 : i64
  } {
    obelisk.sv.symbol.compilation_unit @s2 attributes {
      hierarchical_name = "$unit",
      node_id = 2 : i64
    } {
    }
    obelisk.sv.symbol.instance @s3.queue_push_front_lowering attributes {
      hierarchical_name = "queue_push_front_lowering",
      is_uninstantiated = false,
      name = "queue_push_front_lowering",
      node_id = 3 : i64,
      referenced_path = "queue_push_front_lowering",
      referenced_symbol = @s0.queue_push_front_lowering
    } {
      obelisk.sv.symbol.instance_body @s4.queue_push_front_lowering attributes {
        hierarchical_name = "queue_push_front_lowering",
        name = "queue_push_front_lowering",
        node_id = 4 : i64
      } {
        obelisk.sv.symbol.variable @s5.queue attributes {
          hierarchical_name = "queue_push_front_lowering.queue",
          lifetime = 1 : i32,
          name = "queue",
          node_id = 5 : i64,
          semantic_type = !obelisk.queue<!obelisk.integral<32, true, false, 31 : 0, int>, 0>
        } {
        }
        obelisk.sv.symbol.variable @s20.result attributes {
          hierarchical_name = "queue_push_front_lowering.result",
          lifetime = 1 : i32,
          name = "result",
          node_id = 20 : i64,
          semantic_type = !obelisk.integral<32, true, false, 31 : 0, int>
        } {
        }
        obelisk.sv.symbol.procedural_block @s6 attributes {
          hierarchical_name = "queue_push_front_lowering",
          node_id = 6 : i64,
          procedure_kind = 0 : i32,
          time_precision_fs = 1000000 : i64,
          time_unit_fs = 1000000 : i64
        } {
          obelisk.sv.statement.expression_statement attributes {
            node_id = 7 : i64
          } {
            obelisk.sv.expression.call attributes {
              argument_count = 2 : i64,
              callee_name = "push_front",
              constraint_restrictions = [],
              has_inline_constraints = false,
              has_iterator_expression = false,
              has_output_arguments = false,
              has_this_class = false,
              is_super_class = false,
              is_system_call = true,
              node_id = 8 : i64,
              semantic_type = !obelisk.void,
              subroutine_kind = 0 : i32,
              system_library_cell = "work.queue_push_front_lowering",
              system_scope_path = "queue_push_front_lowering",
              system_scope_symbol = @s1.$root::@s3.queue_push_front_lowering::@s4.queue_push_front_lowering
            } {
              obelisk.sv.expression.named_value attributes {
                node_id = 9 : i64,
                referenced_path = "queue_push_front_lowering.queue",
                referenced_symbol = @s1.$root::@s3.queue_push_front_lowering::@s4.queue_push_front_lowering::@s5.queue,
                semantic_type = !obelisk.queue<!obelisk.integral<32, true, false, 31 : 0, int>, 0>
              } {
              }
              obelisk.sv.expression.integer_literal attributes {
                constant_value = "42",
                node_id = 10 : i64,
                semantic_type = !obelisk.integral<32, true, false, 31 : 0, int>
              } {
              }
            }
          }
        }
        obelisk.sv.symbol.procedural_block @s11 attributes {
          hierarchical_name = "queue_push_front_lowering",
          node_id = 11 : i64,
          procedure_kind = 0 : i32,
          time_precision_fs = 1000000 : i64,
          time_unit_fs = 1000000 : i64
        } {
          obelisk.sv.statement.expression_statement attributes {
            node_id = 12 : i64
          } {
            obelisk.sv.expression.assignment attributes {
              assignment_kind = 0 : i32,
              node_id = 13 : i64,
              semantic_type = !obelisk.integral<32, true, false, 31 : 0, int>
            } {
              obelisk.sv.expression.named_value attributes {
                node_id = 14 : i64,
                referenced_path = "queue_push_front_lowering.result",
                referenced_symbol = @s1.$root::@s3.queue_push_front_lowering::@s4.queue_push_front_lowering::@s20.result,
                semantic_type = !obelisk.integral<32, true, false, 31 : 0, int>
              } {
              }
              obelisk.sv.expression.call attributes {
                argument_count = 1 : i64,
                callee_name = "pop_front",
                constraint_restrictions = [],
                has_inline_constraints = false,
                has_iterator_expression = false,
                has_output_arguments = false,
                has_this_class = false,
                is_super_class = false,
                is_system_call = true,
                node_id = 15 : i64,
                semantic_type = !obelisk.integral<32, true, false, 31 : 0, int>,
                subroutine_kind = 0 : i32,
                system_library_cell = "work.queue_push_front_lowering",
                system_scope_path = "queue_push_front_lowering",
                system_scope_symbol = @s1.$root::@s3.queue_push_front_lowering::@s4.queue_push_front_lowering
              } {
                obelisk.sv.expression.named_value attributes {
                node_id = 16 : i64,
                referenced_path = "queue_push_front_lowering.queue",
                referenced_symbol = @s1.$root::@s3.queue_push_front_lowering::@s4.queue_push_front_lowering::@s5.queue,
                semantic_type = !obelisk.queue<!obelisk.integral<32, true, false, 31 : 0, int>, 0>
                } {
                }
              }
            }
          }
        }
        obelisk.sv.symbol.procedural_block @s21 attributes {
          hierarchical_name = "queue_push_front_lowering",
          node_id = 21 : i64,
          procedure_kind = 0 : i32,
          time_precision_fs = 1000000 : i64,
          time_unit_fs = 1000000 : i64
        } {
          obelisk.sv.statement.expression_statement attributes {
            node_id = 22 : i64
          } {
            obelisk.sv.expression.assignment attributes {
              assignment_kind = 0 : i32,
              node_id = 23 : i64,
              semantic_type = !obelisk.integral<32, true, false, 31 : 0, int>
            } {
              obelisk.sv.expression.named_value attributes {
                node_id = 24 : i64,
                referenced_path = "queue_push_front_lowering.result",
                referenced_symbol = @s1.$root::@s3.queue_push_front_lowering::@s4.queue_push_front_lowering::@s20.result,
                semantic_type = !obelisk.integral<32, true, false, 31 : 0, int>
              } {
              }
              obelisk.sv.expression.call attributes {
                argument_count = 1 : i64,
                callee_name = "pop_back",
                constraint_restrictions = [],
                has_inline_constraints = false,
                has_iterator_expression = false,
                has_output_arguments = false,
                has_this_class = false,
                is_super_class = false,
                is_system_call = true,
                node_id = 25 : i64,
                semantic_type = !obelisk.integral<32, true, false, 31 : 0, int>,
                subroutine_kind = 0 : i32,
                system_library_cell = "work.queue_push_front_lowering",
                system_scope_path = "queue_push_front_lowering",
                system_scope_symbol = @s1.$root::@s3.queue_push_front_lowering::@s4.queue_push_front_lowering
              } {
                obelisk.sv.expression.named_value attributes {
                  node_id = 26 : i64,
                  referenced_path = "queue_push_front_lowering.queue",
                  referenced_symbol = @s1.$root::@s3.queue_push_front_lowering::@s4.queue_push_front_lowering::@s5.queue,
                  semantic_type = !obelisk.queue<!obelisk.integral<32, true, false, 31 : 0, int>, 0>
                } {
                }
              }
            }
          }
        }
      }
    }
  }
}

// push_front lowers to insertion at index zero.
// CHECK: arith.constant {{.*}}0 : i64
// CHECK: simulation.queue.insert
// CHECK: simulation.ref.store

// pop_front reads and removes the element at index zero.
// CHECK-LABEL: simulation.func private @unit_1
// CHECK: %[[POP_ZERO:.*]] = arith.constant {{.*}}0 : i64
// CHECK: simulation.container.read {{.*}}, %[[POP_ZERO]]
// CHECK: %[[DELETE_ZERO:.*]] = arith.constant {{.*}}0 : i64
// CHECK: simulation.queue.delete {{.*}}[%[[DELETE_ZERO]]]

// pop_back reads and removes the element at size - 1.
// CHECK-LABEL: simulation.func private @unit_2
// CHECK: %[[SIZE:.*]] = simulation.container.size
// CHECK: %[[LAST:.*]] = arith.subi %[[SIZE]],
// CHECK: simulation.container.read {{.*}}, %[[LAST]]
// CHECK: simulation.queue.delete {{.*}}[%[[LAST]]]
