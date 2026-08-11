// RUN: obelisk-opt %s '--lower-obelisk-to-sim=opt-level=0' | FileCheck %s

// Two-state division has no unknown result to fall back on, so the host
// instruction must never be handed a divisor that traps on it: dividing by
// zero yields zero (IEEE 1800-2017 11.4.4) rather than raising SIGFPE.

// CHECK-LABEL: obelisk_sim.func private @unit_0(
// CHECK-DAG: %[[ZERO:.*]] = arith.constant 0 : i64
// CHECK-DAG: %[[ONE:.*]] = arith.constant 1 : i64
// CHECK: %[[NOW:.*]] = obelisk_sim.time.now
// CHECK: %[[BAD:.*]] = arith.cmpi eq, %[[NOW]], %[[ZERO]]
// CHECK: %[[SAFE:.*]] = arith.select %[[BAD]], %[[ONE]], %[[NOW]]
// CHECK: %[[QUOTIENT:.*]] = arith.divui %[[NOW]], %[[SAFE]]
// CHECK: arith.select %[[BAD]], %[[ZERO]], %[[QUOTIENT]]
// CHECK: %[[NOW2:.*]] = obelisk_sim.time.now
// CHECK: %[[BAD2:.*]] = arith.cmpi eq, %[[NOW2]], %[[ZERO]]
// CHECK: %[[SAFE2:.*]] = arith.select %[[BAD2]], %[[ONE]], %[[NOW2]]
// CHECK: %[[REMAINDER:.*]] = arith.remui %[[NOW2]], %[[SAFE2]]
// CHECK: arith.select %[[BAD2]], %[[ZERO]], %[[REMAINDER]]

