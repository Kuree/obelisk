// RUN: not obelisk-opt %s '--lower-obelisk-to-sim=opt-level=0' -o /dev/null 2>&1 | FileCheck %s

// CHECK: error: SVA property operator 'accept_on' currently requires one outermost abort around a deterministic bounded property or supported aggregate persistent property without locals, match items, implication/followed-by, disable iff, first_match, expect, or cover-sequence per-match accounting

module {
  obelisk.sv.symbol.definition attributes {definition_kind = 0 : i32, hierarchical_name = "top", name = "top", node_id = 0 : i64, sym_name = "s0.top"} {
  }
  obelisk.sv.symbol.root attributes {hierarchical_name = "\\$root ", name = "$root", node_id = 1 : i64, sym_name = "s1.$root"} {
    obelisk.sv.symbol.compilation_unit attributes {hierarchical_name = "$unit", node_id = 2 : i64, sym_name = "s2"} {
    }
    obelisk.sv.symbol.instance attributes {hierarchical_name = "top", is_uninstantiated = false, name = "top", node_id = 3 : i64, referenced_path = "top", referenced_symbol = @s0.top, sym_name = "s3.top"} {
      obelisk.sv.symbol.instance_body attributes {hierarchical_name = "top", name = "top", node_id = 4 : i64, sym_name = "s4.top"} {
        obelisk.sv.symbol.variable attributes {hierarchical_name = "top.clk", lifetime = 1 : i32, name = "clk", node_id = 5 : i64, semantic_type = !obelisk.integral<1, false, true, 0 : 0, logic>, sym_name = "s5.clk"} {
        }
        obelisk.sv.symbol.variable attributes {hierarchical_name = "top.a", lifetime = 1 : i32, name = "a", node_id = 6 : i64, semantic_type = !obelisk.integral<1, false, true, 0 : 0, logic>, sym_name = "s6.a"} {
        }
        obelisk.sv.symbol.variable attributes {hierarchical_name = "top.reset", lifetime = 1 : i32, name = "reset", node_id = 7 : i64, semantic_type = !obelisk.integral<1, false, true, 0 : 0, logic>, sym_name = "s7.reset"} {
        }
        obelisk.sv.symbol.procedural_block attributes {hierarchical_name = "top", node_id = 8 : i64, procedure_kind = 2 : i32, sym_name = "s8", time_precision_fs = 1000000 : i64, time_unit_fs = 1000000 : i64} {
          obelisk.sv.statement.concurrent_assertion attributes {assertion_kind = 0 : i32, has_default_disable = false, has_fail_action = false, has_pass_action = false, node_id = 9 : i64} {
            obelisk.sv.assertion.clocking attributes {node_id = 10 : i64} {
              obelisk.sv.timing.signal_event attributes {edge_kind = 1 : i32, has_iff = false, node_id = 11 : i64} {
                obelisk.sv.expression.named_value attributes {node_id = 12 : i64, referenced_path = "top.clk", referenced_symbol = @s1.$root::@s3.top::@s4.top::@s5.clk, semantic_type = !obelisk.integral<1, false, true, 0 : 0, logic>} {
                }
              }
              obelisk.sv.assertion.abort attributes {action = 0 : i32, is_synchronous = false, node_id = 13 : i64} {
                obelisk.sv.expression.named_value attributes {node_id = 14 : i64, referenced_path = "top.reset", referenced_symbol = @s1.$root::@s3.top::@s4.top::@s7.reset, semantic_type = !obelisk.integral<1, false, true, 0 : 0, logic>} {
                }
                obelisk.sv.assertion.first_match attributes {match_item_count = 0 : i64, node_id = 15 : i64} {
                  obelisk.sv.assertion.simple attributes {has_repetition = false, is_null = false, node_id = 16 : i64, repetition_is_unbounded = false} {
                    obelisk.sv.expression.named_value attributes {node_id = 17 : i64, referenced_path = "top.a", referenced_symbol = @s1.$root::@s3.top::@s4.top::@s6.a, semantic_type = !obelisk.integral<1, false, true, 0 : 0, logic>} {
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
