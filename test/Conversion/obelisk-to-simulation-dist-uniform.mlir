// RUN: obelisk-opt %s '--lower-obelisk-to-sim=opt-level=0' | FileCheck %s

// CHECK-LABEL: obelisk_sim.func private @unit_1(
// CHECK-DAG: %[[HIGH:.*]] = arith.constant 100 : i32
// CHECK-DAG: %[[LOW:.*]] = arith.constant 0 : i32
// CHECK: %[[SEED_LOGIC:.*]] = obelisk_sim.ref.load
// CHECK: %[[SEED_BITS:.*]] = obelisk_sim.logic.to_bits %[[SEED_LOGIC]]
// CHECK-NOT: obelisk_sim.random.seed
// CHECK: %[[RESULT:.*]], %[[NEXT_SEED:.*]] = obelisk_sim.random.distribution {{.*}}, %[[SEED_BITS]], %[[LOW]], %[[HIGH]] {distribution = 0 : i32}
// CHECK: %[[UPDATED:.*]] = obelisk_sim.logic.from_bits %[[NEXT_SEED]]
// CHECK: obelisk_sim.ref.store %[[UPDATED]]
// CHECK: obelisk_sim.display {{.*}}({{.*}}, %[[RESULT]])

module {
  obelisk.sv.symbol.definition attributes {definition_kind = 0 : i32, hierarchical_name = "top", name = "top", node_id = 0 : i64, sym_name = "s0.top"} {
  }
  obelisk.sv.symbol.root attributes {hierarchical_name = "\\$root ", name = "$root", node_id = 1 : i64, sym_name = "s1.$root"} {
    obelisk.sv.symbol.compilation_unit attributes {hierarchical_name = "$unit", node_id = 2 : i64, sym_name = "s2"} {
    }
    obelisk.sv.symbol.instance attributes {hierarchical_name = "top", is_uninstantiated = false, name = "top", node_id = 3 : i64, referenced_path = "top", referenced_symbol = @s0.top, sym_name = "s3.top"} {
      obelisk.sv.symbol.instance_body attributes {hierarchical_name = "top", name = "top", node_id = 4 : i64, sym_name = "s4.top"} {
        obelisk.sv.symbol.statement_block attributes {block_kind = 0 : i32, hierarchical_name = "top", node_id = 5 : i64, sym_name = "s5"} {
          obelisk.sv.symbol.variable attributes {hierarchical_name = "top.seed", lifetime = 1 : i32, name = "seed", node_id = 6 : i64, semantic_type = !obelisk.integral<32, true, true, 31 : 0, integer>, sym_name = "s6.seed"} {
            obelisk.sv.expression.conversion attributes {node_id = 7 : i64, semantic_type = !obelisk.integral<32, true, true, 31 : 0, integer>} {
              obelisk.sv.expression.integer_literal attributes {constant_value = "1234", node_id = 8 : i64, semantic_type = !obelisk.integral<32, true, false, 31 : 0, int>} {
              }
            }
          }
        }
        obelisk.sv.symbol.procedural_block attributes {hierarchical_name = "top", node_id = 9 : i64, procedure_kind = 0 : i32, sym_name = "s7", time_precision_fs = 1000000 : i64, time_unit_fs = 1000000 : i64} {
          obelisk.sv.statement.block attributes {node_id = 10 : i64} {
            obelisk.sv.statement.list attributes {node_id = 11 : i64} {
              obelisk.sv.statement.variable_declaration attributes {node_id = 12 : i64, referenced_path = "top.seed", referenced_symbol = @s1.$root::@s3.top::@s4.top::@s5::@s6.seed} {
              }
              obelisk.sv.statement.expression_statement attributes {node_id = 13 : i64} {
                obelisk.sv.expression.call attributes {argument_count = 2 : i64, callee_name = "$display", constraint_restrictions = [], defaulted_arguments = array<i64: 0, 0>, has_inline_constraints = false, has_iterator_expression = false, has_output_arguments = false, has_this_class = false, is_super_class = false, is_system_call = true, node_id = 14 : i64, semantic_type = !obelisk.void, subroutine_kind = 1 : i32, system_library_cell = "work.top", system_scope_path = "top", system_scope_symbol = @s1.$root::@s3.top::@s4.top::@s5} {
                  obelisk.sv.expression.string_literal attributes {constant_value = "%d", node_id = 15 : i64, semantic_type = !obelisk.ranged_packed_array<15 : 0 x !obelisk.integral<1, false, false, 0 : 0, bit>>} {
                  }
                  obelisk.sv.expression.call attributes {argument_count = 3 : i64, callee_name = "$dist_uniform", constraint_restrictions = [], defaulted_arguments = array<i64: 0, 0, 0>, has_inline_constraints = false, has_iterator_expression = false, has_output_arguments = true, has_this_class = false, is_super_class = false, is_system_call = true, node_id = 16 : i64, semantic_type = !obelisk.integral<32, true, false, 31 : 0, int>, subroutine_kind = 0 : i32, system_library_cell = "work.top", system_scope_path = "top", system_scope_symbol = @s1.$root::@s3.top::@s4.top::@s5} {
                    obelisk.sv.expression.assignment attributes {assignment_kind = 0 : i32, node_id = 17 : i64, semantic_type = !obelisk.integral<32, true, true, 31 : 0, integer>} {
                      obelisk.sv.expression.named_value attributes {node_id = 18 : i64, referenced_path = "top.seed", referenced_symbol = @s1.$root::@s3.top::@s4.top::@s5::@s6.seed, semantic_type = !obelisk.integral<32, true, true, 31 : 0, integer>} {
                      }
                      obelisk.sv.expression.empty_argument attributes {node_id = 19 : i64, semantic_type = !obelisk.integral<32, true, true, 31 : 0, integer>} {
                      }
                    }
                    obelisk.sv.expression.integer_literal attributes {constant_value = "0", node_id = 20 : i64, semantic_type = !obelisk.integral<32, true, false, 31 : 0, int>} {
                    }
                    obelisk.sv.expression.integer_literal attributes {constant_value = "100", node_id = 21 : i64, semantic_type = !obelisk.integral<32, true, false, 31 : 0, int>} {
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
