// RUN: obelisk-opt %s '--lower-obelisk-to-sim=opt-level=0' | FileCheck %s

//   int a[2]; int b[2]; int c[2]; int index;
//   task automatic produce(output int value); index = 1; value = 7; endtask
//   function automatic void fill(output int value); index = 1; value = 8;
//   endfunction
//   task automatic bump(inout int value); index = 1; value += 1; endtask
//   initial begin
//     produce(a[index]);
//     fill(b[index]);
//     bump(c[index]);
//   end
//
// IEEE 1800-2023 4.9.7: copy-out on return behaves as a blocking assignment,
// so each actual is addressed after the subroutine returns and sees the index
// it changed. IEEE 1800-2023 13.5.1: a task's changes to a formal are not
// visible outside it before it returns, so the caller gives each formal its
// own cell. An inout actual is still read when the call begins.

module attributes {coverage.language_version = 2023 : i32} {
  obelisk.sv.symbol.definition @s0.copy_out attributes {definition_kind = 0 : i32, hierarchical_name = "copy_out", name = "copy_out", node_id = 0 : i64} {
  }
  obelisk.sv.symbol.root @s1.$root attributes {hierarchical_name = "\\$root ", name = "$root", node_id = 1 : i64} {
    obelisk.sv.symbol.compilation_unit @s2 attributes {hierarchical_name = "$unit", node_id = 2 : i64, simulation.vpi_definition_name = "$unit"} {
    }
    obelisk.sv.symbol.instance @s3.copy_out attributes {hierarchical_name = "copy_out", is_uninstantiated = false, name = "copy_out", node_id = 3 : i64, referenced_path = "copy_out", referenced_symbol = @s0.copy_out} {
      obelisk.sv.symbol.instance_body @s4.copy_out attributes {hierarchical_name = "copy_out", name = "copy_out", node_id = 4 : i64, simulation.vpi_automatic = false, simulation.vpi_cell_instance = false, simulation.vpi_definition_name = "copy_out", simulation.vpi_top = true, time_precision_fs = 1000000 : i64, time_unit_fs = 1000000 : i64, vpi_scope_kind = 32 : i32} {
        obelisk.sv.symbol.variable @s5.a attributes {hierarchical_name = "copy_out.a", lifetime = 1 : i32, name = "a", node_id = 5 : i64, semantic_type = !obelisk.ranged_unpacked_array<0 : 1 x !obelisk.integral<32, true, false, 31 : 0, int>>} {
        }
        obelisk.sv.symbol.variable @s6.b attributes {hierarchical_name = "copy_out.b", lifetime = 1 : i32, name = "b", node_id = 6 : i64, semantic_type = !obelisk.ranged_unpacked_array<0 : 1 x !obelisk.integral<32, true, false, 31 : 0, int>>} {
        }
        obelisk.sv.symbol.variable @s7.c attributes {hierarchical_name = "copy_out.c", lifetime = 1 : i32, name = "c", node_id = 7 : i64, semantic_type = !obelisk.ranged_unpacked_array<0 : 1 x !obelisk.integral<32, true, false, 31 : 0, int>>} {
        }
        obelisk.sv.symbol.variable @s8.index attributes {hierarchical_name = "copy_out.index", lifetime = 1 : i32, name = "index", node_id = 8 : i64, semantic_type = !obelisk.integral<32, true, false, 31 : 0, int>} {
        }
        obelisk.sv.symbol.subroutine @s9.produce attributes {hierarchical_name = "copy_out.produce", name = "produce", node_id = 9 : i64, semantic_type = !obelisk.subroutine<(!obelisk.integral<32, true, false, 31 : 0, int>) -> (), true>, subroutine_kind = 1 : i32, time_precision_fs = 1000000 : i64, time_unit_fs = 1000000 : i64} {
          obelisk.sv.statement.list attributes {node_id = 10 : i64} {
            obelisk.sv.statement.expression_statement attributes {node_id = 11 : i64} {
              obelisk.sv.expression.assignment attributes {assignment_kind = 0 : i32, is_signed = true, node_id = 12 : i64, semantic_type = !obelisk.integral<32, true, false, 31 : 0, int>} {
                obelisk.sv.expression.named_value attributes {is_signed = true, node_id = 13 : i64, referenced_path = "copy_out.index", referenced_symbol = @s1.$root::@s3.copy_out::@s4.copy_out::@s8.index, semantic_type = !obelisk.integral<32, true, false, 31 : 0, int>} {
                }
                obelisk.sv.expression.integer_literal attributes {constant_value = "1", is_declared_unsized = true, is_signed = true, node_id = 14 : i64, semantic_type = !obelisk.integral<32, true, false, 31 : 0, int>} {
                }
              }
            }
            obelisk.sv.statement.expression_statement attributes {node_id = 15 : i64} {
              obelisk.sv.expression.assignment attributes {assignment_kind = 0 : i32, is_signed = true, node_id = 16 : i64, semantic_type = !obelisk.integral<32, true, false, 31 : 0, int>} {
                obelisk.sv.expression.named_value attributes {is_signed = true, node_id = 17 : i64, referenced_path = "copy_out.produce.value", referenced_symbol = @s1.$root::@s3.copy_out::@s4.copy_out::@s9.produce::@s10.value, semantic_type = !obelisk.integral<32, true, false, 31 : 0, int>} {
                }
                obelisk.sv.expression.integer_literal attributes {constant_value = "7", is_declared_unsized = true, is_signed = true, node_id = 18 : i64, semantic_type = !obelisk.integral<32, true, false, 31 : 0, int>} {
                }
              }
            }
          }
          obelisk.sv.symbol.formal_argument @s10.value attributes {direction = 1 : i32, hierarchical_name = "copy_out.produce.value", name = "value", node_id = 19 : i64, semantic_type = !obelisk.integral<32, true, false, 31 : 0, int>} {
          }
        }
        obelisk.sv.symbol.subroutine @s11.fill attributes {hierarchical_name = "copy_out.fill", name = "fill", node_id = 20 : i64, semantic_type = !obelisk.subroutine<(!obelisk.integral<32, true, false, 31 : 0, int>) -> !obelisk.void, false>, subroutine_kind = 0 : i32, time_precision_fs = 1000000 : i64, time_unit_fs = 1000000 : i64} {
          obelisk.sv.statement.list attributes {node_id = 21 : i64} {
            obelisk.sv.statement.expression_statement attributes {node_id = 22 : i64} {
              obelisk.sv.expression.assignment attributes {assignment_kind = 0 : i32, is_signed = true, node_id = 23 : i64, semantic_type = !obelisk.integral<32, true, false, 31 : 0, int>} {
                obelisk.sv.expression.named_value attributes {is_signed = true, node_id = 24 : i64, referenced_path = "copy_out.index", referenced_symbol = @s1.$root::@s3.copy_out::@s4.copy_out::@s8.index, semantic_type = !obelisk.integral<32, true, false, 31 : 0, int>} {
                }
                obelisk.sv.expression.integer_literal attributes {constant_value = "1", is_declared_unsized = true, is_signed = true, node_id = 25 : i64, semantic_type = !obelisk.integral<32, true, false, 31 : 0, int>} {
                }
              }
            }
            obelisk.sv.statement.expression_statement attributes {node_id = 26 : i64} {
              obelisk.sv.expression.assignment attributes {assignment_kind = 0 : i32, is_signed = true, node_id = 27 : i64, semantic_type = !obelisk.integral<32, true, false, 31 : 0, int>} {
                obelisk.sv.expression.named_value attributes {is_signed = true, node_id = 28 : i64, referenced_path = "copy_out.fill.value", referenced_symbol = @s1.$root::@s3.copy_out::@s4.copy_out::@s11.fill::@s12.value, semantic_type = !obelisk.integral<32, true, false, 31 : 0, int>} {
                }
                obelisk.sv.expression.integer_literal attributes {constant_value = "8", is_declared_unsized = true, is_signed = true, node_id = 29 : i64, semantic_type = !obelisk.integral<32, true, false, 31 : 0, int>} {
                }
              }
            }
          }
          obelisk.sv.symbol.formal_argument @s12.value attributes {direction = 1 : i32, hierarchical_name = "copy_out.fill.value", name = "value", node_id = 30 : i64, semantic_type = !obelisk.integral<32, true, false, 31 : 0, int>} {
          }
        }
        obelisk.sv.symbol.subroutine @s13.bump attributes {hierarchical_name = "copy_out.bump", name = "bump", node_id = 31 : i64, semantic_type = !obelisk.subroutine<(!obelisk.integral<32, true, false, 31 : 0, int>) -> (), true>, subroutine_kind = 1 : i32, time_precision_fs = 1000000 : i64, time_unit_fs = 1000000 : i64} {
          obelisk.sv.statement.list attributes {node_id = 32 : i64} {
            obelisk.sv.statement.expression_statement attributes {node_id = 33 : i64} {
              obelisk.sv.expression.assignment attributes {assignment_kind = 0 : i32, is_signed = true, node_id = 34 : i64, semantic_type = !obelisk.integral<32, true, false, 31 : 0, int>} {
                obelisk.sv.expression.named_value attributes {is_signed = true, node_id = 35 : i64, referenced_path = "copy_out.index", referenced_symbol = @s1.$root::@s3.copy_out::@s4.copy_out::@s8.index, semantic_type = !obelisk.integral<32, true, false, 31 : 0, int>} {
                }
                obelisk.sv.expression.integer_literal attributes {constant_value = "1", is_declared_unsized = true, is_signed = true, node_id = 36 : i64, semantic_type = !obelisk.integral<32, true, false, 31 : 0, int>} {
                }
              }
            }
            obelisk.sv.statement.expression_statement attributes {node_id = 37 : i64} {
              obelisk.sv.expression.assignment attributes {assignment_kind = 0 : i32, is_signed = true, node_id = 38 : i64, operator_kind = 0 : i32, semantic_type = !obelisk.integral<32, true, false, 31 : 0, int>} {
                obelisk.sv.expression.named_value attributes {is_signed = true, node_id = 39 : i64, referenced_path = "copy_out.bump.value", referenced_symbol = @s1.$root::@s3.copy_out::@s4.copy_out::@s13.bump::@s14.value, semantic_type = !obelisk.integral<32, true, false, 31 : 0, int>} {
                }
                obelisk.sv.expression.binary_op attributes {is_signed = true, node_id = 40 : i64, operator_kind = 0 : i32, semantic_type = !obelisk.integral<32, true, false, 31 : 0, int>} {
                  obelisk.sv.expression.l_value_reference attributes {is_signed = true, node_id = 41 : i64, semantic_type = !obelisk.integral<32, true, false, 31 : 0, int>} {
                  }
                  obelisk.sv.expression.integer_literal attributes {constant_value = "1", is_declared_unsized = true, is_signed = true, node_id = 42 : i64, semantic_type = !obelisk.integral<32, true, false, 31 : 0, int>} {
                  }
                }
              }
            }
          }
          obelisk.sv.symbol.formal_argument @s14.value attributes {direction = 2 : i32, hierarchical_name = "copy_out.bump.value", name = "value", node_id = 43 : i64, semantic_type = !obelisk.integral<32, true, false, 31 : 0, int>} {
          }
        }
        obelisk.sv.symbol.procedural_block @s15 attributes {hierarchical_name = "copy_out", node_id = 44 : i64, procedure_kind = 0 : i32, time_precision_fs = 1000000 : i64, time_unit_fs = 1000000 : i64} {
          obelisk.sv.statement.block attributes {node_id = 45 : i64} {
            obelisk.sv.statement.list attributes {node_id = 46 : i64} {
              obelisk.sv.statement.expression_statement attributes {node_id = 47 : i64} {
                obelisk.sv.expression.call attributes {argument_count = 1 : i64, callee_name = "produce", constraint_restrictions = [], defaulted_arguments = array<i64: 0>, has_inline_constraints = false, has_iterator_expression = false, has_output_arguments = true, has_this_class = false, is_signed = false, is_super_class = false, is_system_call = false, node_id = 48 : i64, referenced_path = "copy_out.produce", referenced_symbol = @s1.$root::@s3.copy_out::@s4.copy_out::@s9.produce, semantic_type = !obelisk.void, subroutine_kind = 1 : i32} {
                  obelisk.sv.expression.assignment attributes {assignment_kind = 0 : i32, is_signed = true, node_id = 49 : i64, semantic_type = !obelisk.integral<32, true, false, 31 : 0, int>} {
                    obelisk.sv.expression.element_select attributes {is_signed = true, node_id = 50 : i64, semantic_type = !obelisk.integral<32, true, false, 31 : 0, int>} {
                      obelisk.sv.expression.named_value attributes {is_signed = false, node_id = 51 : i64, referenced_path = "copy_out.a", referenced_symbol = @s1.$root::@s3.copy_out::@s4.copy_out::@s5.a, semantic_type = !obelisk.ranged_unpacked_array<0 : 1 x !obelisk.integral<32, true, false, 31 : 0, int>>} {
                      }
                      obelisk.sv.expression.named_value attributes {is_signed = true, node_id = 52 : i64, referenced_path = "copy_out.index", referenced_symbol = @s1.$root::@s3.copy_out::@s4.copy_out::@s8.index, semantic_type = !obelisk.integral<32, true, false, 31 : 0, int>} {
                      }
                    }
                    obelisk.sv.expression.empty_argument attributes {is_signed = true, node_id = 53 : i64, semantic_type = !obelisk.integral<32, true, false, 31 : 0, int>} {
                    }
                  }
                }
              }
              obelisk.sv.statement.expression_statement attributes {node_id = 54 : i64} {
                obelisk.sv.expression.call attributes {argument_count = 1 : i64, callee_name = "fill", constraint_restrictions = [], defaulted_arguments = array<i64: 0>, has_inline_constraints = false, has_iterator_expression = false, has_output_arguments = true, has_this_class = false, is_signed = false, is_super_class = false, is_system_call = false, node_id = 55 : i64, referenced_path = "copy_out.fill", referenced_symbol = @s1.$root::@s3.copy_out::@s4.copy_out::@s11.fill, semantic_type = !obelisk.void, subroutine_kind = 0 : i32} {
                  obelisk.sv.expression.assignment attributes {assignment_kind = 0 : i32, is_signed = true, node_id = 56 : i64, semantic_type = !obelisk.integral<32, true, false, 31 : 0, int>} {
                    obelisk.sv.expression.element_select attributes {is_signed = true, node_id = 57 : i64, semantic_type = !obelisk.integral<32, true, false, 31 : 0, int>} {
                      obelisk.sv.expression.named_value attributes {is_signed = false, node_id = 58 : i64, referenced_path = "copy_out.b", referenced_symbol = @s1.$root::@s3.copy_out::@s4.copy_out::@s6.b, semantic_type = !obelisk.ranged_unpacked_array<0 : 1 x !obelisk.integral<32, true, false, 31 : 0, int>>} {
                      }
                      obelisk.sv.expression.named_value attributes {is_signed = true, node_id = 59 : i64, referenced_path = "copy_out.index", referenced_symbol = @s1.$root::@s3.copy_out::@s4.copy_out::@s8.index, semantic_type = !obelisk.integral<32, true, false, 31 : 0, int>} {
                      }
                    }
                    obelisk.sv.expression.empty_argument attributes {is_signed = true, node_id = 60 : i64, semantic_type = !obelisk.integral<32, true, false, 31 : 0, int>} {
                    }
                  }
                }
              }
              obelisk.sv.statement.expression_statement attributes {node_id = 61 : i64} {
                obelisk.sv.expression.call attributes {argument_count = 1 : i64, callee_name = "bump", constraint_restrictions = [], defaulted_arguments = array<i64: 0>, has_inline_constraints = false, has_iterator_expression = false, has_output_arguments = true, has_this_class = false, is_signed = false, is_super_class = false, is_system_call = false, node_id = 62 : i64, referenced_path = "copy_out.bump", referenced_symbol = @s1.$root::@s3.copy_out::@s4.copy_out::@s13.bump, semantic_type = !obelisk.void, subroutine_kind = 1 : i32} {
                  obelisk.sv.expression.assignment attributes {assignment_kind = 0 : i32, is_signed = true, node_id = 63 : i64, semantic_type = !obelisk.integral<32, true, false, 31 : 0, int>} {
                    obelisk.sv.expression.element_select attributes {is_signed = true, node_id = 64 : i64, semantic_type = !obelisk.integral<32, true, false, 31 : 0, int>} {
                      obelisk.sv.expression.named_value attributes {is_signed = false, node_id = 65 : i64, referenced_path = "copy_out.c", referenced_symbol = @s1.$root::@s3.copy_out::@s4.copy_out::@s7.c, semantic_type = !obelisk.ranged_unpacked_array<0 : 1 x !obelisk.integral<32, true, false, 31 : 0, int>>} {
                      }
                      obelisk.sv.expression.named_value attributes {is_signed = true, node_id = 66 : i64, referenced_path = "copy_out.index", referenced_symbol = @s1.$root::@s3.copy_out::@s4.copy_out::@s8.index, semantic_type = !obelisk.integral<32, true, false, 31 : 0, int>} {
                      }
                    }
                    obelisk.sv.expression.empty_argument attributes {is_signed = true, node_id = 67 : i64, semantic_type = !obelisk.integral<32, true, false, 31 : 0, int>} {
                    }
                  }
                }
              }
            }
          }
        }
      }
    }
  }
}


