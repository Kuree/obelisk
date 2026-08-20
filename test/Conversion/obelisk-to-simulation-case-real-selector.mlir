// RUN: obelisk-opt %s '--lower-obelisk-to-sim=opt-level=0' | FileCheck %s

// IEEE 1800-2017 12.5: a case statement compares its expression against each
// item for an exact match. Table 11-1 leaves the case equality operators
// undefined for real and shortreal while ordinary equality accepts any type,
// so a real selector matches its labels with a floating-point comparison --
// a real value carries no x or z bits for `===` to distinguish.

// CHECK-LABEL: obelisk_sim.func private @unit_0
// CHECK: %[[LABEL:.*]] = arith.constant 1.500000e+00 : f64
// CHECK: %[[SELECTOR:.*]] = obelisk_sim.ref.load %{{.*}} : !obelisk_sim.ref<f64> -> f64
// CHECK: %[[MATCH:.*]] = arith.cmpf oeq, %[[SELECTOR]], %[[LABEL]] : f64
// CHECK: cf.cond_br %[[MATCH]]

module attributes {llvm.data_layout = "e-m:e-p:64:64-i64:64-n8:16:32:64-S128", llvm.target_triple = "x86_64-unknown-linux-gnu"} {
  obelisk.sv.symbol.definition attributes {definition_kind = 0 : i32, hierarchical_name = "t", name = "t", node_id = 0 : i64, sym_name = "s0.t"} {
  }
  obelisk.sv.symbol.root attributes {hierarchical_name = "\\$root ", name = "$root", node_id = 1 : i64, sym_name = "s1.$root"} {
    obelisk.sv.symbol.compilation_unit attributes {hierarchical_name = "$unit", node_id = 2 : i64, sym_name = "s2"} {
    }
    obelisk.sv.symbol.instance attributes {hierarchical_name = "t", is_uninstantiated = false, name = "t", node_id = 3 : i64, referenced_path = "t", referenced_symbol = @s0.t, sym_name = "s3.t"} {
      obelisk.sv.symbol.instance_body attributes {hierarchical_name = "t", name = "t", node_id = 4 : i64, sym_name = "s4.t", time_precision_fs = 1000000 : i64, time_unit_fs = 1000000 : i64} {
        obelisk.sv.symbol.variable attributes {hierarchical_name = "t.r", lifetime = 1 : i32, name = "r", node_id = 5 : i64, semantic_type = !obelisk.real, sym_name = "s5.r"} {
        }
        obelisk.sv.symbol.variable attributes {hierarchical_name = "t.hits", lifetime = 1 : i32, name = "hits", node_id = 6 : i64, semantic_type = !obelisk.integral<32, true, false, 31 : 0, int>, sym_name = "s6.hits"} {
        }
        obelisk.sv.symbol.procedural_block attributes {hierarchical_name = "t", node_id = 7 : i64, procedure_kind = 0 : i32, sym_name = "s7", time_precision_fs = 1000000 : i64, time_unit_fs = 1000000 : i64} {
          obelisk.sv.statement.case attributes {check_kind = 0 : i32, condition_kind = 0 : i32, has_default = true, item_count = 1 : i64, item_label_counts = array<i64: 1>, node_id = 8 : i64} {
            obelisk.sv.expression.named_value attributes {is_signed = false, node_id = 9 : i64, referenced_path = "t.r", referenced_symbol = @s1.$root::@s3.t::@s4.t::@s5.r, semantic_type = !obelisk.real} {
            }
            obelisk.sv.expression.real_literal attributes {constant_value = "1.5", is_signed = false, node_id = 10 : i64, semantic_type = !obelisk.real} {
            }
            obelisk.sv.statement.expression_statement attributes {node_id = 11 : i64} {
              obelisk.sv.expression.assignment attributes {assignment_kind = 0 : i32, is_signed = true, node_id = 12 : i64, semantic_type = !obelisk.integral<32, true, false, 31 : 0, int>} {
                obelisk.sv.expression.named_value attributes {is_signed = true, node_id = 13 : i64, referenced_path = "t.hits", referenced_symbol = @s1.$root::@s3.t::@s4.t::@s6.hits, semantic_type = !obelisk.integral<32, true, false, 31 : 0, int>} {
                }
                obelisk.sv.expression.integer_literal attributes {constant_value = "1", is_declared_unsized = true, is_signed = true, node_id = 14 : i64, semantic_type = !obelisk.integral<32, true, false, 31 : 0, int>} {
                }
              }
            }
            obelisk.sv.statement.expression_statement attributes {node_id = 15 : i64} {
              obelisk.sv.expression.assignment attributes {assignment_kind = 0 : i32, is_signed = true, node_id = 16 : i64, semantic_type = !obelisk.integral<32, true, false, 31 : 0, int>} {
                obelisk.sv.expression.named_value attributes {is_signed = true, node_id = 17 : i64, referenced_path = "t.hits", referenced_symbol = @s1.$root::@s3.t::@s4.t::@s6.hits, semantic_type = !obelisk.integral<32, true, false, 31 : 0, int>} {
                }
                obelisk.sv.expression.integer_literal attributes {constant_value = "2", is_declared_unsized = true, is_signed = true, node_id = 18 : i64, semantic_type = !obelisk.integral<32, true, false, 31 : 0, int>} {
                }
              }
            }
          }
        }
      }
    }
  }
}

