// RUN: obelisk-opt %s '--lower-obelisk-to-sim=opt-level=0' | FileCheck %s

module {
  obelisk.sv.symbol.definition attributes {
    definition_kind = 0 : i32,
    hierarchical_name = "queue_ref_task",
    name = "queue_ref_task",
    node_id = 0 : i64,
    sym_name = "s0.queue_ref_task"
  } {
  }
  obelisk.sv.symbol.root attributes {
    hierarchical_name = "\\$root ",
    name = "$root",
    node_id = 1 : i64,
    sym_name = "s1.$root"
  } {
    obelisk.sv.symbol.compilation_unit attributes {
      hierarchical_name = "$unit",
      node_id = 2 : i64,
      sym_name = "s2"
    } {
    }
    obelisk.sv.symbol.instance attributes {
      hierarchical_name = "queue_ref_task",
      is_uninstantiated = false,
      name = "queue_ref_task",
      node_id = 3 : i64,
      referenced_path = "queue_ref_task",
      referenced_symbol = @s0.queue_ref_task,
      sym_name = "s3.queue_ref_task"
    } {
      obelisk.sv.symbol.instance_body attributes {
        hierarchical_name = "queue_ref_task",
        name = "queue_ref_task",
        node_id = 4 : i64,
        sym_name = "s4.queue_ref_task"
      } {
        obelisk.sv.symbol.variable attributes {
          hierarchical_name = "queue_ref_task.queue",
          lifetime = 1 : i32,
          name = "queue",
          node_id = 5 : i64,
          semantic_type = !obelisk.queue<!obelisk.integral<32, true, false, 31 : 0, int>, 0>,
          sym_name = "s5.queue"
        } {
        }
        obelisk.sv.symbol.subroutine attributes {
          hierarchical_name = "queue_ref_task.update",
          name = "update",
          node_id = 6 : i64,
          semantic_type = !obelisk.subroutine<(!obelisk.integral<32, true, false, 31 : 0, int>) -> (), true>,
          subroutine_kind = 1 : i32,
          sym_name = "s6.update",
          time_precision_fs = 1000000 : i64,
          time_unit_fs = 1000000 : i64
        } {
          obelisk.sv.statement.expression_statement attributes {
            node_id = 7 : i64
          } {
            obelisk.sv.expression.assignment attributes {
              assignment_kind = 0 : i32,
              node_id = 8 : i64,
              semantic_type = !obelisk.integral<32, true, false, 31 : 0, int>
            } {
              obelisk.sv.expression.named_value attributes {
                node_id = 9 : i64,
                referenced_path = "queue_ref_task.update.element",
                referenced_symbol = @s1.$root::@s3.queue_ref_task::@s4.queue_ref_task::@s6.update::@s7.element,
                semantic_type = !obelisk.integral<32, true, false, 31 : 0, int>
              } {
              }
              obelisk.sv.expression.integer_literal attributes {
                constant_value = "10",
                node_id = 10 : i64,
                semantic_type = !obelisk.integral<32, true, false, 31 : 0, int>
              } {
              }
            }
          }
          obelisk.sv.symbol.formal_argument attributes {
            direction = 3 : i32,
            hierarchical_name = "queue_ref_task.update.element",
            name = "element",
            node_id = 11 : i64,
            semantic_type = !obelisk.integral<32, true, false, 31 : 0, int>,
            sym_name = "s7.element"
          } {
          }
        }
        obelisk.sv.symbol.procedural_block attributes {
          hierarchical_name = "queue_ref_task",
          node_id = 12 : i64,
          procedure_kind = 0 : i32,
          sym_name = "s8",
          time_precision_fs = 1000000 : i64,
          time_unit_fs = 1000000 : i64
        } {
          obelisk.sv.statement.expression_statement attributes {
            node_id = 13 : i64
          } {
            obelisk.sv.expression.call attributes {
              argument_count = 1 : i64,
              callee_name = "update",
              constraint_restrictions = [],
              has_inline_constraints = false,
              has_iterator_expression = false,
              has_output_arguments = true,
              has_this_class = false,
              is_super_class = false,
              is_system_call = false,
              node_id = 14 : i64,
              referenced_path = "queue_ref_task.update",
              referenced_symbol = @s1.$root::@s3.queue_ref_task::@s4.queue_ref_task::@s6.update,
              semantic_type = !obelisk.void,
              subroutine_kind = 1 : i32
            } {
              obelisk.sv.expression.element_select attributes {
                node_id = 15 : i64,
                semantic_type = !obelisk.integral<32, true, false, 31 : 0, int>
              } {
                obelisk.sv.expression.named_value attributes {
                  node_id = 16 : i64,
                  referenced_path = "queue_ref_task.queue",
                  referenced_symbol = @s1.$root::@s3.queue_ref_task::@s4.queue_ref_task::@s5.queue,
                  semantic_type = !obelisk.queue<!obelisk.integral<32, true, false, 31 : 0, int>, 0>
                } {
                }
                obelisk.sv.expression.integer_literal attributes {
                  constant_value = "1",
                  node_id = 17 : i64,
                  semantic_type = !obelisk.integral<32, true, false, 31 : 0, int>
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

// The task ABI uses a uniform persistent argument reference for a ref formal.
// CHECK-LABEL: obelisk_sim.func private @unit_0(
// CHECK-SAME: !obelisk_sim.argument_ref<i32>
// CHECK: obelisk_sim.argument_ref.store

// An indexed queue actual becomes a persistent path watched through its owner.
// CHECK-LABEL: obelisk_sim.func private @unit_1(
// CHECK: obelisk_sim.argument_ref.from_ref
// CHECK: %[[PATH:.*]] = obelisk_sim.reference_path.index
// CHECK: %[[REF:.*]] = obelisk_sim.argument_ref.from_path %[[PATH]]
// CHECK: obelisk_sim.task.call @unit_0({{.*}}, %[[REF]])
// CHECK-SAME: !obelisk_sim.argument_ref<i32>
