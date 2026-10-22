// RUN: obelisk-opt %s '--lower-obelisk-to-sim=opt-level=0' | FileCheck %s

// IEEE 1800-2023 11.4.13: [A +/- B] spans A -/+ type(A)'(B) and [A +%- B]
// spans A -/+ type(A)'(A*B/100.0), truncating a real tolerance of an
// integral A. The bounds are swapped as needed, so either order matches.
//
// module top;
//   int a, p;
//   bit x, y;
//   initial begin
//     x = p inside {[a +/- 1]};
//     y = p inside {[a +%- 25]};
//   end
// endmodule

module attributes {coverage.language_version = 2023 : i32} {
  obelisk.sv.symbol.definition @s0.top attributes {definition_kind = 0 : i32, hierarchical_name = "top", name = "top", node_id = 0 : i64} {
  }
  obelisk.sv.symbol.root @s1.$root attributes {hierarchical_name = "\\$root ", name = "$root", node_id = 1 : i64} {
    obelisk.sv.symbol.compilation_unit @s2 attributes {hierarchical_name = "$unit", node_id = 2 : i64, simulation.vpi_definition_name = "$unit"} {
    }
    obelisk.sv.symbol.instance @s3.top attributes {hierarchical_name = "top", is_uninstantiated = false, name = "top", node_id = 3 : i64, referenced_path = "top", referenced_symbol = @s0.top} {
      obelisk.sv.symbol.instance_body @s4.top attributes {hierarchical_name = "top", name = "top", node_id = 4 : i64, simulation.vpi_automatic = false, simulation.vpi_cell_instance = false, simulation.vpi_definition_name = "top", simulation.vpi_top = true, time_precision_fs = 1000000 : i64, time_unit_fs = 1000000 : i64, vpi_scope_kind = 32 : i32} {
        obelisk.sv.symbol.variable @s5.a attributes {hierarchical_name = "top.a", lifetime = 1 : i32, name = "a", node_id = 5 : i64, semantic_type = !obelisk.integral<32, true, false, 31 : 0, int>} {
        }
        obelisk.sv.symbol.variable @s6.p attributes {hierarchical_name = "top.p", lifetime = 1 : i32, name = "p", node_id = 6 : i64, semantic_type = !obelisk.integral<32, true, false, 31 : 0, int>} {
        }
        obelisk.sv.symbol.variable @s7.x attributes {hierarchical_name = "top.x", lifetime = 1 : i32, name = "x", node_id = 7 : i64, semantic_type = !obelisk.integral<1, false, false, 0 : 0, bit>} {
        }
        obelisk.sv.symbol.variable @s8.y attributes {hierarchical_name = "top.y", lifetime = 1 : i32, name = "y", node_id = 8 : i64, semantic_type = !obelisk.integral<1, false, false, 0 : 0, bit>} {
        }
        obelisk.sv.symbol.procedural_block @s9 attributes {hierarchical_name = "top", node_id = 9 : i64, procedure_kind = 0 : i32, time_precision_fs = 1000000 : i64, time_unit_fs = 1000000 : i64} {
          obelisk.sv.statement.block attributes {node_id = 10 : i64} {
            obelisk.sv.statement.list attributes {node_id = 11 : i64} {
              obelisk.sv.statement.expression_statement attributes {node_id = 12 : i64} {
                obelisk.sv.expression.assignment attributes {assignment_kind = 0 : i32, is_signed = false, node_id = 13 : i64, semantic_type = !obelisk.integral<1, false, false, 0 : 0, bit>} {
                  obelisk.sv.expression.named_value attributes {is_signed = false, node_id = 14 : i64, referenced_path = "top.x", referenced_symbol = @s1.$root::@s3.top::@s4.top::@s7.x, semantic_type = !obelisk.integral<1, false, false, 0 : 0, bit>} {
                  }
                  obelisk.sv.expression.conversion attributes {is_implicit = true, is_signed = false, node_id = 15 : i64, semantic_type = !obelisk.integral<1, false, false, 0 : 0, bit>} {
                    obelisk.sv.expression.inside attributes {is_signed = false, item_count = 1 : i64, node_id = 16 : i64, semantic_type = !obelisk.integral<1, false, true, 0 : 0, logic>} {
                      obelisk.sv.expression.named_value attributes {is_signed = true, node_id = 17 : i64, referenced_path = "top.p", referenced_symbol = @s1.$root::@s3.top::@s4.top::@s6.p, semantic_type = !obelisk.integral<32, true, false, 31 : 0, int>} {
                      }
                      obelisk.sv.expression.value_range attributes {is_signed = false, node_id = 18 : i64, range_kind = 1 : i32, semantic_type = !obelisk.void} {
                        obelisk.sv.expression.named_value attributes {is_signed = true, node_id = 19 : i64, referenced_path = "top.a", referenced_symbol = @s1.$root::@s3.top::@s4.top::@s5.a, semantic_type = !obelisk.integral<32, true, false, 31 : 0, int>} {
                        }
                        obelisk.sv.expression.integer_literal attributes {constant_value = "1", is_declared_unsized = true, is_signed = true, node_id = 20 : i64, semantic_type = !obelisk.integral<32, true, false, 31 : 0, int>} {
                        }
                      }
                    }
                  }
                }
              }
              obelisk.sv.statement.expression_statement attributes {node_id = 21 : i64} {
                obelisk.sv.expression.assignment attributes {assignment_kind = 0 : i32, is_signed = false, node_id = 22 : i64, semantic_type = !obelisk.integral<1, false, false, 0 : 0, bit>} {
                  obelisk.sv.expression.named_value attributes {is_signed = false, node_id = 23 : i64, referenced_path = "top.y", referenced_symbol = @s1.$root::@s3.top::@s4.top::@s8.y, semantic_type = !obelisk.integral<1, false, false, 0 : 0, bit>} {
                  }
                  obelisk.sv.expression.conversion attributes {is_implicit = true, is_signed = false, node_id = 24 : i64, semantic_type = !obelisk.integral<1, false, false, 0 : 0, bit>} {
                    obelisk.sv.expression.inside attributes {is_signed = false, item_count = 1 : i64, node_id = 25 : i64, semantic_type = !obelisk.integral<1, false, true, 0 : 0, logic>} {
                      obelisk.sv.expression.named_value attributes {is_signed = true, node_id = 26 : i64, referenced_path = "top.p", referenced_symbol = @s1.$root::@s3.top::@s4.top::@s6.p, semantic_type = !obelisk.integral<32, true, false, 31 : 0, int>} {
                      }
                      obelisk.sv.expression.value_range attributes {is_signed = false, node_id = 27 : i64, range_kind = 2 : i32, semantic_type = !obelisk.void} {
                        obelisk.sv.expression.named_value attributes {is_signed = true, node_id = 28 : i64, referenced_path = "top.a", referenced_symbol = @s1.$root::@s3.top::@s4.top::@s5.a, semantic_type = !obelisk.integral<32, true, false, 31 : 0, int>} {
                        }
                        obelisk.sv.expression.integer_literal attributes {constant_value = "25", is_declared_unsized = true, is_signed = true, node_id = 29 : i64, semantic_type = !obelisk.integral<32, true, false, 31 : 0, int>} {
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


// CHECK-LABEL: simulation.func private @unit_0(
// CHECK-DAG: %[[PERCENT:.*]] = arith.constant 2.500000e+01 : f64
// CHECK-DAG: %[[HUNDRED:.*]] = arith.constant 1.000000e+02 : f64
// CHECK: %[[P:.*]] = simulation.ref.load %arg2
// CHECK: %[[A:.*]] = simulation.ref.load %arg1
// CHECK-DAG: %[[LOW:.*]] = arith.subi %[[A]], %c1_i32
// CHECK-DAG: %[[HIGH:.*]] = arith.addi %[[A]], %c1_i32
// CHECK: %[[ABOVE:.*]] = arith.cmpi sge, %[[P]], %[[LOW]]
// CHECK: %[[BELOW:.*]] = arith.cmpi sle, %[[P]], %[[HIGH]]
// CHECK: %[[ABOVE_HIGH:.*]] = arith.cmpi sge, %[[P]], %[[HIGH]]
// CHECK: %[[BELOW_LOW:.*]] = arith.cmpi sle, %[[P]], %[[LOW]]
// CHECK: %[[ASCENDING:.*]] = arith.andi %[[ABOVE]], %[[BELOW]]
// CHECK: %[[DESCENDING:.*]] = arith.andi %[[ABOVE_HIGH]], %[[BELOW_LOW]]
// CHECK: %[[X:.*]] = arith.ori %[[ASCENDING]], %[[DESCENDING]]
// CHECK: simulation.ref.store %[[X]] to %arg3

// CHECK: %[[CENTER:.*]] = simulation.real.from_integer %{{.*}} signed = true : i32 -> f64
// CHECK: %[[PRODUCT:.*]] = arith.mulf %[[CENTER]], %[[PERCENT]]
// CHECK: %[[REAL_STEP:.*]] = arith.divf %[[PRODUCT]], %[[HUNDRED]]
// CHECK: %[[BITS:.*]] = arith.bitcast %[[REAL_STEP]] : f64 to i64
// CHECK: %[[TRUNCATED:.*]] = arith.bitcast %{{.*}} : i64 to f64
// CHECK: %[[STEP:.*]] = simulation.real.to_integer %[[TRUNCATED]] signed = true : i32
// CHECK-DAG: arith.subi %{{.*}}, %[[STEP]]
// CHECK-DAG: arith.addi %{{.*}}, %[[STEP]]
