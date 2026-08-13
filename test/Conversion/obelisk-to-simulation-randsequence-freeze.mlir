// RUN: obelisk-opt %s --obelisk-sim-prepare | FileCheck %s

// IEEE 1800-2017 18.17 gives a randsequence an automatic grammar scope.
// Production declarations remain symbols in that scope, while preparation
// freezes a non-symbol production graph into the isolated executable unit.

module {
  obelisk.sv.symbol.definition attributes {definition_kind = 0 : i32, hierarchical_name = "m", name = "m", node_id = 0 : i64, sym_name = "s0.m"} {
  }
  obelisk.sv.symbol.root attributes {hierarchical_name = "\\$root ", name = "$root", node_id = 1 : i64, sym_name = "s1.$root"} {
    obelisk.sv.symbol.compilation_unit attributes {hierarchical_name = "$unit", node_id = 2 : i64, sym_name = "s2"} {
    }
    obelisk.sv.symbol.instance attributes {hierarchical_name = "m", is_uninstantiated = false, name = "m", node_id = 3 : i64, referenced_path = "m", referenced_symbol = @s0.m, sym_name = "s3.m"} {
      obelisk.sv.symbol.instance_body attributes {hierarchical_name = "m", name = "m", node_id = 4 : i64, sym_name = "s4.m", time_precision_fs = 1000000 : i64, time_unit_fs = 1000000 : i64} {
        obelisk.sv.symbol.statement_block attributes {block_kind = 0 : i32, hierarchical_name = "m", node_id = 5 : i64, sym_name = "s5"} {
          obelisk.sv.symbol.rand_seq_production attributes {argument_count = 0 : i64, hierarchical_name = "m.main", name = "main", node_id = 6 : i64, rule_blocks = [@s1.$root::@s3.m::@s4.m::@s5::@s6.main::@s7], rule_count = 1 : i64, rule_has_rand_join_expressions = array<i64: 0>, rule_has_weight_code_blocks = array<i64: 0>, rule_has_weights = array<i64: 0>, rule_is_rand_join = array<i64: 0>, rule_item_counts = array<i64: 1>, semantic_type = !obelisk.void, sym_name = "s6.main"} {
            obelisk.sv.symbol.statement_block attributes {block_kind = 0 : i32, hierarchical_name = "m.main", node_id = 7 : i64, sym_name = "s7"} {
            }
            obelisk.sv.rand_seq.item attributes {argument_count = 0 : i64, has_target = true, node_id = 8 : i64, target = @s1.$root::@s3.m::@s4.m::@s5::@s8.leaf, target_path = "m.leaf"} {
            }
          }
          obelisk.sv.symbol.rand_seq_production attributes {argument_count = 1 : i64, hierarchical_name = "m.leaf", name = "leaf", node_id = 9 : i64, rule_blocks = [@s1.$root::@s3.m::@s4.m::@s5::@s8.leaf::@s10], rule_count = 1 : i64, rule_has_rand_join_expressions = array<i64: 0>, rule_has_weight_code_blocks = array<i64: 0>, rule_has_weights = array<i64: 0>, rule_is_rand_join = array<i64: 0>, rule_item_counts = array<i64: 1>, semantic_type = !obelisk.void, sym_name = "s8.leaf"} {
            obelisk.sv.symbol.formal_argument attributes {direction = 0 : i32, hierarchical_name = "m.leaf.value", name = "value", node_id = 10 : i64, semantic_type = !obelisk.integral<32, true, false, 31 : 0, int>, sym_name = "s9.value"} {
              obelisk.sv.expression.integer_literal attributes {constant_value = "7", is_signed = true, node_id = 11 : i64, semantic_type = !obelisk.integral<32, true, false, 31 : 0, int>} {
              }
            }
            obelisk.sv.symbol.statement_block attributes {block_kind = 0 : i32, hierarchical_name = "m.leaf", node_id = 12 : i64, sym_name = "s10"} {
            }
            obelisk.sv.rand_seq.code_block attributes {block = @s1.$root::@s3.m::@s4.m::@s5::@s8.leaf::@s10, block_path = "m.leaf", node_id = 13 : i64} {
              obelisk.sv.statement.block attributes {node_id = 14 : i64} {
                obelisk.sv.statement.expression_statement attributes {node_id = 17 : i64} {
                  obelisk.sv.expression.named_value attributes {is_signed = true, node_id = 18 : i64, referenced_path = "m.leaf.value", referenced_symbol = @s1.$root::@s3.m::@s4.m::@s5::@s8.leaf::@s9.value, semantic_type = !obelisk.integral<32, true, false, 31 : 0, int>} {
                  }
                }
              }
            }
          }
        }
        obelisk.sv.symbol.procedural_block attributes {hierarchical_name = "m", node_id = 15 : i64, procedure_kind = 0 : i32, sym_name = "s11", time_precision_fs = 1000000 : i64, time_unit_fs = 1000000 : i64} {
          obelisk.sv.statement.rand_sequence attributes {first_production = @s1.$root::@s3.m::@s4.m::@s5::@s6.main, first_production_path = "m.main", has_first_production = true, node_id = 16 : i64, production_count = 2 : i64} {
          }
        }
      }
    }
  }
}

// CHECK-LABEL: obelisk_sim.func private @unit_0
// CHECK-SAME: #obelisk_sim.local_binding<path = "m.leaf.value", type = i32, automatic = true
// CHECK: obelisk.sv.statement.rand_sequence
// CHECK-NEXT: obelisk.sv.rand_seq.frozen_production attributes {{.*}}formal_arguments = []{{.*}}referenced_path = "m.main"{{.*}}referenced_symbol = @s1.$root::@s3.m::@s4.m::@s5::@s6.main{{.*}}rule_item_counts = array<i64: 1>{{.*}}rule_variables =
// CHECK: obelisk.sv.rand_seq.item attributes {{.*}}target_path = "m.leaf"
// CHECK: obelisk.sv.rand_seq.frozen_production attributes {{.*}}formal_arguments = [{{.*}}default_operand_count = 1 : i64{{.*}}referenced_path = "m.leaf.value"{{.*}}referenced_symbol = @s1.$root::@s3.m::@s4.m::@s5::@s8.leaf::@s9.value{{.*}}]{{.*}}referenced_path = "m.leaf"
// CHECK-NEXT: obelisk.sv.expression.integer_literal attributes {{.*}}constant_value = "7"
// CHECK: obelisk.sv.rand_seq.code_block
// CHECK-NEXT: obelisk.sv.statement.block