module {
  obelisk.sv.symbol.definition attributes {definition_kind = 0 : i32, hierarchical_name = "t", name = "t", node_id = 0 : i64, sym_name = "s0.t"} {
  }
  obelisk.sv.symbol.root attributes {hierarchical_name = "\\$root ", name = "$root", node_id = 1 : i64, sym_name = "s1.$root"} {
    obelisk.sv.symbol.compilation_unit attributes {hierarchical_name = "$unit", node_id = 2 : i64, sym_name = "s2"} {
    }
    obelisk.sv.symbol.instance attributes {hierarchical_name = "t", is_uninstantiated = false, name = "t", node_id = 3 : i64, referenced_path = "t", referenced_symbol = @s0.t, sym_name = "s3.t"} {
      obelisk.sv.symbol.instance_body attributes {hierarchical_name = "t", name = "t", node_id = 4 : i64, sym_name = "s4.t", time_precision_fs = 1000000 : i64, time_unit_fs = 1000000 : i64} {
        obelisk.sv.symbol.variable attributes {hierarchical_name = "t.d", lifetime = 1 : i32, name = "d", node_id = 5 : i64, semantic_type = !obelisk.integral<32, false, false, 31 : 0, int>, sym_name = "s5.d"} {
        }
        obelisk.sv.symbol.variable attributes {hierarchical_name = "t.q", lifetime = 1 : i32, name = "q", node_id = 6 : i64, semantic_type = !obelisk.integral<32, false, false, 31 : 0, int>, sym_name = "s6.q"} {
        }
        obelisk.sv.symbol.procedural_block attributes {hierarchical_name = "t", node_id = 7 : i64, procedure_kind = 0 : i32, sym_name = "s7", time_precision_fs = 1000000 : i64, time_unit_fs = 1000000 : i64} {
          obelisk.sv.statement.block attributes {node_id = 8 : i64} {
            obelisk.sv.statement.list attributes {node_id = 9 : i64} {
              obelisk.sv.statement.expression_statement attributes {node_id = 10 : i64} {
                obelisk.sv.expression.assignment attributes {assignment_kind = 0 : i32, is_signed = false, node_id = 11 : i64, semantic_type = !obelisk.integral<32, false, false, 31 : 0, int>} {
                  obelisk.sv.expression.named_value attributes {is_signed = false, node_id = 12 : i64, referenced_path = "t.d", referenced_symbol = @s1.$root::@s3.t::@s4.t::@s5.d, semantic_type = !obelisk.integral<32, false, false, 31 : 0, int>} {
                  }
                  obelisk.sv.expression.conversion attributes {is_signed = false, node_id = 13 : i64, semantic_type = !obelisk.integral<32, false, false, 31 : 0, int>} {
                    obelisk.sv.expression.binary_op attributes {is_signed = false, node_id = 14 : i64, operator_kind = 3 : i32, semantic_type = !obelisk.time} {
                      obelisk.sv.expression.call attributes {argument_count = 0 : i64, callee_name = "$time", constraint_restrictions = [], defaulted_arguments = array<i64>, has_inline_constraints = false, has_iterator_expression = false, has_output_arguments = false, has_this_class = false, is_signed = false, is_super_class = false, is_system_call = true, node_id = 15 : i64, semantic_type = !obelisk.time, subroutine_kind = 0 : i32, system_library_cell = "work.t", system_scope_path = "t", system_scope_symbol = @s1.$root::@s3.t::@s4.t} {
                      }
                      obelisk.sv.expression.call attributes {argument_count = 0 : i64, callee_name = "$time", constraint_restrictions = [], defaulted_arguments = array<i64>, has_inline_constraints = false, has_iterator_expression = false, has_output_arguments = false, has_this_class = false, is_signed = false, is_super_class = false, is_system_call = true, node_id = 16 : i64, semantic_type = !obelisk.time, subroutine_kind = 0 : i32, system_library_cell = "work.t", system_scope_path = "t", system_scope_symbol = @s1.$root::@s3.t::@s4.t} {
                      }
                    }
                  }
                }
              }
              obelisk.sv.statement.expression_statement attributes {node_id = 17 : i64} {
                obelisk.sv.expression.assignment attributes {assignment_kind = 0 : i32, is_signed = false, node_id = 18 : i64, semantic_type = !obelisk.integral<32, false, false, 31 : 0, int>} {
                  obelisk.sv.expression.named_value attributes {is_signed = false, node_id = 19 : i64, referenced_path = "t.q", referenced_symbol = @s1.$root::@s3.t::@s4.t::@s6.q, semantic_type = !obelisk.integral<32, false, false, 31 : 0, int>} {
                  }
                  obelisk.sv.expression.conversion attributes {is_signed = false, node_id = 20 : i64, semantic_type = !obelisk.integral<32, false, false, 31 : 0, int>} {
                    obelisk.sv.expression.binary_op attributes {is_signed = false, node_id = 21 : i64, operator_kind = 4 : i32, semantic_type = !obelisk.time} {
                      obelisk.sv.expression.call attributes {argument_count = 0 : i64, callee_name = "$time", constraint_restrictions = [], defaulted_arguments = array<i64>, has_inline_constraints = false, has_iterator_expression = false, has_output_arguments = false, has_this_class = false, is_signed = false, is_super_class = false, is_system_call = true, node_id = 22 : i64, semantic_type = !obelisk.time, subroutine_kind = 0 : i32, system_library_cell = "work.t", system_scope_path = "t", system_scope_symbol = @s1.$root::@s3.t::@s4.t} {
                      }
                      obelisk.sv.expression.call attributes {argument_count = 0 : i64, callee_name = "$time", constraint_restrictions = [], defaulted_arguments = array<i64>, has_inline_constraints = false, has_iterator_expression = false, has_output_arguments = false, has_this_class = false, is_signed = false, is_super_class = false, is_system_call = true, node_id = 23 : i64, semantic_type = !obelisk.time, subroutine_kind = 0 : i32, system_library_cell = "work.t", system_scope_path = "t", system_scope_symbol = @s1.$root::@s3.t::@s4.t} {
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

