// RUN: obelisk-opt %s '--lower-obelisk-to-sim=opt-level=0' | FileCheck %s

module {
  obelisk.sv.symbol.definition attributes {
    definition_kind = 0 : i32,
    hierarchical_name = "disable_task",
    name = "disable_task",
    node_id = 0 : i64,
    sym_name = "s0.disable_task"
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
      obelisk.sv.symbol.subroutine attributes {
        hierarchical_name = "worker",
        name = "worker",
        node_id = 3 : i64,
        semantic_type = !obelisk.subroutine<() -> (), true>,
        subroutine_kind = 1 : i32,
        sym_name = "s3.worker",
        time_precision_fs = 1000000 : i64,
        time_unit_fs = 1000000 : i64
      } {
        obelisk.sv.statement.timed attributes {
          node_id = 4 : i64
        } {
          obelisk.sv.timing.delay attributes {
            node_id = 5 : i64
          } {
            obelisk.sv.expression.integer_literal attributes {
              constant_value = "1",
              node_id = 6 : i64,
              semantic_type = !obelisk.integral<32, true, false, 31 : 0, int>
            } {
            }
          }
          obelisk.sv.statement.empty attributes {
            node_id = 7 : i64
          } {
          }
        }
      }
    }
    obelisk.sv.symbol.instance attributes {
      hierarchical_name = "disable_task",
      is_uninstantiated = false,
      name = "disable_task",
      node_id = 8 : i64,
      referenced_path = "disable_task",
      referenced_symbol = @s0.disable_task,
      sym_name = "s4.disable_task"
    } {
      obelisk.sv.symbol.instance_body attributes {
        hierarchical_name = "disable_task",
        name = "disable_task",
        node_id = 9 : i64,
        sym_name = "s5.disable_task"
      } {
        obelisk.sv.symbol.procedural_block attributes {
          hierarchical_name = "disable_task",
          node_id = 10 : i64,
          procedure_kind = 0 : i32,
          sym_name = "s6",
          time_precision_fs = 1000000 : i64,
          time_unit_fs = 1000000 : i64
        } {
          obelisk.sv.statement.block attributes {
            node_id = 11 : i64
          } {
            obelisk.sv.statement.list attributes {
              node_id = 12 : i64
            } {
              obelisk.sv.statement.expression_statement attributes {
                node_id = 13 : i64
              } {
                obelisk.sv.expression.call attributes {
                  argument_count = 0 : i64,
                  callee_name = "worker",
                  constraint_restrictions = [],
                  has_inline_constraints = false,
                  has_iterator_expression = false,
                  has_output_arguments = false,
                  has_this_class = false,
                  is_super_class = false,
                  is_system_call = false,
                  node_id = 14 : i64,
                  referenced_path = "worker",
                  referenced_symbol = @s1.$root::@s2::@s3.worker,
                  semantic_type = !obelisk.void,
                  subroutine_kind = 1 : i32
                } {
                }
              }
              obelisk.sv.statement.disable attributes {
                node_id = 15 : i64,
                target_path = "worker",
                target_symbol = @s1.$root::@s2::@s3.worker
              } {
              }
            }
          }
        }
      }
    }
  }
}

// A task is itself a dynamically activated control target. Its target ID must
// match an independently lowered hierarchical disable, survive suspension,
// and be released on normal return.
// CHECK-LABEL: obelisk_sim.func private @unit_0(
// CHECK-SAME: obelisk_sim.control_target_id = [[TASK_ID:[0-9]+]] : i64
// CHECK-SAME: obelisk_sim.hierarchical_name = "worker"
// CHECK: %[[ACTIVATION:.*]] = obelisk_sim.control.enter [[TASK_ID]]
// CHECK: obelisk_sim.suspend.delay {{.*}}(%[[ACTIVATION]] : !obelisk_sim.control)
// CHECK: ^{{.*}}(%[[RESUMED:.*]]: !obelisk_sim.control):
// CHECK: obelisk_sim.control.leave %[[RESUMED]]
// CHECK: obelisk_sim.return

// CHECK-LABEL: obelisk_sim.func private @unit_1(
// CHECK: obelisk_sim.control.disable [[TASK_ID]] {hierarchical = true}
// CHECK: obelisk_sim.return
