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
        obelisk.sv.symbol.sequence attributes {has_default_instance = true, hierarchical_name = "t.seq", name = "seq", node_id = 8 : i64, port_count = 0 : i64, port_paths = [], port_symbols = [], sym_name = "s8.seq"} {
          obelisk.sv.expression.assertion_instance attributes {argument_count = 0 : i64, argument_formal_paths = [], argument_formal_symbols = [], argument_kinds = array<i64>, has_expanded_body = true, is_recursive_property = false, local_variable_count = 0 : i64, local_variable_has_initializer = array<i64>, local_variable_paths = [], local_variable_symbols = [], node_id = 9 : i64, referenced_path = "t.seq", referenced_symbol = @s1.$root::@s3.t::@s4.t::@s8.seq, semantic_type = !obelisk.sequence} {
            obelisk.sv.assertion.clocking attributes {node_id = 10 : i64} {
              obelisk.sv.timing.signal_event attributes {edge_kind = 1 : i32, has_iff = false, node_id = 11 : i64} {
                obelisk.sv.expression.named_value attributes {node_id = 12 : i64, referenced_path = "t.clk", referenced_symbol = @s1.$root::@s3.t::@s4.t::@s5.clk, semantic_type = !obelisk.integral<1, false, true, 0 : 0, logic>} {
                }
              }
              obelisk.sv.assertion.sequence_concat attributes {delays = [{is_unbounded = false, max = 0 : i64, min = 0 : i64}, {is_unbounded = false, max = 1 : i64, min = 1 : i64}], node_id = 13 : i64} {
                obelisk.sv.assertion.simple attributes {has_repetition = false, is_null = false, node_id = 14 : i64, repetition_is_unbounded = false} {
                  obelisk.sv.expression.named_value attributes {node_id = 15 : i64, referenced_path = "t.a", referenced_symbol = @s1.$root::@s3.t::@s4.t::@s6.a, semantic_type = !obelisk.integral<1, false, true, 0 : 0, logic>} {
                  }
                }
                obelisk.sv.assertion.simple attributes {has_repetition = false, is_null = false, node_id = 16 : i64, repetition_is_unbounded = false} {
                  obelisk.sv.expression.named_value attributes {node_id = 17 : i64, referenced_path = "t.b", referenced_symbol = @s1.$root::@s3.t::@s4.t::@s7.b, semantic_type = !obelisk.integral<1, false, true, 0 : 0, logic>} {
                  }
                }
              }
            }
          }
        }
        obelisk.sv.symbol.procedural_block attributes {hierarchical_name = "t", node_id = 18 : i64, procedure_kind = 0 : i32, sym_name = "s9", time_precision_fs = 1000000 : i64, time_unit_fs = 1000000 : i64} {
          obelisk.sv.statement.timed attributes {node_id = 19 : i64} {
            obelisk.sv.timing.signal_event attributes {edge_kind = 0 : i32, has_iff = false, node_id = 20 : i64} {
              obelisk.sv.expression.assertion_instance attributes {argument_count = 0 : i64, argument_formal_paths = [], argument_formal_symbols = [], argument_kinds = array<i64>, has_expanded_body = true, is_recursive_property = false, local_variable_count = 0 : i64, local_variable_has_initializer = array<i64>, local_variable_paths = [], local_variable_symbols = [], node_id = 21 : i64, referenced_path = "t.seq", referenced_symbol = @s1.$root::@s3.t::@s4.t::@s8.seq, semantic_type = !obelisk.sequence} {
                obelisk.sv.assertion.clocking attributes {node_id = 22 : i64} {
                  obelisk.sv.timing.signal_event attributes {edge_kind = 1 : i32, has_iff = false, node_id = 23 : i64} {
                    obelisk.sv.expression.named_value attributes {node_id = 24 : i64, referenced_path = "t.clk", referenced_symbol = @s1.$root::@s3.t::@s4.t::@s5.clk, semantic_type = !obelisk.integral<1, false, true, 0 : 0, logic>} {
                    }
                  }
                  obelisk.sv.assertion.sequence_concat attributes {delays = [{is_unbounded = false, max = 0 : i64, min = 0 : i64}, {is_unbounded = false, max = 1 : i64, min = 1 : i64}], node_id = 25 : i64} {
                    obelisk.sv.assertion.simple attributes {has_repetition = false, is_null = false, node_id = 26 : i64, repetition_is_unbounded = false} {
                      obelisk.sv.expression.named_value attributes {node_id = 27 : i64, referenced_path = "t.a", referenced_symbol = @s1.$root::@s3.t::@s4.t::@s6.a, semantic_type = !obelisk.integral<1, false, true, 0 : 0, logic>} {
                      }
                    }
                    obelisk.sv.assertion.simple attributes {has_repetition = false, is_null = false, node_id = 28 : i64, repetition_is_unbounded = false} {
                      obelisk.sv.expression.named_value attributes {node_id = 29 : i64, referenced_path = "t.b", referenced_symbol = @s1.$root::@s3.t::@s4.t::@s7.b, semantic_type = !obelisk.integral<1, false, true, 0 : 0, logic>} {
                      }
                    }
                  }
                }
              }
            }
            obelisk.sv.statement.empty attributes {node_id = 30 : i64} {
            }
          }
        }
      }
    }
  }
}

// The declaration becomes one time-zero monitor in Observed. It tracks every
// overlapping attempt and triggers a shared static endpoint event on success.
// CHECK-LABEL: obelisk_sim.func private @unit_0(
// CHECK-SAME: [[ENDPOINT:%arg[0-9]+]]: !obelisk_sim.event
// CHECK-SAME: home_region = 8 : i32
// CHECK: obelisk_sim.suspend.edge posedge
// CHECK-SAME: resume_region = 8 : i32
// CHECK: obelisk_sim.event.trigger [[ENDPOINT]] nonblocking = false

// The procedural control waits on that endpoint, independently of sequence
// start time, and resumes in Reactive after endpoint detection in Observed.
// CHECK-LABEL: obelisk_sim.func private @unit_1(
// CHECK-SAME: [[WAIT_ENDPOINT:%arg[0-9]+]]: !obelisk_sim.event
// CHECK: obelisk_sim.suspend.event [[WAIT_ENDPOINT]]
// CHECK-SAME: resume_region = 10 : i32
// CHECK-NOT: obelisk.sv.
