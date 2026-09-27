// RUN: obelisk-opt %s '--lower-obelisk-to-sim=opt-level=0' | FileCheck %s

// IEEE 1800-2017 9.4.2 makes an event control wait on a value change of the
// expression it names, and 14.16 gives a clocking-block output its own driver
// onto the net. A process that both drives `w` through `cb.w` and waits for
// `@(posedge w)` therefore resolves `w` two ways: the driver it writes and the
// net whose resolved value the edge is detected on. The wait has to watch the
// net -- a driver handle carries only this process's contribution, which no
// edge can be read from.

module {
  obelisk.sv.symbol.definition attributes {definition_kind = 0 : i32, hierarchical_name = "t", name = "t", node_id = 0 : i64, sym_name = "s0.t"} {
  }
  obelisk.sv.symbol.root attributes {hierarchical_name = "\\$root ", name = "$root", node_id = 1 : i64, sym_name = "s1.$root"} {
    obelisk.sv.symbol.compilation_unit attributes {hierarchical_name = "$unit", node_id = 2 : i64, sym_name = "s2"} {
    }
    obelisk.sv.symbol.instance attributes {hierarchical_name = "t", is_uninstantiated = false, name = "t", node_id = 3 : i64, referenced_path = "t", referenced_symbol = @s0.t, sym_name = "s3.t"} {
      obelisk.sv.symbol.instance_body attributes {hierarchical_name = "t", name = "t", node_id = 4 : i64, sym_name = "s4.t", time_precision_fs = 1000000 : i64, time_unit_fs = 1000000 : i64} {
        obelisk.sv.symbol.net attributes {hierarchical_name = "t.w", is_implicit = false, name = "w", net_kind = 1 : i32, node_id = 5 : i64, semantic_type = !obelisk.integral<1, false, true, 0 : 0, logic>, sym_name = "s5.w"} {
        }
        obelisk.sv.symbol.variable attributes {hierarchical_name = "t.clk", lifetime = 1 : i32, name = "clk", node_id = 6 : i64, semantic_type = !obelisk.integral<1, false, true, 0 : 0, logic>, sym_name = "s6.clk"} {
        }
        obelisk.sv.symbol.clocking_block attributes {hierarchical_name = "t.cb", is_default = false, is_global = false, name = "cb", node_id = 7 : i64, sym_name = "s7.cb"} {
          obelisk.sv.timing.signal_event attributes {edge_kind = 1 : i32, has_iff = false, node_id = 8 : i64} {
            obelisk.sv.expression.named_value attributes {is_signed = false, node_id = 9 : i64, referenced_path = "t.clk", referenced_symbol = @s1.$root::@s3.t::@s4.t::@s6.clk, semantic_type = !obelisk.integral<1, false, true, 0 : 0, logic>} {
            }
          }
          obelisk.sv.symbol.clock_var attributes {direction = 2 : i32, has_input_delay = false, has_output_delay = false, hierarchical_name = "t.cb.w", input_edge = 0 : i32, lifetime = 1 : i32, name = "w", node_id = 10 : i64, output_edge = 0 : i32, semantic_type = !obelisk.integral<1, false, true, 0 : 0, logic>, sym_name = "s8.w"} {
            obelisk.sv.expression.named_value attributes {is_signed = false, node_id = 11 : i64, referenced_path = "t.w", referenced_symbol = @s1.$root::@s3.t::@s4.t::@s5.w, semantic_type = !obelisk.integral<1, false, true, 0 : 0, logic>} {
            }
          }
        }
        obelisk.sv.symbol.procedural_block attributes {hierarchical_name = "t", node_id = 12 : i64, procedure_kind = 0 : i32, sym_name = "s9", time_precision_fs = 1000000 : i64, time_unit_fs = 1000000 : i64} {
          obelisk.sv.statement.block attributes {node_id = 13 : i64} {
            obelisk.sv.statement.list attributes {node_id = 14 : i64} {
              obelisk.sv.statement.expression_statement attributes {node_id = 15 : i64} {
                obelisk.sv.expression.assignment attributes {assignment_kind = 1 : i32, is_signed = false, node_id = 16 : i64, semantic_type = !obelisk.integral<1, false, true, 0 : 0, logic>} {
                  obelisk.sv.expression.named_value attributes {clocking_access_direction = 2 : i32, clocking_event_edge = 1 : i32, clocking_event_path = "t.clk", clocking_event_symbol = @s1.$root::@s3.t::@s4.t::@s6.clk, clocking_input_skew_edge = 0 : i32, clocking_input_skew_one_step, clocking_output_skew_delay = "0", clocking_output_skew_edge = 0 : i32, clocking_source_path = "t.w", clocking_source_symbol = @s1.$root::@s3.t::@s4.t::@s5.w, clocking_time_precision_fs = 1000000 : i64, clocking_time_unit_fs = 1000000 : i64, clocking_variable, is_signed = false, node_id = 17 : i64, referenced_path = "t.cb.w", referenced_symbol = @s1.$root::@s3.t::@s4.t::@s7.cb::@s8.w, semantic_type = !obelisk.integral<1, false, true, 0 : 0, logic>} {
                  }
                  obelisk.sv.expression.conversion attributes {folded_constant = "1'b1", is_signed = false, node_id = 18 : i64, semantic_type = !obelisk.integral<1, false, true, 0 : 0, logic>} {
                    obelisk.sv.expression.integer_literal attributes {constant_value = "1'b1", is_signed = false, node_id = 19 : i64, semantic_type = !obelisk.ranged_packed_array<0 : 0 x !obelisk.integral<1, false, false, 0 : 0, bit>>} {
                    }
                  }
                }
              }
              obelisk.sv.statement.timed attributes {node_id = 20 : i64} {
                obelisk.sv.timing.signal_event attributes {edge_kind = 1 : i32, has_iff = false, node_id = 21 : i64} {
                  obelisk.sv.expression.named_value attributes {is_signed = false, node_id = 22 : i64, referenced_path = "t.w", referenced_symbol = @s1.$root::@s3.t::@s4.t::@s5.w, semantic_type = !obelisk.integral<1, false, true, 0 : 0, logic>} {
                  }
                }
                obelisk.sv.statement.empty attributes {node_id = 23 : i64} {
                }
              }
            }
          }
        }
      }
    }
  }
}


// CHECK: simulation.func private @unit_0(
// CHECK-DAG: %[[NET:[^:]*]]: !simulation.net<!simulation.logic<1>>
// CHECK-DAG: !simulation.driver<!simulation.logic<1>>
// CHECK: simulation.suspend.edge posedge %[[NET]]
