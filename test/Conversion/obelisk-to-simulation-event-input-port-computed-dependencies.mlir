// RUN: obelisk-opt %s '--lower-obelisk-to-sim=opt-level=0' | FileCheck %s

module {
  obelisk.sv.symbol.definition attributes {definition_kind = 0 : i32, name = "sink", node_id = 52 : i64, sym_name = "sink"} {}
  obelisk.sv.symbol.definition attributes {definition_kind = 0 : i32, name = "leaf", node_id = 0 : i64, sym_name = "leaf"} {}
  obelisk.sv.symbol.definition attributes {definition_kind = 0 : i32, name = "relay2", node_id = 1 : i64, sym_name = "relay2"} {}
  obelisk.sv.symbol.definition attributes {definition_kind = 0 : i32, name = "relay1", node_id = 2 : i64, sym_name = "relay1"} {}
  obelisk.sv.symbol.definition attributes {definition_kind = 0 : i32, name = "top", node_id = 3 : i64, sym_name = "top"} {}
  obelisk.sv.symbol.root attributes {hierarchical_name = "\\$root ", name = "$root", node_id = 4 : i64, sym_name = "root"} {
    obelisk.sv.symbol.instance attributes {hierarchical_name = "top", name = "top", node_id = 5 : i64, referenced_path = "top", referenced_symbol = @top, sym_name = "top_i"} {
      obelisk.sv.symbol.instance_body attributes {hierarchical_name = "top", name = "top", node_id = 6 : i64, sym_name = "top_b", time_precision_fs = 1 : i64, time_unit_fs = 1 : i64} {
        obelisk.sv.symbol.variable attributes {hierarchical_name = "top.select", lifetime = 1 : i32, name = "select", node_id = 7 : i64, semantic_type = !obelisk.integral<1, false, true, 0 : 0, logic>, sym_name = "select"} {}
        obelisk.sv.symbol.variable attributes {hierarchical_name = "top.a", lifetime = 1 : i32, name = "a", node_id = 8 : i64, semantic_type = !obelisk.event, sym_name = "a"} {}
        obelisk.sv.symbol.variable attributes {hierarchical_name = "top.b", lifetime = 1 : i32, name = "b", node_id = 9 : i64, semantic_type = !obelisk.event, sym_name = "b"} {}
        obelisk.sv.symbol.variable attributes {hierarchical_name = "top.hop", lifetime = 1 : i32, name = "hop", node_id = 31 : i64, semantic_type = !obelisk.integral<1, false, true, 0 : 0, logic>, sym_name = "hop"} {}
        obelisk.sv.symbol.procedural_block attributes {hierarchical_name = "top", node_id = 47 : i64, procedure_kind = 2 : i32, sym_name = "ordinary_wait", time_precision_fs = 1 : i64, time_unit_fs = 1 : i64} {
          obelisk.sv.statement.timed attributes {node_id = 48 : i64} {
            obelisk.sv.timing.signal_event attributes {edge_kind = 0 : i32, has_iff = false, node_id = 49 : i64} {
              obelisk.sv.expression.named_value attributes {node_id = 50 : i64, referenced_path = "top.select", referenced_symbol = @root::@top_i::@top_b::@select, semantic_type = !obelisk.integral<1, false, true, 0 : 0, logic>} {}
            }
            obelisk.sv.statement.empty attributes {node_id = 51 : i64} {}
          }
        }
        obelisk.sv.symbol.instance attributes {hierarchical_name = "top.r1", name = "r1", node_id = 10 : i64, referenced_path = "relay1", referenced_symbol = @relay1, sym_name = "r1"} {
          obelisk.sv.port.connection attributes {actual_is_constant = false, direction = 0 : i32, formal_name = "sel", formal_ordinal = 0 : i64, formal_path = "top.r1.sel", formal_symbol = @root::@top_i::@top_b::@r1::@relay1_b::@sel_p, formal_type = !obelisk.integral<1, false, true, 0 : 0, logic>, internal_path = "top.r1.sel", internal_symbol = @root::@top_i::@top_b::@r1::@relay1_b::@sel_v, is_ansi = true, is_net = false, node_id = 11 : i64, provenance = 0 : i32} {
          } {
            obelisk.sv.expression.named_value attributes {node_id = 12 : i64, referenced_path = "top.select", referenced_symbol = @root::@top_i::@top_b::@select, semantic_type = !obelisk.integral<1, false, true, 0 : 0, logic>} {}
          }
          obelisk.sv.port.connection attributes {actual_is_constant = false, direction = 1 : i32, formal_name = "out", formal_ordinal = 1 : i64, formal_path = "top.r1.out", formal_symbol = @root::@top_i::@top_b::@r1::@relay1_b::@out_p, formal_type = !obelisk.integral<1, false, true, 0 : 0, logic>, internal_path = "top.r1.out", internal_symbol = @root::@top_i::@top_b::@r1::@relay1_b::@out_v, is_ansi = true, is_net = false, node_id = 32 : i64, provenance = 0 : i32} {
          } {
            obelisk.sv.expression.assignment attributes {assignment_kind = 0 : i32, node_id = 33 : i64, semantic_type = !obelisk.integral<1, false, true, 0 : 0, logic>} {
              obelisk.sv.expression.named_value attributes {node_id = 34 : i64, referenced_path = "top.hop", referenced_symbol = @root::@top_i::@top_b::@hop, semantic_type = !obelisk.integral<1, false, true, 0 : 0, logic>} {}
              obelisk.sv.expression.empty_argument attributes {node_id = 35 : i64, semantic_type = !obelisk.integral<1, false, true, 0 : 0, logic>} {}
            }
          }
          obelisk.sv.symbol.instance_body attributes {hierarchical_name = "top.r1", name = "relay1", node_id = 13 : i64, sym_name = "relay1_b", time_precision_fs = 1 : i64, time_unit_fs = 1 : i64} {
            obelisk.sv.symbol.port attributes {direction = 0 : i32, hierarchical_name = "top.r1.sel", name = "sel", node_id = 14 : i64, semantic_type = !obelisk.integral<1, false, true, 0 : 0, logic>, sym_name = "sel_p"} {}
            obelisk.sv.symbol.variable attributes {hierarchical_name = "top.r1.sel", lifetime = 1 : i32, name = "sel", node_id = 15 : i64, semantic_type = !obelisk.integral<1, false, true, 0 : 0, logic>, sym_name = "sel_v"} {}
            obelisk.sv.symbol.port attributes {direction = 1 : i32, hierarchical_name = "top.r1.out", name = "out", node_id = 36 : i64, semantic_type = !obelisk.integral<1, false, true, 0 : 0, logic>, sym_name = "out_p"} {}
            obelisk.sv.symbol.variable attributes {hierarchical_name = "top.r1.out", lifetime = 1 : i32, name = "out", node_id = 37 : i64, semantic_type = !obelisk.integral<1, false, true, 0 : 0, logic>, sym_name = "out_v"} {}
            obelisk.sv.symbol.continuous_assign attributes {hierarchical_name = "top.r1", node_id = 38 : i64, sym_name = "assign", time_precision_fs = 1 : i64, time_unit_fs = 1 : i64} {
              obelisk.sv.expression.assignment attributes {assignment_kind = 0 : i32, node_id = 39 : i64, semantic_type = !obelisk.integral<1, false, true, 0 : 0, logic>} {
                obelisk.sv.expression.named_value attributes {node_id = 40 : i64, referenced_path = "top.r1.out", referenced_symbol = @root::@top_i::@top_b::@r1::@relay1_b::@out_v, semantic_type = !obelisk.integral<1, false, true, 0 : 0, logic>} {}
                obelisk.sv.expression.named_value attributes {node_id = 41 : i64, referenced_path = "top.r1.sel", referenced_symbol = @root::@top_i::@top_b::@r1::@relay1_b::@sel_v, semantic_type = !obelisk.integral<1, false, true, 0 : 0, logic>} {}
              }
            }
            obelisk.sv.symbol.instance attributes {hierarchical_name = "top.r1.r2", name = "r2", node_id = 16 : i64, referenced_path = "relay2", referenced_symbol = @relay2, sym_name = "r2"} {
              obelisk.sv.port.connection attributes {actual_is_constant = false, direction = 0 : i32, formal_name = "sel", formal_ordinal = 0 : i64, formal_path = "top.r1.r2.sel", formal_symbol = @root::@top_i::@top_b::@r1::@relay1_b::@r2::@relay2_b::@sel_p, formal_type = !obelisk.integral<1, false, true, 0 : 0, logic>, internal_path = "top.r1.r2.sel", internal_symbol = @root::@top_i::@top_b::@r1::@relay1_b::@r2::@relay2_b::@sel_v, is_ansi = true, is_net = false, node_id = 17 : i64, provenance = 0 : i32} {
              } {
                obelisk.sv.expression.named_value attributes {node_id = 18 : i64, referenced_path = "top.hop", referenced_symbol = @root::@top_i::@top_b::@hop, semantic_type = !obelisk.integral<1, false, true, 0 : 0, logic>} {}
              }
              obelisk.sv.symbol.instance_body attributes {hierarchical_name = "top.r1.r2", name = "relay2", node_id = 19 : i64, sym_name = "relay2_b", time_precision_fs = 1 : i64, time_unit_fs = 1 : i64} {
                obelisk.sv.symbol.port attributes {direction = 0 : i32, hierarchical_name = "top.r1.r2.sel", name = "sel", node_id = 20 : i64, semantic_type = !obelisk.integral<1, false, true, 0 : 0, logic>, sym_name = "sel_p"} {}
                obelisk.sv.symbol.variable attributes {hierarchical_name = "top.r1.r2.sel", lifetime = 1 : i32, name = "sel", node_id = 21 : i64, semantic_type = !obelisk.integral<1, false, true, 0 : 0, logic>, sym_name = "sel_v"} {}
                obelisk.sv.symbol.instance attributes {hierarchical_name = "top.r1.r2.l", name = "l", node_id = 22 : i64, referenced_path = "leaf", referenced_symbol = @leaf, sym_name = "l"} {
                  obelisk.sv.port.connection attributes {actual_is_constant = false, direction = 0 : i32, formal_name = "wake", formal_ordinal = 0 : i64, formal_path = "top.r1.r2.l.wake", formal_symbol = @root::@top_i::@top_b::@r1::@relay1_b::@r2::@relay2_b::@l::@leaf_b::@wake_p, formal_type = !obelisk.event, internal_path = "top.r1.r2.l.wake", internal_symbol = @root::@top_i::@top_b::@r1::@relay1_b::@r2::@relay2_b::@l::@leaf_b::@wake_v, is_ansi = true, is_net = false, node_id = 23 : i64, provenance = 0 : i32} {
                  } {
                    obelisk.sv.expression.conditional_op attributes {condition_count = 1 : i64, condition_pattern_flags = array<i64: 0>, node_id = 24 : i64, semantic_type = !obelisk.event} {
                      obelisk.sv.expression.named_value attributes {node_id = 25 : i64, referenced_path = "top.r1.r2.sel", referenced_symbol = @root::@top_i::@top_b::@r1::@relay1_b::@r2::@relay2_b::@sel_v, semantic_type = !obelisk.integral<1, false, true, 0 : 0, logic>} {}
                      obelisk.sv.expression.named_value attributes {node_id = 26 : i64, referenced_path = "top.a", referenced_symbol = @root::@top_i::@top_b::@a, semantic_type = !obelisk.event} {}
                      obelisk.sv.expression.named_value attributes {node_id = 27 : i64, referenced_path = "top.b", referenced_symbol = @root::@top_i::@top_b::@b, semantic_type = !obelisk.event} {}
                    }
                  }
                  obelisk.sv.symbol.instance_body attributes {hierarchical_name = "top.r1.r2.l", name = "leaf", node_id = 28 : i64, sym_name = "leaf_b", time_precision_fs = 1 : i64, time_unit_fs = 1 : i64} {
                    obelisk.sv.symbol.port attributes {direction = 0 : i32, hierarchical_name = "top.r1.r2.l.wake", name = "wake", node_id = 29 : i64, semantic_type = !obelisk.event, sym_name = "wake_p"} {}
                    obelisk.sv.symbol.variable attributes {hierarchical_name = "top.r1.r2.l.wake", lifetime = 1 : i32, name = "wake", node_id = 30 : i64, semantic_type = !obelisk.event, sym_name = "wake_v"} {}
                    obelisk.sv.symbol.instance attributes {hierarchical_name = "top.r1.r2.l.s", name = "s", node_id = 53 : i64, referenced_path = "sink", referenced_symbol = @sink, sym_name = "s"} {
                      obelisk.sv.port.connection attributes {actual_is_constant = false, direction = 0 : i32, formal_name = "wake", formal_ordinal = 0 : i64, formal_path = "top.r1.r2.l.s.wake", formal_symbol = @root::@top_i::@top_b::@r1::@relay1_b::@r2::@relay2_b::@l::@leaf_b::@s::@sink_b::@wake_p, formal_type = !obelisk.event, internal_path = "top.r1.r2.l.s.wake", internal_symbol = @root::@top_i::@top_b::@r1::@relay1_b::@r2::@relay2_b::@l::@leaf_b::@s::@sink_b::@wake_v, is_ansi = true, is_net = false, node_id = 54 : i64, provenance = 0 : i32} {
                      } {
                        obelisk.sv.expression.named_value attributes {node_id = 55 : i64, referenced_path = "top.r1.r2.l.wake", referenced_symbol = @root::@top_i::@top_b::@r1::@relay1_b::@r2::@relay2_b::@l::@leaf_b::@wake_v, semantic_type = !obelisk.event} {}
                      }
                      obelisk.sv.symbol.instance_body attributes {hierarchical_name = "top.r1.r2.l.s", name = "sink", node_id = 56 : i64, sym_name = "sink_b", time_precision_fs = 1 : i64, time_unit_fs = 1 : i64} {
                        obelisk.sv.symbol.port attributes {direction = 0 : i32, hierarchical_name = "top.r1.r2.l.s.wake", name = "wake", node_id = 57 : i64, semantic_type = !obelisk.event, sym_name = "wake_p"} {}
                        obelisk.sv.symbol.variable attributes {hierarchical_name = "top.r1.r2.l.s.wake", lifetime = 1 : i32, name = "wake", node_id = 58 : i64, semantic_type = !obelisk.event, sym_name = "wake_v"} {}
                        obelisk.sv.symbol.variable attributes {hierarchical_name = "top.r1.r2.l.s.local", lifetime = 1 : i32, name = "local", node_id = 59 : i64, semantic_type = !obelisk.event, sym_name = "local"} {}
                        obelisk.sv.symbol.procedural_block attributes {hierarchical_name = "top.r1.r2.l.s", node_id = 60 : i64, procedure_kind = 0 : i32, sym_name = "write", time_precision_fs = 1 : i64, time_unit_fs = 1 : i64} {
                          obelisk.sv.statement.expression_statement attributes {node_id = 61 : i64} {
                            obelisk.sv.expression.assignment attributes {assignment_kind = 0 : i32, node_id = 62 : i64, semantic_type = !obelisk.event} {
                              obelisk.sv.expression.named_value attributes {node_id = 63 : i64, referenced_path = "top.r1.r2.l.s.wake", referenced_symbol = @root::@top_i::@top_b::@r1::@relay1_b::@r2::@relay2_b::@l::@leaf_b::@s::@sink_b::@wake_v, semantic_type = !obelisk.event} {}
                              obelisk.sv.expression.named_value attributes {node_id = 64 : i64, referenced_path = "top.r1.r2.l.s.local", referenced_symbol = @root::@top_i::@top_b::@r1::@relay1_b::@r2::@relay2_b::@l::@leaf_b::@s::@sink_b::@local, semantic_type = !obelisk.event} {}
                            }
                          }
                        }
                        obelisk.sv.symbol.procedural_block attributes {hierarchical_name = "top.r1.r2.l.s", node_id = 65 : i64, procedure_kind = 2 : i32, sym_name = "wait", time_precision_fs = 1 : i64, time_unit_fs = 1 : i64} {
                          obelisk.sv.statement.timed attributes {node_id = 66 : i64} {
                            obelisk.sv.timing.signal_event attributes {edge_kind = 0 : i32, has_iff = false, node_id = 67 : i64} {
                              obelisk.sv.expression.named_value attributes {node_id = 68 : i64, referenced_path = "top.r1.r2.l.s.wake", referenced_symbol = @root::@top_i::@top_b::@r1::@relay1_b::@r2::@relay2_b::@l::@leaf_b::@s::@sink_b::@wake_v, semantic_type = !obelisk.event} {}
                            }
                            obelisk.sv.statement.empty attributes {node_id = 69 : i64} {}
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
}

// CHECK-LABEL: obelisk_sim.func @__obelisk_root
// CHECK: obelisk_sim.spawn @unit_0
// CHECK: obelisk_sim.spawn @unit_4
// CHECK: obelisk_sim.spawn @unit_1
// CHECK: obelisk_sim.spawn @unit_5
// CHECK: obelisk_sim.spawn @unit_6
// CHECK: obelisk_sim.spawn @unit_7
// CHECK: obelisk_sim.spawn @unit_8
// CHECK: obelisk_sim.spawn @unit_3
// CHECK: obelisk_sim.func private @unit_1
// CHECK-SAME: schedule.computed_event_startup
// CHECK: obelisk_sim.func private @unit_4
// CHECK-SAME: schedule.computed_event_startup
// CHECK: obelisk_sim.func private @unit_5
// CHECK-SAME: schedule.computed_event_startup
// CHECK: obelisk_sim.func private @unit_6
// CHECK-SAME: schedule.computed_event_startup
// CHECK: obelisk_sim.func private @unit_7
// CHECK-SAME: schedule.computed_event_startup
// CHECK: obelisk_sim.func private @unit_8
// CHECK-SAME: schedule.computed_event_startup
// CHECK-NOT: obelisk.sv.
