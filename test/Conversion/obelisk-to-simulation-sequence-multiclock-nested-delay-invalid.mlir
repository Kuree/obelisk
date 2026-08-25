// RUN: not obelisk-opt %s '--lower-obelisk-to-sim=opt-level=3' 2>&1 | FileCheck %s

// A single leading-delay bit cannot represent the two waits in
// `(##1 out0) ##1 @(posedge clk1) out1` when the nested first term itself is
// wrapped by another leading ##1. Reject it instead of collapsing two source
// ticks into one.
module attributes {llvm.data_layout = "e-m:e-p:64:64-i64:64-n8:16:32:64-S128", llvm.target_triple = "x86_64-unknown-linux-gnu"} {
  obelisk.sv.symbol.definition attributes {definition_kind = 0 : i32, hierarchical_name = "top", name = "top", node_id = 0 : i64, sym_name = "s0.top"} {}
  obelisk.sv.symbol.root attributes {hierarchical_name = "\\$root ", name = "$root", node_id = 1 : i64, sym_name = "s1.$root"} {
    obelisk.sv.symbol.compilation_unit attributes {hierarchical_name = "$unit", node_id = 2 : i64, sym_name = "s2"} {}
    obelisk.sv.symbol.instance attributes {hierarchical_name = "top", is_uninstantiated = false, name = "top", node_id = 3 : i64, referenced_path = "top", referenced_symbol = @s0.top, sym_name = "s3.top"} {
      obelisk.sv.symbol.instance_body attributes {hierarchical_name = "top", name = "top", node_id = 4 : i64, sym_name = "s4.top"} {
        obelisk.sv.symbol.variable attributes {hierarchical_name = "top.clk0", lifetime = 1 : i32, name = "clk0", node_id = 5 : i64, semantic_type = !obelisk.integral<1, false, true, 0 : 0, logic>, sym_name = "s5.clk0"} {}
        obelisk.sv.symbol.variable attributes {hierarchical_name = "top.clk1", lifetime = 1 : i32, name = "clk1", node_id = 6 : i64, semantic_type = !obelisk.integral<1, false, true, 0 : 0, logic>, sym_name = "s6.clk1"} {}
        obelisk.sv.symbol.variable attributes {hierarchical_name = "top.out0", lifetime = 1 : i32, name = "out0", node_id = 7 : i64, semantic_type = !obelisk.integral<1, false, true, 0 : 0, logic>, sym_name = "s7.out0"} {}
        obelisk.sv.symbol.variable attributes {hierarchical_name = "top.out1", lifetime = 1 : i32, name = "out1", node_id = 8 : i64, semantic_type = !obelisk.integral<1, false, true, 0 : 0, logic>, sym_name = "s8.out1"} {}
        obelisk.sv.symbol.procedural_block attributes {hierarchical_name = "top", node_id = 9 : i64, procedure_kind = 2 : i32, sym_name = "s9", time_precision_fs = 1000000 : i64, time_unit_fs = 1000000 : i64} {
          obelisk.sv.statement.concurrent_assertion attributes {assertion_kind = 0 : i32, has_default_disable = false, has_fail_action = false, has_pass_action = false, node_id = 10 : i64} {
            obelisk.sv.assertion.clocking attributes {node_id = 11 : i64} {
              obelisk.sv.timing.signal_event attributes {edge_kind = 1 : i32, has_iff = false, node_id = 12 : i64} {
                obelisk.sv.expression.named_value attributes {node_id = 13 : i64, referenced_path = "top.clk0", referenced_symbol = @s1.$root::@s3.top::@s4.top::@s5.clk0, semantic_type = !obelisk.integral<1, false, true, 0 : 0, logic>} {}
              }
              obelisk.sv.assertion.sequence_concat attributes {delays = [{is_unbounded = false, max = 1 : i64, min = 1 : i64}, {is_unbounded = false, max = 1 : i64, min = 1 : i64}], node_id = 14 : i64} {
                obelisk.sv.assertion.sequence_concat attributes {delays = [{is_unbounded = false, max = 1 : i64, min = 1 : i64}], node_id = 15 : i64} {
                  obelisk.sv.assertion.simple attributes {has_repetition = false, is_null = false, node_id = 16 : i64, repetition_is_unbounded = false} {
                    obelisk.sv.expression.named_value attributes {node_id = 17 : i64, referenced_path = "top.out0", referenced_symbol = @s1.$root::@s3.top::@s4.top::@s7.out0, semantic_type = !obelisk.integral<1, false, true, 0 : 0, logic>} {}
                  }
                }
                obelisk.sv.assertion.clocking attributes {node_id = 18 : i64} {
                  obelisk.sv.timing.signal_event attributes {edge_kind = 1 : i32, has_iff = false, node_id = 19 : i64} {
                    obelisk.sv.expression.named_value attributes {node_id = 20 : i64, referenced_path = "top.clk1", referenced_symbol = @s1.$root::@s3.top::@s4.top::@s6.clk1, semantic_type = !obelisk.integral<1, false, true, 0 : 0, logic>} {}
                  }
                  obelisk.sv.assertion.simple attributes {has_repetition = false, is_null = false, node_id = 21 : i64, repetition_is_unbounded = false} {
                    obelisk.sv.expression.named_value attributes {node_id = 22 : i64, referenced_path = "top.out1", referenced_symbol = @s1.$root::@s3.top::@s4.top::@s8.out1, semantic_type = !obelisk.integral<1, false, true, 0 : 0, logic>} {}
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

// CHECK: error: bounded AOT sequence compilation failed
