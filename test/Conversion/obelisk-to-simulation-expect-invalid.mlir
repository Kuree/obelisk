// RUN: not obelisk-opt %s '--lower-obelisk-to-sim=opt-level=0' 2>&1 | FileCheck %s

module {
  obelisk.sv.symbol.definition attributes {definition_kind = 0 : i32, hierarchical_name = "expect_invalid", name = "expect_invalid", node_id = 0 : i64, sym_name = "s0.expect_invalid"} {
  }
  obelisk.sv.symbol.root attributes {hierarchical_name = "\\$root ", name = "$root", node_id = 1 : i64, sym_name = "s1.$root"} {
    obelisk.sv.symbol.compilation_unit attributes {hierarchical_name = "$unit", node_id = 2 : i64, sym_name = "s2"} {
    }
    obelisk.sv.symbol.instance attributes {hierarchical_name = "expect_invalid", is_uninstantiated = false, name = "expect_invalid", node_id = 3 : i64, referenced_path = "expect_invalid", referenced_symbol = @s0.expect_invalid, sym_name = "s3.expect_invalid"} {
      obelisk.sv.symbol.instance_body attributes {hierarchical_name = "expect_invalid", name = "expect_invalid", node_id = 4 : i64, sym_name = "s4.expect_invalid"} {
        obelisk.sv.symbol.variable attributes {hierarchical_name = "expect_invalid.enable", lifetime = 1 : i32, name = "enable", node_id = 5 : i64, semantic_type = !obelisk.integral<1, false, true, 0 : 0, logic>, sym_name = "s5.enable"} {
        }
        obelisk.sv.symbol.procedural_block attributes {hierarchical_name = "expect_invalid", node_id = 6 : i64, procedure_kind = 0 : i32, sym_name = "s6", time_precision_fs = 1000000 : i64, time_unit_fs = 1000000 : i64} {
          obelisk.sv.statement.concurrent_assertion attributes {assertion_kind = 5 : i32, has_default_disable = false, has_fail_action = false, has_pass_action = false, node_id = 7 : i64} {
            obelisk.sv.assertion.clocking attributes {node_id = 8 : i64} {
              obelisk.sv.timing.signal_event attributes {edge_kind = 1 : i32, has_iff = false, node_id = 9 : i64} {
                obelisk.sv.expression.named_value attributes {node_id = 10 : i64, referenced_path = "expect_invalid.enable", referenced_symbol = @s1.$root::@s3.expect_invalid::@s4.expect_invalid::@s5.enable, semantic_type = !obelisk.integral<1, false, true, 0 : 0, logic>} {
                }
              }
              obelisk.sv.assertion.binary attributes {node_id = 11 : i64, operator_kind = 11 : i32} {
                obelisk.sv.assertion.simple attributes {has_repetition = false, is_null = false, node_id = 12 : i64, repetition_is_unbounded = false} {
                  obelisk.sv.expression.named_value attributes {node_id = 13 : i64, referenced_path = "expect_invalid.enable", referenced_symbol = @s1.$root::@s3.expect_invalid::@s4.expect_invalid::@s5.enable, semantic_type = !obelisk.integral<1, false, true, 0 : 0, logic>} {
                  }
                }
                obelisk.sv.assertion.simple attributes {has_repetition = false, is_null = false, node_id = 14 : i64, repetition_is_unbounded = false} {
                  obelisk.sv.expression.named_value attributes {node_id = 15 : i64, referenced_path = "expect_invalid.enable", referenced_symbol = @s1.$root::@s3.expect_invalid::@s4.expect_invalid::@s5.enable, semantic_type = !obelisk.integral<1, false, true, 0 : 0, logic>} {
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

// CHECK: expect currently requires one deterministic fixed sequence, optionally with outer first_match, or bounded alternatives without first_match, locals, implication/followed-by, disable iff, or match items
