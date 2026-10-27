// RUN: talos-opt %s '--lower-talos-to-sim=opt-level=0' | FileCheck %s --check-prefix=VPI
// RUN: talos-opt %s '--lower-talos-to-sim=opt-level=0' | FileCheck %s

// randcase (IEEE 1800-2017 18.16) evaluates every weight once, draws from the
// process random number generator over their sum, and selects the item whose
// cumulative bound first exceeds the draw. The weights are variables so the
// selection arithmetic survives constant folding.

module {
  talos.sv.symbol.definition @s0.randcase_weights attributes {definition_kind = 0 : i32, hierarchical_name = "randcase_weights", name = "randcase_weights", node_id = 0 : i64} {
  }
  talos.sv.symbol.root @s1.$root attributes {hierarchical_name = "\\$root ", name = "$root", node_id = 1 : i64} {
    talos.sv.symbol.compilation_unit @s2 attributes {hierarchical_name = "$unit", node_id = 2 : i64} {
    }
    talos.sv.symbol.instance @s3.randcase_weights attributes {hierarchical_name = "randcase_weights", is_uninstantiated = false, name = "randcase_weights", node_id = 3 : i64, referenced_path = "randcase_weights", referenced_symbol = @s0.randcase_weights} {
      talos.sv.symbol.instance_body @s4.randcase_weights attributes {hierarchical_name = "randcase_weights", name = "randcase_weights", node_id = 4 : i64, time_precision_fs = 1000000 : i64, time_unit_fs = 1000000 : i64} {
        talos.sv.symbol.variable @s5.wa attributes {hierarchical_name = "randcase_weights.wa", lifetime = 1 : i32, name = "wa", node_id = 5 : i64, semantic_type = !talos.integral<32, true, false, 31 : 0, int>} {
        }
        talos.sv.symbol.variable @s6.wb attributes {hierarchical_name = "randcase_weights.wb", lifetime = 1 : i32, name = "wb", node_id = 6 : i64, semantic_type = !talos.integral<32, true, false, 31 : 0, int>} {
        }
        talos.sv.symbol.variable @s7.chosen attributes {hierarchical_name = "randcase_weights.chosen", lifetime = 1 : i32, name = "chosen", node_id = 7 : i64, semantic_type = !talos.ranged_packed_array<1 : 0 x !talos.integral<1, false, false, 0 : 0, bit>>} {
        }
        talos.sv.symbol.procedural_block @s8 attributes {hierarchical_name = "randcase_weights", node_id = 8 : i64, procedure_kind = 0 : i32, time_precision_fs = 1000000 : i64, time_unit_fs = 1000000 : i64} {
          talos.sv.statement.rand_case attributes {item_count = 2 : i64, node_id = 10 : i64} {
            talos.sv.expression.named_value attributes {is_signed = true, node_id = 11 : i64, referenced_path = "randcase_weights.wa", referenced_symbol = @s1.$root::@s3.randcase_weights::@s4.randcase_weights::@s5.wa, semantic_type = !talos.integral<32, true, false, 31 : 0, int>} {
            }
            talos.sv.expression.named_value attributes {is_signed = true, node_id = 12 : i64, referenced_path = "randcase_weights.wb", referenced_symbol = @s1.$root::@s3.randcase_weights::@s4.randcase_weights::@s6.wb, semantic_type = !talos.integral<32, true, false, 31 : 0, int>} {
            }
            talos.sv.statement.expression_statement attributes {node_id = 13 : i64} {
              talos.sv.expression.assignment attributes {assignment_kind = 0 : i32, is_signed = false, node_id = 14 : i64, semantic_type = !talos.ranged_packed_array<1 : 0 x !talos.integral<1, false, false, 0 : 0, bit>>} {
                talos.sv.expression.named_value attributes {is_signed = false, node_id = 15 : i64, referenced_path = "randcase_weights.chosen", referenced_symbol = @s1.$root::@s3.randcase_weights::@s4.randcase_weights::@s7.chosen, semantic_type = !talos.ranged_packed_array<1 : 0 x !talos.integral<1, false, false, 0 : 0, bit>>} {
                }
                talos.sv.expression.integer_literal attributes {constant_value = "2'b1", is_signed = false, node_id = 16 : i64, semantic_type = !talos.ranged_packed_array<1 : 0 x !talos.integral<1, false, false, 0 : 0, bit>>} {
                }
              }
            }
            talos.sv.statement.expression_statement attributes {node_id = 17 : i64} {
              talos.sv.expression.assignment attributes {assignment_kind = 0 : i32, is_signed = false, node_id = 18 : i64, semantic_type = !talos.ranged_packed_array<1 : 0 x !talos.integral<1, false, false, 0 : 0, bit>>} {
                talos.sv.expression.named_value attributes {is_signed = false, node_id = 19 : i64, referenced_path = "randcase_weights.chosen", referenced_symbol = @s1.$root::@s3.randcase_weights::@s4.randcase_weights::@s7.chosen, semantic_type = !talos.ranged_packed_array<1 : 0 x !talos.integral<1, false, false, 0 : 0, bit>>} {
                }
                talos.sv.expression.integer_literal attributes {constant_value = "2'b10", is_signed = false, node_id = 20 : i64, semantic_type = !talos.ranged_packed_array<1 : 0 x !talos.integral<1, false, false, 0 : 0, bit>>} {
                }
              }
            }
          }
        }
      }
    }
  }
}

