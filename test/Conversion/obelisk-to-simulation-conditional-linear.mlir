// RUN: obelisk-opt %s '--lower-obelisk-to-sim=opt-level=0' | FileCheck %s

// Each conditional arm must be lowered once. In particular, the nested false
// arm must not be cloned again for the ambiguous-condition path.
module {
  obelisk.sv.symbol.definition attributes {definition_kind = 0 : i32, hierarchical_name = "conditional_linear", name = "conditional_linear", node_id = 0 : i64, sym_name = "s0.conditional_linear"} {
  }
  obelisk.sv.symbol.root attributes {hierarchical_name = "\\$root ", name = "$root", node_id = 1 : i64, sym_name = "s1.$root"} {
    obelisk.sv.symbol.compilation_unit attributes {hierarchical_name = "$unit", node_id = 2 : i64, sym_name = "s2"} {
    }
    obelisk.sv.symbol.instance attributes {hierarchical_name = "conditional_linear", is_uninstantiated = false, name = "conditional_linear", node_id = 3 : i64, referenced_path = "conditional_linear", referenced_symbol = @s0.conditional_linear, sym_name = "s3.conditional_linear"} {
      obelisk.sv.symbol.instance_body attributes {hierarchical_name = "conditional_linear", name = "conditional_linear", node_id = 4 : i64, sym_name = "s4.conditional_linear", time_precision_fs = 1000000 : i64, time_unit_fs = 1000000 : i64} {
        obelisk.sv.symbol.variable attributes {hierarchical_name = "conditional_linear.condition", lifetime = 1 : i32, name = "condition", node_id = 5 : i64, semantic_type = !obelisk.integral<1, false, true, 0 : 0, logic>, sym_name = "s5.condition"} {
        }
        obelisk.sv.symbol.variable attributes {hierarchical_name = "conditional_linear.result", lifetime = 1 : i32, name = "result", node_id = 6 : i64, semantic_type = !obelisk.real, sym_name = "s6.result"} {
        }
        obelisk.sv.symbol.procedural_block attributes {hierarchical_name = "conditional_linear", node_id = 7 : i64, procedure_kind = 0 : i32, sym_name = "s7", time_precision_fs = 1000000 : i64, time_unit_fs = 1000000 : i64} {
          obelisk.sv.statement.block attributes {node_id = 8 : i64} {
            obelisk.sv.statement.list attributes {node_id = 9 : i64} {
              obelisk.sv.statement.expression_statement attributes {node_id = 10 : i64} {
                obelisk.sv.expression.assignment attributes {assignment_kind = 0 : i32, node_id = 11 : i64, semantic_type = !obelisk.real} {
                  obelisk.sv.expression.named_value attributes {node_id = 12 : i64, referenced_path = "conditional_linear.result", referenced_symbol = @s1.$root::@s3.conditional_linear::@s4.conditional_linear::@s6.result, semantic_type = !obelisk.real} {
                  }
                  obelisk.sv.expression.conditional_op attributes {condition_count = 1 : i64, condition_pattern_flags = array<i64: 0>, node_id = 13 : i64, semantic_type = !obelisk.real} {
                    obelisk.sv.expression.named_value attributes {node_id = 14 : i64, referenced_path = "conditional_linear.condition", referenced_symbol = @s1.$root::@s3.conditional_linear::@s4.conditional_linear::@s5.condition, semantic_type = !obelisk.integral<1, false, true, 0 : 0, logic>} {
                    }
                    obelisk.sv.expression.real_literal attributes {constant_value = "1.0", node_id = 15 : i64, semantic_type = !obelisk.real} {
                    }
                    obelisk.sv.expression.conditional_op attributes {condition_count = 1 : i64, condition_pattern_flags = array<i64: 0>, node_id = 16 : i64, semantic_type = !obelisk.real} {
                      obelisk.sv.expression.named_value attributes {node_id = 17 : i64, referenced_path = "conditional_linear.condition", referenced_symbol = @s1.$root::@s3.conditional_linear::@s4.conditional_linear::@s5.condition, semantic_type = !obelisk.integral<1, false, true, 0 : 0, logic>} {
                      }
                      obelisk.sv.expression.real_literal attributes {constant_value = "2.0", node_id = 18 : i64, semantic_type = !obelisk.real} {
                      }
                      obelisk.sv.expression.conditional_op attributes {condition_count = 1 : i64, condition_pattern_flags = array<i64: 0>, node_id = 19 : i64, semantic_type = !obelisk.real} {
                        obelisk.sv.expression.named_value attributes {node_id = 20 : i64, referenced_path = "conditional_linear.condition", referenced_symbol = @s1.$root::@s3.conditional_linear::@s4.conditional_linear::@s5.condition, semantic_type = !obelisk.integral<1, false, true, 0 : 0, logic>} {
                        }
                        obelisk.sv.expression.real_literal attributes {constant_value = "3.0", node_id = 21 : i64, semantic_type = !obelisk.real} {
                        }
                        obelisk.sv.expression.real_literal attributes {constant_value = "4.0", node_id = 22 : i64, semantic_type = !obelisk.real} {
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
}

// CHECK-LABEL: obelisk_sim.func private @unit_0
// CHECK-COUNT-1: arith.constant 4.000000e+00 : f64
// CHECK-NOT: obelisk.sv.
