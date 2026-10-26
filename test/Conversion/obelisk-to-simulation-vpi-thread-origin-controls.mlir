// RUN: obelisk-opt %s '--lower-obelisk-to-sim=opt-level=0' -o %t
// RUN: FileCheck %s < %t
// RUN: env OBELISK_TEST_INPUT=%t %obj_root/test/obelisk-design-database-dump-test --gtest_filter=GeneratedDesignDatabase.StatementForms
// RUN: obelisk-opt %s '--lower-obelisk-to-sim=opt-level=3' '--encode-obelisk-sim-to-bytecode=vpi=read' -o /dev/null

// For controls preserve source order, with singular access to ordinal zero.
// Task/function body cardinality follows LRM37.12d5; fork origins retain the
// task as lexical owner, while function-call statements keep their exact kind.
module attributes {llvm.data_layout = "e-m:e-p:64:64-i64:64-n8:16:32:64-S128", llvm.target_triple = "x86_64-unknown-linux-gnu"} {
  obelisk.sv.symbol.definition @definition attributes {definition_kind = 0 : i32, hierarchical_name = "top", name = "top", node_id = 38 : i64} {
  }
  obelisk.sv.symbol.root @root attributes {hierarchical_name = "$root", name = "$root", node_id = 41 : i64} {
    obelisk.sv.symbol.instance @top attributes {hierarchical_name = "top", name = "top", is_uninstantiated = false, referenced_path = "top", referenced_symbol = @definition, node_id = 40 : i64} {
      obelisk.sv.symbol.instance_body @body attributes {hierarchical_name = "top", name = "top", time_precision_fs = 1 : i64, time_unit_fs = 1 : i64, node_id = 39 : i64} {
        obelisk.sv.symbol.variable @i attributes {hierarchical_name = "top.i", name = "i", lifetime = 1 : i32, semantic_type = !obelisk.integral<32, true, false, 31 : 0, int>, node_id = 1 : i64} {
        }
        obelisk.sv.symbol.variable @j attributes {hierarchical_name = "top.j", name = "j", lifetime = 1 : i32, semantic_type = !obelisk.integral<32, true, false, 31 : 0, int>, node_id = 2 : i64} {
        }
        obelisk.sv.symbol.subroutine @worker attributes {hierarchical_name = "top.worker", name = "worker", subroutine_kind = 1 : i32, semantic_type = !obelisk.subroutine<() -> (), true>, time_precision_fs = 1 : i64, time_unit_fs = 1 : i64, node_id = 25 : i64} {
          obelisk.sv.statement.list attributes {node_id = 24 : i64} {
            obelisk.sv.statement.empty attributes {node_id = 23 : i64} {
            }
            obelisk.sv.statement.block attributes {block_kind = 3 : i32, node_id = 22 : i64} {
              obelisk.sv.statement.timed attributes {node_id = 21 : i64} {
                obelisk.sv.timing.delay attributes {node_id = 19 : i64} {
                  obelisk.sv.expression.integer_literal attributes {constant_value = "1", semantic_type = !obelisk.integral<32, true, false, 31 : 0, int>, node_id = 18 : i64} {
                  }
                }
                obelisk.sv.statement.empty attributes {node_id = 20 : i64} {
                }
              }
            }
            obelisk.sv.statement.procedural_assign attributes {is_force = false, node_id = 210 : i64} {
              obelisk.sv.expression.assignment attributes {assignment_kind = 0 : i32, semantic_type = !obelisk.integral<32, true, false, 31 : 0, int>, node_id = 211 : i64} {
                obelisk.sv.expression.named_value attributes {referenced_symbol = @root::@top::@body::@i, referenced_path = "top.i", semantic_type = !obelisk.integral<32, true, false, 31 : 0, int>, node_id = 212 : i64} {}
                obelisk.sv.expression.integer_literal attributes {constant_value = "7", semantic_type = !obelisk.integral<32, true, false, 31 : 0, int>, node_id = 213 : i64} {}
              }
            }
            obelisk.sv.statement.procedural_deassign attributes {is_release = false, node_id = 214 : i64} {
              obelisk.sv.expression.named_value attributes {referenced_symbol = @root::@top::@body::@i, referenced_path = "top.i", semantic_type = !obelisk.integral<32, true, false, 31 : 0, int>, node_id = 215 : i64} {}
            }
          }
        }
        obelisk.sv.symbol.subroutine @single attributes {hierarchical_name = "top.single", name = "single", subroutine_kind = 1 : i32, semantic_type = !obelisk.subroutine<() -> (), true>, time_precision_fs = 1 : i64, time_unit_fs = 1 : i64, node_id = 28 : i64} {
          obelisk.sv.statement.list attributes {node_id = 27 : i64} {
            obelisk.sv.statement.empty attributes {node_id = 26 : i64} {
            }
          }
        }
        obelisk.sv.symbol.subroutine @zero attributes {hierarchical_name = "top.zero", name = "zero", subroutine_kind = 1 : i32, semantic_type = !obelisk.subroutine<() -> (), true>, time_precision_fs = 1 : i64, time_unit_fs = 1 : i64, node_id = 30 : i64} {
          obelisk.sv.statement.list attributes {node_id = 29 : i64} {
          }
        }
        obelisk.sv.symbol.subroutine @noop attributes {hierarchical_name = "top.noop", name = "noop", subroutine_kind = 0 : i32, semantic_type = !obelisk.subroutine<() -> !obelisk.void, false>, time_precision_fs = 1 : i64, time_unit_fs = 1 : i64, node_id = 33 : i64} {
          obelisk.sv.statement.list attributes {node_id = 32 : i64} {
            obelisk.sv.statement.empty attributes {node_id = 31 : i64} {
            }
          }
        }
        obelisk.sv.symbol.procedural_block @loop attributes {hierarchical_name = "top", procedure_kind = 0 : i32, time_precision_fs = 1 : i64, time_unit_fs = 1 : i64, node_id = 35 : i64} {
          obelisk.sv.statement.for_loop attributes {initializer_count = 2 : i64, has_condition = true, step_count = 2 : i64, node_id = 17 : i64} {
            obelisk.sv.expression.assignment attributes {assignment_kind = 0 : i32, semantic_type = !obelisk.integral<32, true, false, 31 : 0, int>, node_id = 5 : i64} {
              obelisk.sv.expression.named_value attributes {referenced_symbol = @root::@top::@body::@i, referenced_path = "top.i", semantic_type = !obelisk.integral<32, true, false, 31 : 0, int>, node_id = 3 : i64} {
              }
              obelisk.sv.expression.integer_literal attributes {constant_value = "0", semantic_type = !obelisk.integral<32, true, false, 31 : 0, int>, node_id = 4 : i64} {
              }
            }
            obelisk.sv.expression.assignment attributes {assignment_kind = 0 : i32, semantic_type = !obelisk.integral<32, true, false, 31 : 0, int>, node_id = 8 : i64} {
              obelisk.sv.expression.named_value attributes {referenced_symbol = @root::@top::@body::@j, referenced_path = "top.j", semantic_type = !obelisk.integral<32, true, false, 31 : 0, int>, node_id = 6 : i64} {
              }
              obelisk.sv.expression.integer_literal attributes {constant_value = "1", semantic_type = !obelisk.integral<32, true, false, 31 : 0, int>, node_id = 7 : i64} {
              }
            }
            obelisk.sv.expression.integer_literal attributes {constant_value = "0", semantic_type = !obelisk.integral<32, true, false, 31 : 0, int>, node_id = 9 : i64} {
            }
            obelisk.sv.expression.assignment attributes {assignment_kind = 0 : i32, semantic_type = !obelisk.integral<32, true, false, 31 : 0, int>, node_id = 12 : i64} {
              obelisk.sv.expression.named_value attributes {referenced_symbol = @root::@top::@body::@i, referenced_path = "top.i", semantic_type = !obelisk.integral<32, true, false, 31 : 0, int>, node_id = 10 : i64} {
              }
              obelisk.sv.expression.integer_literal attributes {constant_value = "2", semantic_type = !obelisk.integral<32, true, false, 31 : 0, int>, node_id = 11 : i64} {
              }
            }
            obelisk.sv.expression.assignment attributes {assignment_kind = 0 : i32, semantic_type = !obelisk.integral<32, true, false, 31 : 0, int>, node_id = 15 : i64} {
              obelisk.sv.expression.named_value attributes {referenced_symbol = @root::@top::@body::@j, referenced_path = "top.j", semantic_type = !obelisk.integral<32, true, false, 31 : 0, int>, node_id = 13 : i64} {
              }
              obelisk.sv.expression.integer_literal attributes {constant_value = "3", semantic_type = !obelisk.integral<32, true, false, 31 : 0, int>, node_id = 14 : i64} {
              }
            }
            obelisk.sv.statement.empty attributes {node_id = 16 : i64} {
            }
          }
        }
        obelisk.sv.symbol.procedural_block @call attributes {hierarchical_name = "top", procedure_kind = 0 : i32, time_precision_fs = 1 : i64, time_unit_fs = 1 : i64, node_id = 37 : i64} {
          obelisk.sv.statement.expression_statement attributes {node_id = 36 : i64} {
            obelisk.sv.expression.call attributes {argument_count = 0 : i64, callee_name = "noop", constraint_restrictions = [], defaulted_arguments = array<i64>, has_inline_constraints = false, has_iterator_expression = false, has_output_arguments = false, has_this_class = false, is_signed = false, is_super_class = false, is_system_call = false, referenced_path = "top.noop", referenced_symbol = @root::@top::@body::@noop, semantic_type = !obelisk.void, subroutine_kind = 0 : i32, node_id = 34 : i64} {
            }
          }
        }
        obelisk.sv.symbol.procedural_block @invoke attributes {hierarchical_name = "top", procedure_kind = 0 : i32, time_precision_fs = 1 : i64, time_unit_fs = 1 : i64, node_id = 137 : i64} {
          obelisk.sv.statement.expression_statement attributes {node_id = 136 : i64} {
            obelisk.sv.expression.call attributes {argument_count = 0 : i64, callee_name = "worker", constraint_restrictions = [], defaulted_arguments = array<i64>, has_inline_constraints = false, has_iterator_expression = false, has_output_arguments = false, has_this_class = false, is_signed = false, is_super_class = false, is_system_call = false, referenced_path = "top.worker", referenced_symbol = @root::@top::@body::@worker, semantic_type = !obelisk.void, subroutine_kind = 1 : i32, node_id = 134 : i64} {
            }
          }
        }
        obelisk.sv.symbol.procedural_block @random attributes {hierarchical_name = "top", procedure_kind = 0 : i32, time_precision_fs = 1 : i64, time_unit_fs = 1 : i64, node_id = 200 : i64} {
          obelisk.sv.statement.rand_case attributes {item_count = 1 : i64, node_id = 201 : i64} {
            obelisk.sv.expression.integer_literal attributes {constant_value = "1", semantic_type = !obelisk.integral<32, true, false, 31 : 0, int>, node_id = 202 : i64} {}
            obelisk.sv.statement.empty attributes {node_id = 203 : i64} {}
          }
        }

      }
    }
  }
}

