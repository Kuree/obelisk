// RUN: obelisk-opt %s '--lower-obelisk-to-sim=opt-level=0' | FileCheck %s
// RUN: obelisk-opt %s '--lower-obelisk-to-sim=opt-level=3' --emit-bytecode -o /dev/null

// IEEE 1800-2017 20.12: the control type, assertion-type mask, and
// directive-type mask are integer expressions. They are read at execution
// time, checked, and tested against each compiler-resolved target without a
// runtime hierarchy scan.
module {
  obelisk.sv.symbol.definition @s0.top attributes {definition_kind = 0 : i32, hierarchical_name = "top", name = "top", node_id = 0 : i64} {
  }
  obelisk.sv.symbol.root @s1.$root attributes {hierarchical_name = "\\$root ", name = "$root", node_id = 1 : i64} {
    obelisk.sv.symbol.compilation_unit @s2 attributes {hierarchical_name = "$unit", node_id = 2 : i64} {
    }
    obelisk.sv.symbol.instance @s3.top attributes {hierarchical_name = "top", is_uninstantiated = false, name = "top", node_id = 3 : i64, referenced_path = "top", referenced_symbol = @s0.top} {
      obelisk.sv.symbol.instance_body @s4.top attributes {hierarchical_name = "top", name = "top", node_id = 4 : i64, time_precision_fs = 1000000 : i64, time_unit_fs = 1000000 : i64} {
        obelisk.sv.symbol.variable @s5.mask attributes {hierarchical_name = "top.mask", lifetime = 1 : i32, name = "mask", node_id = 5 : i64, semantic_type = !obelisk.integral<32, true, false, 31 : 0, int>} {
        }
        obelisk.sv.symbol.statement_block @s6.target attributes {block_kind = 0 : i32, hierarchical_name = "top.target", name = "target", node_id = 6 : i64} {
        }
        obelisk.sv.symbol.procedural_block @s7 attributes {hierarchical_name = "top", node_id = 7 : i64, procedure_kind = 0 : i32, time_precision_fs = 1000000 : i64, time_unit_fs = 1000000 : i64} {
          obelisk.sv.statement.block attributes {node_id = 8 : i64} {
            obelisk.sv.statement.list attributes {node_id = 9 : i64} {
              obelisk.sv.statement.block attributes {block_path = "top.target", block_symbol = @s1.$root::@s3.top::@s4.top::@s6.target, node_id = 10 : i64} {
                obelisk.sv.statement.immediate_assertion attributes {assertion_kind = 0 : i32, has_fail_action = false, has_pass_action = true, is_deferred = false, is_final = false, node_id = 11 : i64} {
                  obelisk.sv.expression.integer_literal attributes {constant_value = "1'b1", is_signed = false, node_id = 12 : i64, semantic_type = !obelisk.ranged_packed_array<0 : 0 x !obelisk.integral<1, false, false, 0 : 0, bit>>} {
                  }
                  obelisk.sv.statement.empty attributes {node_id = 13 : i64} {
                  }
                }
              }
              obelisk.sv.statement.expression_statement attributes {node_id = 14 : i64} {
                obelisk.sv.expression.call attributes {argument_count = 4 : i64, callee_name = "$assertcontrol", constraint_restrictions = [], defaulted_arguments = array<i64: 0, 0, 0, 0>, has_inline_constraints = false, has_iterator_expression = false, has_output_arguments = false, has_this_class = false, is_signed = false, is_super_class = false, is_system_call = true, node_id = 15 : i64, semantic_type = !obelisk.void, subroutine_kind = 1 : i32, system_library_cell = "work.top", system_scope_path = "top", system_scope_symbol = @s1.$root::@s3.top::@s4.top} {
                  obelisk.sv.expression.integer_literal attributes {constant_value = "4", is_declared_unsized = true, is_signed = true, node_id = 16 : i64, semantic_type = !obelisk.integral<32, true, false, 31 : 0, int>} {
                  }
                  obelisk.sv.expression.named_value attributes {is_signed = true, node_id = 17 : i64, referenced_path = "top.mask", referenced_symbol = @s1.$root::@s3.top::@s4.top::@s5.mask, semantic_type = !obelisk.integral<32, true, false, 31 : 0, int>} {
                  }
                  obelisk.sv.expression.named_value attributes {is_signed = true, node_id = 18 : i64, referenced_path = "top.mask", referenced_symbol = @s1.$root::@s3.top::@s4.top::@s5.mask, semantic_type = !obelisk.integral<32, true, false, 31 : 0, int>} {
                  }
                  obelisk.sv.expression.integer_literal attributes {constant_value = "0", is_declared_unsized = true, is_signed = true, node_id = 19 : i64, semantic_type = !obelisk.integral<32, true, false, 31 : 0, int>} {
                  }
                }
              }
              obelisk.sv.statement.expression_statement attributes {node_id = 20 : i64} {
                obelisk.sv.expression.call attributes {argument_count = 4 : i64, callee_name = "$assertcontrol", constraint_restrictions = [], defaulted_arguments = array<i64: 0, 0, 0, 0>, has_inline_constraints = false, has_iterator_expression = false, has_output_arguments = false, has_this_class = false, is_signed = false, is_super_class = false, is_system_call = true, node_id = 21 : i64, semantic_type = !obelisk.void, subroutine_kind = 1 : i32, system_library_cell = "work.top", system_scope_path = "top", system_scope_symbol = @s1.$root::@s3.top::@s4.top} {
                  obelisk.sv.expression.named_value attributes {is_signed = true, node_id = 22 : i64, referenced_path = "top.mask", referenced_symbol = @s1.$root::@s3.top::@s4.top::@s5.mask, semantic_type = !obelisk.integral<32, true, false, 31 : 0, int>} {
                  }
                  obelisk.sv.expression.integer_literal attributes {constant_value = "2", is_declared_unsized = true, is_signed = true, node_id = 23 : i64, semantic_type = !obelisk.integral<32, true, false, 31 : 0, int>} {
                  }
                  obelisk.sv.expression.integer_literal attributes {constant_value = "1", is_declared_unsized = true, is_signed = true, node_id = 24 : i64, semantic_type = !obelisk.integral<32, true, false, 31 : 0, int>} {
                  }
                  obelisk.sv.expression.integer_literal attributes {constant_value = "0", is_declared_unsized = true, is_signed = true, node_id = 25 : i64, semantic_type = !obelisk.integral<32, true, false, 31 : 0, int>} {
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

// CHECK-DAG: arith.constant -256 : i64
// CHECK-DAG: arith.constant -8 : i64
// CHECK-DAG: arith.constant 2 : i64
// CHECK-DAG: arith.constant 1 : i64
// CHECK-DAG: simulation.bytes.constant "WARNING:{{.*}}$assertcontrol control type is outside the valid range 1 through 11; the task has no effect"
// CHECK: %[[MASK32:.*]] = simulation.ref.load
// CHECK: %[[MASK:.*]] = arith.extsi %[[MASK32]] : i32 to i64
// CHECK: %[[BAD_ASSERT:.*]] = arith.andi %[[MASK]],
// CHECK: %[[BAD_ASSERTED:.*]] = arith.cmpi ne, %[[BAD_ASSERT]],
// CHECK: %[[BAD_DIRECTIVE:.*]] = arith.andi %[[MASK]],
// CHECK: %[[BAD_DIRECTED:.*]] = arith.cmpi ne, %[[BAD_DIRECTIVE]],
// CHECK: arith.ori %[[BAD_ASSERTED]], %[[BAD_DIRECTED]] : i1
// CHECK: simulation.fatal
// CHECK: %[[ASSERT_TYPE:.*]] = arith.andi %[[MASK]],
// CHECK: %[[ASSERT_MATCH:.*]] = arith.cmpi ne, %[[ASSERT_TYPE]],
// CHECK: %[[DIRECTIVE_TYPE:.*]] = arith.andi %[[MASK]],
// CHECK: %[[DIRECTIVE_MATCH:.*]] = arith.cmpi ne, %[[DIRECTIVE_TYPE]],
// CHECK: arith.andi %[[ASSERT_MATCH]], %[[DIRECTIVE_MATCH]] : i1
// CHECK: simulation.assert.control {{.*}} action <off> assertion
// CHECK: %[[ACTION32:.*]] = simulation.ref.load
// CHECK: %[[ACTION:.*]] = arith.extsi %[[ACTION32]] : i32 to i64
// CHECK: arith.cmpi ult, %[[ACTION]],
// CHECK: arith.cmpi ugt, %[[ACTION]],
// CHECK: simulation.assert.control.dynamic {{.*}} action %[[ACTION32]] assertion