// CHECK-LABEL: simulation.func private @unit_3(
// CHECK-SAME: %[[A:arg1]]: !simulation.ref<!simulation.unpacked_array
// CHECK-SAME: %[[B:arg2]]: !simulation.ref<!simulation.unpacked_array
// CHECK-SAME: %[[C:arg3]]: !simulation.ref<!simulation.unpacked_array
// CHECK-SAME: %[[INDEX:arg4]]: !simulation.ref<i32>
// CHECK: %[[OUT:.*]] = simulation.ref.alloc
// CHECK: simulation.task.call @unit_0(%arg0, %[[OUT]], {{.*}} to ^[[PRODUCED:bb[0-9]+]]
// CHECK: ^[[PRODUCED]](%[[OUT_CELL:.*]]: !simulation.ref<i32>):
// CHECK-NEXT: %[[PRODUCED_VALUE:.*]] = simulation.ref.load %[[OUT_CELL]]
// CHECK-NEXT: %[[INDEX_A:.*]] = simulation.ref.load %[[INDEX]]
// CHECK: %[[TARGET_A:.*]] = simulation.ref.array_element %[[A]]
// CHECK-NEXT: simulation.ref.store %[[PRODUCED_VALUE]] to %[[TARGET_A]]
// CHECK: simulation.call @unit_1
// CHECK: simulation.ref.load %[[INDEX]]
// CHECK: simulation.ref.array_element %[[B]]
// CHECK: %[[INDEX_IN:.*]] = simulation.ref.load %[[INDEX]]
// CHECK: %[[SOURCE_C:.*]] = simulation.ref.array_element %[[C]]
// CHECK-NEXT: %[[COPIED_IN:.*]] = simulation.ref.load %[[SOURCE_C]]
// CHECK-NEXT: %[[INOUT:.*]] = simulation.ref.alloc %[[COPIED_IN]]
// CHECK-NEXT: simulation.task.call @unit_2(%arg0, %[[COPIED_IN]], %[[INOUT]], {{.*}} to ^[[BUMPED:bb[0-9]+]]
// CHECK: ^[[BUMPED]](%[[INOUT_CELL:.*]]: !simulation.ref<i32>):
// CHECK-NEXT: %[[BUMPED_VALUE:.*]] = simulation.ref.load %[[INOUT_CELL]]
// CHECK-NEXT: simulation.ref.load %[[INDEX]]
// CHECK: %[[TARGET_C:.*]] = simulation.ref.array_element %[[C]]
// CHECK-NEXT: simulation.ref.store %[[BUMPED_VALUE]] to %[[TARGET_C]]
