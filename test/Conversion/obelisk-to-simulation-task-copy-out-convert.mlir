// RUN: obelisk-opt %s '--lower-obelisk-to-sim=opt-level=0' | FileCheck %s
// RUN: obelisk-opt %s '--lower-obelisk-to-sim=opt-level=0' \
// RUN:   '--encode-obelisk-sim-to-bytecode=vpi=off' -o /dev/null
// RUN: obelisk-opt %s '--lower-obelisk-to-sim=opt-level=0' \
// RUN:   --convert-obelisk-sim-processes-to-llvm-coroutines -o /dev/null

// IEEE 1800-2017 13.5: an inout formal is copied in on the call and assigned
// back to the actual on return. The formal here is an `integer` and the actual
// is a one-bit `reg`, so the formal cannot share the actual's storage: the
// call allocates storage of the formal's own type and converts in both
// directions.

module attributes {
  llvm.data_layout = "e-p:64:64-i64:64-i32:32-i16:16-i8:8",
  llvm.target_triple = "x86_64-unknown-linux-gnu"
} {
  obelisk.sv.symbol.definition attributes {definition_kind = 0 : i32, hierarchical_name = "task_copy_out_convert", name = "task_copy_out_convert", node_id = 0 : i64, sym_name = "s0.task_copy_out_convert"} {
  }
  obelisk.sv.symbol.root attributes {hierarchical_name = "\\$root ", name = "$root", node_id = 1 : i64, sym_name = "s1.$root"} {
    obelisk.sv.symbol.compilation_unit attributes {hierarchical_name = "$unit", node_id = 2 : i64, sym_name = "s2"} {
    }
    obelisk.sv.symbol.instance attributes {hierarchical_name = "task_copy_out_convert", is_uninstantiated = false, name = "task_copy_out_convert", node_id = 3 : i64, referenced_path = "task_copy_out_convert", referenced_symbol = @s0.task_copy_out_convert, sym_name = "s3.task_copy_out_convert"} {
      obelisk.sv.symbol.instance_body attributes {hierarchical_name = "task_copy_out_convert", name = "task_copy_out_convert", node_id = 4 : i64, sym_name = "s4.task_copy_out_convert", time_precision_fs = 1000000 : i64, time_unit_fs = 1000000 : i64} {
        obelisk.sv.symbol.variable attributes {hierarchical_name = "task_copy_out_convert.flag", lifetime = 1 : i32, name = "flag", node_id = 5 : i64, semantic_type = !obelisk.integral<1, false, true, 0 : 0, reg>, sym_name = "s5.flag"} {
        }
        obelisk.sv.symbol.subroutine attributes {default_lifetime = 1 : i32, hierarchical_name = "task_copy_out_convert.t", name = "t", node_id = 6 : i64, semantic_type = !obelisk.subroutine<(!obelisk.integral<32, true, true, 31 : 0, integer>) -> (), true>, subroutine_kind = 1 : i32, sym_name = "s6.t", time_precision_fs = 1000000 : i64, time_unit_fs = 1000000 : i64} {
          obelisk.sv.statement.expression_statement attributes {node_id = 7 : i64} {
            obelisk.sv.expression.assignment attributes {assignment_kind = 0 : i32, is_signed = true, node_id = 8 : i64, semantic_type = !obelisk.integral<32, true, true, 31 : 0, integer>} {
              obelisk.sv.expression.named_value attributes {is_signed = true, node_id = 9 : i64, referenced_path = "task_copy_out_convert.t.v", referenced_symbol = @s1.$root::@s3.task_copy_out_convert::@s4.task_copy_out_convert::@s6.t::@s7.v, semantic_type = !obelisk.integral<32, true, true, 31 : 0, integer>} {
              }
              obelisk.sv.expression.binary_op attributes {is_signed = true, node_id = 10 : i64, operator_kind = 0 : i32, semantic_type = !obelisk.integral<32, true, true, 31 : 0, integer>} {
                obelisk.sv.expression.named_value attributes {is_signed = true, node_id = 11 : i64, referenced_path = "task_copy_out_convert.t.v", referenced_symbol = @s1.$root::@s3.task_copy_out_convert::@s4.task_copy_out_convert::@s6.t::@s7.v, semantic_type = !obelisk.integral<32, true, true, 31 : 0, integer>} {
                }
                obelisk.sv.expression.conversion attributes {folded_constant = "1", is_signed = true, node_id = 12 : i64, semantic_type = !obelisk.integral<32, true, true, 31 : 0, integer>} {
                  obelisk.sv.expression.integer_literal attributes {constant_value = "1", is_declared_unsized = true, is_signed = true, node_id = 13 : i64, semantic_type = !obelisk.integral<32, true, false, 31 : 0, int>} {
                  }
                }
              }
            }
          }
          obelisk.sv.symbol.formal_argument attributes {direction = 2 : i32, hierarchical_name = "task_copy_out_convert.t.v", lifetime = 1 : i32, name = "v", node_id = 14 : i64, semantic_type = !obelisk.integral<32, true, true, 31 : 0, integer>, sym_name = "s7.v"} {
          }
        }
        obelisk.sv.symbol.procedural_block attributes {hierarchical_name = "task_copy_out_convert", node_id = 15 : i64, procedure_kind = 0 : i32, sym_name = "s8", time_precision_fs = 1000000 : i64, time_unit_fs = 1000000 : i64} {
          obelisk.sv.statement.block attributes {node_id = 16 : i64} {
            obelisk.sv.statement.list attributes {node_id = 17 : i64} {
              obelisk.sv.statement.expression_statement attributes {node_id = 18 : i64} {
                obelisk.sv.expression.assignment attributes {assignment_kind = 0 : i32, is_signed = false, node_id = 19 : i64, semantic_type = !obelisk.integral<1, false, true, 0 : 0, reg>} {
                  obelisk.sv.expression.named_value attributes {is_signed = false, node_id = 20 : i64, referenced_path = "task_copy_out_convert.flag", referenced_symbol = @s1.$root::@s3.task_copy_out_convert::@s4.task_copy_out_convert::@s5.flag, semantic_type = !obelisk.integral<1, false, true, 0 : 0, reg>} {
                  }
                  obelisk.sv.expression.conversion attributes {folded_constant = "1'b0", is_signed = false, node_id = 21 : i64, semantic_type = !obelisk.integral<1, false, true, 0 : 0, reg>} {
                    obelisk.sv.expression.conversion attributes {folded_constant = "0", is_signed = true, node_id = 22 : i64, semantic_type = !obelisk.ranged_packed_array<31 : 0 x !obelisk.integral<1, true, true, 0 : 0, logic>>} {
                      obelisk.sv.expression.integer_literal attributes {constant_value = "0", is_declared_unsized = true, is_signed = true, node_id = 23 : i64, semantic_type = !obelisk.integral<32, true, false, 31 : 0, int>} {
                      }
                    }
                  }
                }
              }
              obelisk.sv.statement.expression_statement attributes {node_id = 24 : i64} {
                obelisk.sv.expression.call attributes {argument_count = 1 : i64, callee_name = "t", constraint_restrictions = [], defaulted_arguments = array<i64: 0>, has_inline_constraints = false, has_iterator_expression = false, has_output_arguments = true, has_this_class = false, is_signed = false, is_super_class = false, is_system_call = false, node_id = 25 : i64, referenced_path = "task_copy_out_convert.t", referenced_symbol = @s1.$root::@s3.task_copy_out_convert::@s4.task_copy_out_convert::@s6.t, semantic_type = !obelisk.void, subroutine_kind = 1 : i32} {
                  obelisk.sv.expression.assignment attributes {assignment_kind = 0 : i32, is_signed = false, node_id = 26 : i64, semantic_type = !obelisk.integral<1, false, true, 0 : 0, reg>} {
                    obelisk.sv.expression.named_value attributes {is_signed = false, node_id = 27 : i64, referenced_path = "task_copy_out_convert.flag", referenced_symbol = @s1.$root::@s3.task_copy_out_convert::@s4.task_copy_out_convert::@s5.flag, semantic_type = !obelisk.integral<1, false, true, 0 : 0, reg>} {
                    }
                    obelisk.sv.expression.conversion attributes {is_signed = false, node_id = 28 : i64, semantic_type = !obelisk.integral<1, false, true, 0 : 0, reg>} {
                      obelisk.sv.expression.empty_argument attributes {is_signed = true, node_id = 29 : i64, semantic_type = !obelisk.integral<32, true, true, 31 : 0, integer>} {
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
}


// CHECK: obelisk_sim.func private @unit_1(
// CHECK-SAME: %[[FLAG:[^:]*]]: !obelisk_sim.ref<!obelisk_sim.logic<1>>
// CHECK: %[[IN:.*]] = obelisk_sim.logic.resize
// CHECK-SAME: !obelisk_sim.logic<1> -> !obelisk_sim.logic<32>
// CHECK: %[[FORMAL:.*]] = obelisk_sim.ref.alloc %[[IN]]
// CHECK-SAME: -> !obelisk_sim.ref<!obelisk_sim.logic<32>>
// CHECK: obelisk_sim.task.call @unit_0(%{{.*}}, %[[IN]], %[[FORMAL]]
// CHECK: ^bb1(%[[RETURNED:.*]]: !obelisk_sim.ref<!obelisk_sim.logic<32>>):
// CHECK: %[[OUT:.*]] = obelisk_sim.ref.load %[[RETURNED]]
// CHECK: %[[NARROWED:.*]] = obelisk_sim.logic.resize %[[OUT]]
// CHECK-SAME: !obelisk_sim.logic<32> -> !obelisk_sim.logic<1>
// CHECK: obelisk_sim.ref.store %[[NARROWED]] to %[[FLAG]]