// Summands are unsigned, and the total retains maximum weight precision.
// A signed value's bit pattern must not be clamped before summation.
// CHECK: %[[ZERO:.*]] = arith.constant 0 : i32
// CHECK: %[[BOUND0:.*]] = simulation.ref.load
// CHECK: %[[SECOND:.*]] = simulation.ref.load
// CHECK: %[[TOTAL:.*]] = arith.addi %[[BOUND0]], %[[SECOND]] : i32

// An all-zero weight list selects no branch at all.
// CHECK: %[[ANY:.*]] = arith.cmpi ne, %[[TOTAL]], %[[ZERO]]
// CHECK: cf.cond_br %[[ANY]], ^[[SELECT:[^ ,]*]], ^[[MERGE:[^ ,]*]]

// CHECK: ^[[SELECT]]:
// CHECK: %[[RANGE:.*]] = arith.extui %[[TOTAL]] : i32 to i64
// CHECK: %[[RAW:.*]] = simulation.random.bounded %{{.*}}, %[[RANGE]]
// CHECK: %[[DRAW:.*]] = arith.trunci %[[RAW]] : i64 to i32
// CHECK: %[[SELECTED:.*]] = arith.cmpi ult, %[[DRAW]], %[[BOUND0]]
// CHECK: cf.cond_br %[[SELECTED]], ^[[ITEM0:[^ ,]*]], ^[[ITEM1:[^ ,]*]]

// The draw is always below the total, so the last item needs no test.
// CHECK: ^[[ITEM1]]:
// CHECK-NOT: arith.cmpi ult
// CHECK: cf.br ^[[MERGE]]

// VPI: simulation.statement.decl [[CASE:[0-9]+]] in [[UNIT:[0-9]+]] scope [[SCOPE:[0-9]+]] type 5 {vpi_properties = #simulation.vpi_properties<[#simulation.vpi_property<selector = 650 : i32, value = 8 : i32>]>}
// VPI: simulation.vpi_thread_origin_binding [[UNIT]] origin [[CASE]]
// VPI: simulation.statement.decl [[FIRST:[0-9]+]] in [[UNIT]] scope [[SCOPE]] type 6 parent [[CASE]]
// VPI: simulation.vpi_statement_relation.decl statement [[CASE]] type 5 selector 6 ordinal 0 modes 2 to [[FIRST]]
// VPI: simulation.statement.decl [[LEFT:[0-9]+]] in [[UNIT]] scope [[SCOPE]] type 3 parent [[FIRST]]
// VPI: simulation.vpi_statement_relation.decl statement [[FIRST]] type 6 selector 104 ordinal 0 modes 1 to [[LEFT]]
// VPI: simulation.statement.decl [[SECOND:[0-9]+]] in [[UNIT]] scope [[SCOPE]] type 6 parent [[CASE]]
// VPI: simulation.vpi_statement_relation.decl statement [[CASE]] type 5 selector 6 ordinal 1 modes 2 to [[SECOND]]
// VPI: simulation.statement.decl [[RIGHT:[0-9]+]] in [[UNIT]] scope [[SCOPE]] type 3 parent [[SECOND]]
