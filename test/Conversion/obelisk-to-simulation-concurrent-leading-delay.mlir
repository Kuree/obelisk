// RUN: obelisk-opt %s '--lower-obelisk-to-sim=opt-level=0' | FileCheck %s

module {
  obelisk.sv.symbol.definition attributes {definition_kind = 0 : i32, hierarchical_name = "t", name = "t", node_id = 0 : i64, sym_name = "s0.t"} {
  }
  obelisk.sv.symbol.root attributes {hierarchical_name = "\\$root ", name = "$root", node_id = 1 : i64, sym_name = "s1.$root"} {
    obelisk.sv.symbol.compilation_unit attributes {hierarchical_name = "$unit", node_id = 2 : i64, sym_name = "s2"} {
    }
    obelisk.sv.symbol.instance attributes {hierarchical_name = "t", is_uninstantiated = false, name = "t", node_id = 3 : i64, referenced_path = "t", referenced_symbol = @s0.t, sym_name = "s3.t"} {
      obelisk.sv.symbol.instance_body attributes {hierarchical_name = "t", name = "t", node_id = 4 : i64, sym_name = "s4.t", time_precision_fs = 1000000 : i64, time_unit_fs = 1000000 : i64} {
        obelisk.sv.symbol.variable attributes {hierarchical_name = "t.clk", lifetime = 1 : i32, name = "clk", node_id = 5 : i64, semantic_type = !obelisk.integral<1, false, true, 0 : 0, logic>, sym_name = "s5.clk"} {
        }
        obelisk.sv.symbol.variable attributes {hierarchical_name = "t.a", lifetime = 1 : i32, name = "a", node_id = 6 : i64, semantic_type = !obelisk.integral<1, false, true, 0 : 0, logic>, sym_name = "s6.a"} {
        }
        obelisk.sv.symbol.variable attributes {hierarchical_name = "t.b", lifetime = 1 : i32, name = "b", node_id = 7 : i64, semantic_type = !obelisk.integral<1, false, true, 0 : 0, logic>, sym_name = "s7.b"} {
        }
        obelisk.sv.symbol.variable attributes {hierarchical_name = "t.failures", lifetime = 1 : i32, name = "failures", node_id = 8 : i64, semantic_type = !obelisk.integral<32, true, false, 31 : 0, int>, sym_name = "s8.failures"} {
        }
        obelisk.sv.symbol.procedural_block attributes {hierarchical_name = "t", node_id = 30 : i64, procedure_kind = 2 : i32, sym_name = "s9", time_precision_fs = 1000000 : i64, time_unit_fs = 1000000 : i64} {
          obelisk.sv.statement.concurrent_assertion attributes {assertion_kind = 0 : i32, has_default_disable = false, has_fail_action = true, has_pass_action = false, node_id = 31 : i64} {
            obelisk.sv.assertion.clocking attributes {node_id = 10 : i64} {
              obelisk.sv.timing.signal_event attributes {edge_kind = 1 : i32, has_iff = false, node_id = 11 : i64} {
                obelisk.sv.expression.named_value attributes {node_id = 12 : i64, referenced_path = "t.clk", referenced_symbol = @s1.$root::@s3.t::@s4.t::@s5.clk, semantic_type = !obelisk.integral<1, false, true, 0 : 0, logic>} {
                }
              }
              obelisk.sv.assertion.binary attributes {node_id = 13 : i64, operator_kind = 11 : i32} {
                obelisk.sv.assertion.simple attributes {has_repetition = false, is_null = false, node_id = 14 : i64, repetition_is_unbounded = false} {
                  obelisk.sv.expression.named_value attributes {node_id = 15 : i64, referenced_path = "t.a", referenced_symbol = @s1.$root::@s3.t::@s4.t::@s6.a, semantic_type = !obelisk.integral<1, false, true, 0 : 0, logic>} {
                  }
                }
                // A leading ##2 is represented as the delay of the first and
                // only concatenation element.
                obelisk.sv.assertion.sequence_concat attributes {delays = [{is_unbounded = false, max = 2 : i64, min = 2 : i64}], node_id = 16 : i64} {
                  obelisk.sv.assertion.simple attributes {has_repetition = false, is_null = false, node_id = 17 : i64, repetition_is_unbounded = false} {
                    obelisk.sv.expression.named_value attributes {node_id = 18 : i64, referenced_path = "t.b", referenced_symbol = @s1.$root::@s3.t::@s4.t::@s7.b, semantic_type = !obelisk.integral<1, false, true, 0 : 0, logic>} {
                    }
                  }
                }
              }
            }
            obelisk.sv.statement.expression_statement attributes {node_id = 19 : i64} {
              obelisk.sv.expression.unary_op attributes {node_id = 20 : i64, operator_kind = 12 : i32, semantic_type = !obelisk.integral<32, true, false, 31 : 0, int>} {
                obelisk.sv.expression.named_value attributes {node_id = 21 : i64, referenced_path = "t.failures", referenced_symbol = @s1.$root::@s3.t::@s4.t::@s8.failures, semantic_type = !obelisk.integral<32, true, false, 31 : 0, int>} {
                }
              }
            }
          }
        }
      }
    }
  }
}

// The antecedent starts age one (mask 2), which advances to age two (mask 4).
// Only an age-two attempt evaluates the consequent b.
// CHECK-LABEL: obelisk_sim.func private @unit_0(
// CHECK: %[[AGE1_MASK:.*]] = arith.constant {{.*}} 2 : i64
// CHECK: %[[AGE1:.*]] = arith.andi %{{.*}}, %[[AGE1_MASK]] : i64
// CHECK: %[[AGE2_NEXT:.*]] = arith.constant {{.*}} 4 : i64
// CHECK: %[[ADVANCED:.*]] = arith.select %{{.*}}, %[[AGE2_NEXT]], %{{.*}} : i64
// CHECK: %[[AGE2_MASK:.*]] = arith.constant {{.*}} 4 : i64
// CHECK: %[[AGE2:.*]] = arith.andi %{{.*}}, %[[AGE2_MASK]] : i64
// CHECK: %[[CONSEQUENT:.*]] = obelisk_sim.assert.sampled_read %arg0 from %arg3
// CHECK: %[[ANTECEDENT:.*]] = obelisk_sim.assert.sampled_read %arg0 from %arg2
// CHECK: %[[START_MASK:.*]] = arith.constant {{.*}} 2 : i64
// CHECK: arith.select %{{.*}}, %[[START_MASK]], %{{.*}} : i64
// CHECK-NOT: obelisk.sv.