// CHECK: simulation.code_unit.decl [[WORKER:[0-9]+]] in [[SCOPE:[0-9]+]] task hierarchy "top.worker"
// CHECK: simulation.code_unit.decl [[SINGLE:[0-9]+]] in [[SCOPE]] task hierarchy "top.single"
// CHECK: simulation.code_unit.decl [[ZERO:[0-9]+]] in [[SCOPE]] task hierarchy "top.zero"
// CHECK: simulation.code_unit.decl [[FUNCTION:[0-9]+]] in [[SCOPE]] function hierarchy "top.noop"
// CHECK: simulation.code_unit.decl [[LOOP:[0-9]+]] in [[SCOPE]] initial
// CHECK: simulation.code_unit.decl [[CALL:[0-9]+]] in [[SCOPE]] initial
// CHECK: simulation.statement.decl [[BODY:[0-9]+]] in [[WORKER]] scope [[SCOPE]] type 4
// CHECK: simulation.vpi_statement_relation.decl code_unit [[WORKER]] type 59 selector 104 ordinal 0 modes 1 to [[BODY]]
// CHECK: simulation.statement.decl [[FORK:[0-9]+]] in [[WORKER]] scope [[SCOPE]] type 18 parent [[BODY]]
// CHECK: simulation.statement.decl [[DELAY:[0-9]+]] in [[WORKER]] scope [[SCOPE]] type 11 parent [[FORK]]
// CHECK: simulation.statement.decl [[ASSIGN:[0-9]+]] in [[WORKER]] scope [[SCOPE]] type 2 parent [[BODY]]
// CHECK: simulation.vpi_statement_relation.decl statement [[BODY]] type 4 selector 104 ordinal 2 modes 2 to [[ASSIGN]]
// CHECK: simulation.statement.decl [[DEASSIGN:[0-9]+]] in [[WORKER]] scope [[SCOPE]] type 9 parent [[BODY]]
// CHECK: simulation.vpi_statement_relation.decl statement [[BODY]] type 4 selector 104 ordinal 3 modes 2 to [[DEASSIGN]]
// CHECK: simulation.statement.decl [[ONLY:[0-9]+]] in [[SINGLE]] scope [[SCOPE]] type 38
// CHECK: simulation.vpi_statement_relation.decl code_unit [[SINGLE]] type 59 selector 104 ordinal 0 modes 1 to [[ONLY]]
// CHECK-NOT: simulation.vpi_statement_relation.decl code_unit [[ZERO]]
// CHECK: simulation.statement.decl {{[0-9]+}} in [[FUNCTION]] scope [[SCOPE]] type 38
// CHECK: simulation.statement.decl [[FOR:[0-9]+]] in [[LOOP]] scope [[SCOPE]] type 15
// CHECK: simulation.vpi_thread_origin_binding [[LOOP]] origin [[FOR]]
// CHECK: simulation.vpi_statement_relation.decl statement [[FOR]] type 15 selector 75 ordinal 0 modes 3
// CHECK: simulation.vpi_statement_relation.decl statement [[FOR]] type 15 selector 75 ordinal 1 modes 2
// CHECK: simulation.vpi_statement_relation.decl statement [[FOR]] type 15 selector 74 ordinal 0 modes 3
// CHECK: simulation.vpi_statement_relation.decl statement [[FOR]] type 15 selector 74 ordinal 1 modes 2
// CHECK: simulation.statement.decl [[FUNCALL:[0-9]+]] in [[CALL]] scope [[SCOPE]] type 19
// CHECK: simulation.vpi_thread_origin_binding [[CALL]] origin [[FUNCALL]]
// CHECK: simulation.code_unit.decl [[BRANCH:[0-9]+]] in [[SCOPE]] fork hierarchy "top.worker.$fork.{{.*}}" debug "fork branch" {internal}
// CHECK-NEXT: simulation.vpi_thread_origin_binding [[BRANCH]] origin [[DELAY]]
