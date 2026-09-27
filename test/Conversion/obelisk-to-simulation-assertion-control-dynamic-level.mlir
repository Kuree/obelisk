// RUN: obelisk-opt %s '--lower-obelisk-to-sim=opt-level=0' | FileCheck %s
// RUN: obelisk-opt %s '--lower-obelisk-to-sim=opt-level=3' --emit-bytecode -o /dev/null

// IEEE 1800-2017 20.12: the levels operand is evaluated at execution time.
// Preparation resolves the possible hierarchy targets and lowering compares
// the live value against their relative depths before applying control.
module {
  obelisk.sv.symbol.definition attributes {definition_kind = 0 : i32, hierarchical_name = "top", name = "top", node_id = 0 : i64, sym_name = "s0.top"} {
  }
  obelisk.sv.symbol.root attributes {hierarchical_name = "\\$root ", name = "$root", node_id = 1 : i64, sym_name = "s1.$root"} {
    obelisk.sv.symbol.compilation_unit attributes {hierarchical_name = "$unit", node_id = 2 : i64, sym_name = "s2"} {
    }
    obelisk.sv.symbol.instance attributes {hierarchical_name = "top", is_uninstantiated = false, name = "top", node_id = 3 : i64, referenced_path = "top", referenced_symbol = @s0.top, sym_name = "s3.top"} {
      obelisk.sv.symbol.instance_body attributes {hierarchical_name = "top", name = "top", node_id = 4 : i64, sym_name = "s4.top", time_precision_fs = 1000000 : i64, time_unit_fs = 1000000 : i64} {
        obelisk.sv.symbol.variable attributes {hierarchical_name = "top.levels", lifetime = 1 : i32, name = "levels", node_id = 5 : i64, semantic_type = !obelisk.integral<32, true, false, 31 : 0, int>, sym_name = "s5.levels"} {
        }
        obelisk.sv.symbol.statement_block attributes {block_kind = 0 : i32, hierarchical_name = "top.target", name = "target", node_id = 6 : i64, sym_name = "s6.target"} {
        }
        obelisk.sv.symbol.procedural_block attributes {hierarchical_name = "top", node_id = 7 : i64, procedure_kind = 0 : i32, sym_name = "s7", time_precision_fs = 1000000 : i64, time_unit_fs = 1000000 : i64} {
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
                obelisk.sv.expression.call attributes {argument_count = 1 : i64, callee_name = "$assertoff", constraint_restrictions = [], defaulted_arguments = array<i64: 0>, has_inline_constraints = false, has_iterator_expression = false, has_output_arguments = false, has_this_class = false, is_signed = false, is_super_class = false, is_system_call = true, node_id = 15 : i64, semantic_type = !obelisk.void, subroutine_kind = 1 : i32, system_library_cell = "work.top", system_scope_path = "top", system_scope_symbol = @s1.$root::@s3.top::@s4.top} {
                  obelisk.sv.expression.named_value attributes {is_signed = true, node_id = 16 : i64, referenced_path = "top.levels", referenced_symbol = @s1.$root::@s3.top::@s4.top::@s5.levels, semantic_type = !obelisk.integral<32, true, false, 31 : 0, int>} {
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

// CHECK: %[[ZERO:.*]] = arith.constant 0 : i64
// CHECK: %[[LEVELS32:.*]] = simulation.ref.load {{.*}} : !simulation.ref<i32> -> i32
// CHECK: %[[LEVELS:.*]] = arith.extsi %[[LEVELS32]] : i32 to i64
// CHECK: %[[ALL:.*]] = arith.cmpi eq, %[[LEVELS]], %[[ZERO]] : i64
// CHECK: %[[INCLUDED:.*]] = arith.cmpi ugt, %[[LEVELS]], %[[ZERO]] : i64
// CHECK: %[[SELECTED:.*]] = arith.ori %[[ALL]], %[[INCLUDED]] : i1
// CHECK: cf.cond_br %[[SELECTED]]
// CHECK: simulation.assert.control {{.*}} action <off> assertion
