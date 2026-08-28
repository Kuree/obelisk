// RUN: obelisk-opt %s '--lower-obelisk-to-sim=opt-level=0' | FileCheck %s
// RUN: obelisk-opt %s '--lower-obelisk-to-sim=opt-level=3' '--encode-obelisk-sim-to-bytecode=vpi=off' -o /dev/null

// Two virtual handles have the same interface type and therefore the same
// static member symbol. Clock identity must include the receiver: va.clk to
// vb.clk is a real ##1 handoff. The same must hold for va.cb versus vb.cb even
// if a frontend retains the clocking block's static event symbol on both
// members. In contrast, va.cb and va.clk denote the same selected clock and
// remain on the compact single-clock monitor path.
module attributes {llvm.data_layout = "e-m:e-p:64:64-i64:64-n8:16:32:64-S128", llvm.target_triple = "x86_64-unknown-linux-gnu"} {
  obelisk.sv.symbol.definition attributes {definition_kind = 1 : i32, hierarchical_name = "bus_if", name = "bus_if", node_id = 0 : i64, sym_name = "s0.bus_if"} {}
  obelisk.sv.symbol.definition attributes {definition_kind = 0 : i32, hierarchical_name = "top", name = "top", node_id = 1 : i64, sym_name = "s1.top"} {}
  obelisk.sv.symbol.root attributes {hierarchical_name = "\\$root ", name = "$root", node_id = 2 : i64, sym_name = "s2.$root"} {
    obelisk.sv.symbol.compilation_unit attributes {hierarchical_name = "$unit", node_id = 3 : i64, sym_name = "s3"} {}
    obelisk.sv.symbol.instance attributes {hierarchical_name = "top", is_uninstantiated = false, name = "top", node_id = 4 : i64, referenced_path = "top", referenced_symbol = @s1.top, sym_name = "s4.top"} {
      obelisk.sv.symbol.instance_body attributes {hierarchical_name = "top", name = "top", node_id = 5 : i64, sym_name = "s5.top", time_precision_fs = 1000000 : i64, time_unit_fs = 1000000 : i64} {
        obelisk.sv.symbol.instance attributes {hierarchical_name = "top.a_if", is_uninstantiated = false, name = "a_if", node_id = 6 : i64, referenced_path = "bus_if", referenced_symbol = @s0.bus_if, sym_name = "s6.a_if"} {
          obelisk.sv.symbol.instance_body attributes {hierarchical_name = "top.a_if", name = "bus_if", node_id = 7 : i64, sym_name = "s7.bus_if", time_precision_fs = 1000000 : i64, time_unit_fs = 1000000 : i64, virtual_interface_identity = @s2.$root::@s5.top::@s12.bus_if} {
            obelisk.sv.symbol.variable attributes {hierarchical_name = "top.a_if.clk", lifetime = 1 : i32, name = "clk", node_id = 8 : i64, semantic_type = !obelisk.integral<1, false, true, 0 : 0, logic>, sym_name = "s8.clk"} {}
          }
        }
        obelisk.sv.symbol.instance attributes {hierarchical_name = "top.b_if", is_uninstantiated = false, name = "b_if", node_id = 9 : i64, referenced_path = "bus_if", referenced_symbol = @s0.bus_if, sym_name = "s9.b_if"} {
          obelisk.sv.symbol.instance_body attributes {hierarchical_name = "top.b_if", name = "bus_if", node_id = 10 : i64, sym_name = "s10.bus_if", time_precision_fs = 1000000 : i64, time_unit_fs = 1000000 : i64, virtual_interface_identity = @s2.$root::@s5.top::@s12.bus_if} {
            obelisk.sv.symbol.variable attributes {hierarchical_name = "top.b_if.clk", lifetime = 1 : i32, name = "clk", node_id = 11 : i64, semantic_type = !obelisk.integral<1, false, true, 0 : 0, logic>, sym_name = "s11.clk"} {}
          }
        }
        obelisk.sv.symbol.variable attributes {hierarchical_name = "top.va", lifetime = 1 : i32, name = "va", node_id = 12 : i64, semantic_type = !obelisk.virtual_interface<@s2.$root::@s5.top::@s12.bus_if, "">, sym_name = "s13.va"} {}
        obelisk.sv.symbol.variable attributes {hierarchical_name = "top.vb", lifetime = 1 : i32, name = "vb", node_id = 13 : i64, semantic_type = !obelisk.virtual_interface<@s2.$root::@s5.top::@s12.bus_if, "">, sym_name = "s14.vb"} {}
        obelisk.sv.symbol.variable attributes {hierarchical_name = "top.a", lifetime = 1 : i32, name = "a", node_id = 14 : i64, semantic_type = !obelisk.integral<1, false, true, 0 : 0, logic>, sym_name = "s15.a"} {}
        obelisk.sv.symbol.variable attributes {hierarchical_name = "top.b", lifetime = 1 : i32, name = "b", node_id = 15 : i64, semantic_type = !obelisk.integral<1, false, true, 0 : 0, logic>, sym_name = "s16.b"} {}
        obelisk.sv.symbol.sequence attributes {has_default_instance = false, hierarchical_name = "top.inner", name = "inner", node_id = 16 : i64, port_count = 1 : i64, port_paths = ["top.inner.c"], port_symbols = [@s2.$root::@s4.top::@s5.top::@s17.inner::@s18.c], sym_name = "s17.inner"} {
          obelisk.sv.symbol.assertion_port attributes {has_default_value = false, hierarchical_name = "top.inner.c", is_local_variable = false, name = "c", node_id = 17 : i64, semantic_type = !obelisk.event, sym_name = "s18.c"} {}
        }
        obelisk.sv.symbol.property attributes {has_default_instance = false, hierarchical_name = "top.outer", name = "outer", node_id = 18 : i64, port_count = 1 : i64, port_paths = ["top.outer.s"], port_symbols = [@s2.$root::@s4.top::@s5.top::@s19.outer::@s20.s], sym_name = "s19.outer"} {
          obelisk.sv.symbol.assertion_port attributes {has_default_value = false, hierarchical_name = "top.outer.s", is_local_variable = false, name = "s", node_id = 19 : i64, semantic_type = !obelisk.sequence, sym_name = "s20.s"} {}
        }

        // Different dynamic receivers: detached two-stage handoff.
        obelisk.sv.symbol.procedural_block attributes {hierarchical_name = "top", node_id = 20 : i64, procedure_kind = 2 : i32, sym_name = "s20", time_precision_fs = 1000000 : i64, time_unit_fs = 1000000 : i64} {
          obelisk.sv.statement.concurrent_assertion attributes {assertion_kind = 0 : i32, has_default_disable = false, has_fail_action = false, has_pass_action = false, node_id = 21 : i64} {
            obelisk.sv.assertion.clocking attributes {node_id = 22 : i64} {
              obelisk.sv.timing.signal_event attributes {edge_kind = 1 : i32, has_iff = false, node_id = 23 : i64} {
                obelisk.sv.expression.member_access attributes {is_signed = false, member_name = "clk", node_id = 24 : i64, referenced_path = "top.bus_if.clk", referenced_symbol = @s2.$root::@s4.top::@s5.top::@s6.a_if::@s7.bus_if::@s8.clk, semantic_type = !obelisk.integral<1, false, true, 0 : 0, logic>} {
                  obelisk.sv.expression.named_value attributes {is_signed = false, node_id = 25 : i64, referenced_path = "top.va", referenced_symbol = @s2.$root::@s4.top::@s5.top::@s13.va, semantic_type = !obelisk.virtual_interface<@s2.$root::@s5.top::@s12.bus_if, "">} {}
                }
              }
              obelisk.sv.assertion.sequence_concat attributes {delays = [{is_unbounded = false, max = 0 : i64, min = 0 : i64}, {is_unbounded = false, max = 1 : i64, min = 1 : i64}], node_id = 26 : i64} {
                obelisk.sv.assertion.simple attributes {has_repetition = false, is_null = false, node_id = 27 : i64, repetition_is_unbounded = false} {
                  obelisk.sv.expression.named_value attributes {is_signed = false, node_id = 28 : i64, referenced_path = "top.a", referenced_symbol = @s2.$root::@s4.top::@s5.top::@s15.a, semantic_type = !obelisk.integral<1, false, true, 0 : 0, logic>} {}
                }
                obelisk.sv.assertion.clocking attributes {node_id = 29 : i64} {
                  obelisk.sv.timing.signal_event attributes {edge_kind = 1 : i32, has_iff = false, node_id = 30 : i64} {
                    obelisk.sv.expression.member_access attributes {is_signed = false, member_name = "clk", node_id = 31 : i64, referenced_path = "top.bus_if.clk", referenced_symbol = @s2.$root::@s4.top::@s5.top::@s6.a_if::@s7.bus_if::@s8.clk, semantic_type = !obelisk.integral<1, false, true, 0 : 0, logic>} {
                      obelisk.sv.expression.named_value attributes {is_signed = false, node_id = 32 : i64, referenced_path = "top.vb", referenced_symbol = @s2.$root::@s4.top::@s5.top::@s14.vb, semantic_type = !obelisk.virtual_interface<@s2.$root::@s5.top::@s12.bus_if, "">} {}
                    }
                  }
                  obelisk.sv.assertion.simple attributes {has_repetition = false, is_null = false, node_id = 33 : i64, repetition_is_unbounded = false} {
                    obelisk.sv.expression.named_value attributes {is_signed = false, node_id = 34 : i64, referenced_path = "top.b", referenced_symbol = @s2.$root::@s4.top::@s5.top::@s16.b, semantic_type = !obelisk.integral<1, false, true, 0 : 0, logic>} {}
                  }
                }
              }
            }
          }
        }

        // Two dynamically selected instances of the same unqualified
        // clocking block are also distinct clocks. Deliberately retain the
        // same static clocking_event_symbol on both member descriptors to
        // guard against bypassing receiver-chain comparison.
        obelisk.sv.symbol.procedural_block attributes {hierarchical_name = "top", node_id = 80 : i64, procedure_kind = 2 : i32, sym_name = "s80", time_precision_fs = 1000000 : i64, time_unit_fs = 1000000 : i64} {
          obelisk.sv.statement.concurrent_assertion attributes {assertion_kind = 0 : i32, has_default_disable = false, has_fail_action = false, has_pass_action = false, node_id = 81 : i64} {
            obelisk.sv.assertion.clocking attributes {node_id = 82 : i64} {
              obelisk.sv.timing.signal_event attributes {edge_kind = 0 : i32, has_iff = false, node_id = 83 : i64} {
                obelisk.sv.expression.member_access attributes {clocking_event_symbol = @s2.$root::@s4.top::@s5.top::@s6.a_if::@s7.bus_if::@s8.clk, is_signed = false, member_name = "cb", node_id = 84 : i64, referenced_path = "top.bus_if.cb", referenced_symbol = @s2.$root::@s4.top::@s5.top::@s12.bus_if::@s12.body::@s13.cb, semantic_type = !obelisk.void, virtual_interface_clock_event_edge = 1 : i32, virtual_interface_clock_member = "clk", virtual_interface_clocking_block_event} {
                  obelisk.sv.expression.named_value attributes {is_signed = false, node_id = 85 : i64, referenced_path = "top.va", referenced_symbol = @s2.$root::@s4.top::@s5.top::@s13.va, semantic_type = !obelisk.virtual_interface<@s2.$root::@s5.top::@s12.bus_if, "">} {}
                }
              }
              obelisk.sv.assertion.sequence_concat attributes {delays = [{is_unbounded = false, max = 0 : i64, min = 0 : i64}, {is_unbounded = false, max = 1 : i64, min = 1 : i64}], node_id = 86 : i64} {
                obelisk.sv.assertion.simple attributes {has_repetition = false, is_null = false, node_id = 87 : i64, repetition_is_unbounded = false} {
                  obelisk.sv.expression.named_value attributes {is_signed = false, node_id = 88 : i64, referenced_path = "top.a", referenced_symbol = @s2.$root::@s4.top::@s5.top::@s15.a, semantic_type = !obelisk.integral<1, false, true, 0 : 0, logic>} {}
                }
                obelisk.sv.assertion.clocking attributes {node_id = 89 : i64} {
                  obelisk.sv.timing.signal_event attributes {edge_kind = 0 : i32, has_iff = false, node_id = 90 : i64} {
                    obelisk.sv.expression.member_access attributes {clocking_event_symbol = @s2.$root::@s4.top::@s5.top::@s6.a_if::@s7.bus_if::@s8.clk, is_signed = false, member_name = "cb", node_id = 91 : i64, referenced_path = "top.bus_if.cb", referenced_symbol = @s2.$root::@s4.top::@s5.top::@s12.bus_if::@s12.body::@s13.cb, semantic_type = !obelisk.void, virtual_interface_clock_event_edge = 1 : i32, virtual_interface_clock_event_has_iff, virtual_interface_clock_member = "clk", virtual_interface_clocking_block_event} {
                      obelisk.sv.expression.named_value attributes {is_signed = false, node_id = 92 : i64, referenced_path = "top.vb", referenced_symbol = @s2.$root::@s4.top::@s5.top::@s14.vb, semantic_type = !obelisk.virtual_interface<@s2.$root::@s5.top::@s12.bus_if, "">} {}
                      obelisk.sv.expression.named_value attributes {is_signed = false, node_id = 95 : i64, referenced_path = "top.bus_if.clk", referenced_symbol = @s2.$root::@s4.top::@s5.top::@s6.a_if::@s7.bus_if::@s8.clk, semantic_type = !obelisk.integral<1, false, true, 0 : 0, logic>} {}
                      obelisk.sv.expression.named_value attributes {is_signed = false, node_id = 96 : i64, referenced_path = "top.b", referenced_symbol = @s2.$root::@s4.top::@s5.top::@s16.b, semantic_type = !obelisk.integral<1, false, true, 0 : 0, logic>} {}
                    }
                  }
                  obelisk.sv.assertion.simple attributes {has_repetition = false, is_null = false, node_id = 93 : i64, repetition_is_unbounded = false} {
                    obelisk.sv.expression.named_value attributes {is_signed = false, node_id = 94 : i64, referenced_path = "top.b", referenced_symbol = @s2.$root::@s4.top::@s5.top::@s16.b, semantic_type = !obelisk.integral<1, false, true, 0 : 0, logic>} {}
                  }
                }
              }
            }
          }
        }

        // A clocking-block event passed through expanded property and sequence
        // formals has the same selected receiver and underlying clock as the
        // outer direct member event. It must stay a single-clock monitor.
        obelisk.sv.symbol.procedural_block attributes {hierarchical_name = "top", node_id = 40 : i64, procedure_kind = 2 : i32, sym_name = "s40", time_precision_fs = 1000000 : i64, time_unit_fs = 1000000 : i64} {
          obelisk.sv.statement.concurrent_assertion attributes {assertion_kind = 2 : i32, has_default_disable = false, has_fail_action = false, has_pass_action = false, node_id = 41 : i64} {
            obelisk.sv.assertion.clocking attributes {node_id = 42 : i64} {
              obelisk.sv.timing.signal_event attributes {edge_kind = 1 : i32, has_iff = false, node_id = 43 : i64} {
                obelisk.sv.expression.member_access attributes {is_signed = false, member_name = "clk", node_id = 44 : i64, referenced_path = "top.bus_if.clk", referenced_symbol = @s2.$root::@s4.top::@s5.top::@s6.a_if::@s7.bus_if::@s8.clk, semantic_type = !obelisk.integral<1, false, true, 0 : 0, logic>} {
                  obelisk.sv.expression.named_value attributes {is_signed = false, node_id = 45 : i64, referenced_path = "top.va", referenced_symbol = @s2.$root::@s4.top::@s5.top::@s13.va, semantic_type = !obelisk.virtual_interface<@s2.$root::@s5.top::@s12.bus_if, "">} {}
                }
              }
              obelisk.sv.assertion.simple attributes {has_repetition = false, is_null = false, node_id = 46 : i64, repetition_is_unbounded = false} {
                obelisk.sv.expression.assertion_instance attributes {argument_count = 1 : i64, argument_formal_paths = ["top.outer.s"], argument_formal_symbols = [@s2.$root::@s4.top::@s5.top::@s19.outer::@s20.s], argument_kinds = array<i64: 1>, has_expanded_body = true, is_recursive_property = false, local_variable_count = 0 : i64, local_variable_has_initializer = array<i64>, local_variable_paths = [], local_variable_symbols = [], node_id = 47 : i64, referenced_path = "top.outer", referenced_symbol = @s2.$root::@s4.top::@s5.top::@s19.outer, semantic_type = !obelisk.property} {
                  obelisk.sv.assertion.simple attributes {has_repetition = false, is_null = false, node_id = 48 : i64, repetition_is_unbounded = false} {
                    obelisk.sv.expression.assertion_instance attributes {argument_count = 1 : i64, argument_formal_paths = ["top.inner.c"], argument_formal_symbols = [@s2.$root::@s4.top::@s5.top::@s17.inner::@s18.c], argument_kinds = array<i64: 2>, has_expanded_body = true, is_recursive_property = false, local_variable_count = 0 : i64, local_variable_has_initializer = array<i64>, local_variable_paths = [], local_variable_symbols = [], node_id = 49 : i64, referenced_path = "top.inner", referenced_symbol = @s2.$root::@s4.top::@s5.top::@s17.inner, semantic_type = !obelisk.sequence} {
                      obelisk.sv.assertion.clocking attributes {node_id = 50 : i64} {
                        obelisk.sv.timing.signal_event attributes {edge_kind = 0 : i32, has_iff = false, node_id = 51 : i64} {
                          obelisk.sv.expression.member_access attributes {is_signed = false, member_name = "cb", node_id = 52 : i64, referenced_path = "top.bus_if.cb", referenced_symbol = @s2.$root::@s4.top::@s5.top::@s12.bus_if::@s12.body::@s13.cb, semantic_type = !obelisk.void, virtual_interface_clock_event_edge = 1 : i32, virtual_interface_clock_member = "clk", virtual_interface_clocking_block_event} {
                            obelisk.sv.expression.named_value attributes {is_signed = false, node_id = 53 : i64, referenced_path = "top.va", referenced_symbol = @s2.$root::@s4.top::@s5.top::@s13.va, semantic_type = !obelisk.virtual_interface<@s2.$root::@s5.top::@s12.bus_if, "">} {}
                          }
                        }
                        obelisk.sv.assertion.simple attributes {has_repetition = false, is_null = false, node_id = 54 : i64, repetition_is_unbounded = false} {
                          obelisk.sv.expression.named_value attributes {is_signed = false, node_id = 55 : i64, referenced_path = "top.a", referenced_symbol = @s2.$root::@s4.top::@s5.top::@s15.a, semantic_type = !obelisk.integral<1, false, true, 0 : 0, logic>} {}
                        }
                      }
                      obelisk.sv.timing.signal_event attributes {edge_kind = 0 : i32, has_iff = false, node_id = 58 : i64} {
                        obelisk.sv.expression.member_access attributes {is_signed = false, member_name = "cb", node_id = 59 : i64, referenced_path = "top.bus_if.cb", referenced_symbol = @s2.$root::@s4.top::@s5.top::@s12.bus_if::@s12.body::@s13.cb, semantic_type = !obelisk.void, virtual_interface_clock_event_edge = 1 : i32, virtual_interface_clock_member = "clk", virtual_interface_clocking_block_event} {
                          obelisk.sv.expression.named_value attributes {is_signed = false, node_id = 63 : i64, referenced_path = "top.va", referenced_symbol = @s2.$root::@s4.top::@s5.top::@s13.va, semantic_type = !obelisk.virtual_interface<@s2.$root::@s5.top::@s12.bus_if, "">} {}
                        }
                      }
                    }
                  }
                  obelisk.sv.assertion.simple attributes {has_repetition = false, is_null = false, node_id = 56 : i64, repetition_is_unbounded = false} {
                    obelisk.sv.expression.integer_literal attributes {constant_value = "1'b1", is_signed = false, node_id = 57 : i64, semantic_type = !obelisk.integral<1, false, false, 0 : 0, bit>} {}
                  }
                }
              }
            }
          }
        }

        // A declared iff on a dynamically selected clocking-block event uses
        // the existing virtual observer binding in an assertion monitor too.
        obelisk.sv.symbol.procedural_block attributes {hierarchical_name = "top", node_id = 70 : i64, procedure_kind = 2 : i32, sym_name = "s70", time_precision_fs = 1000000 : i64, time_unit_fs = 1000000 : i64} {
          obelisk.sv.statement.concurrent_assertion attributes {assertion_kind = 2 : i32, has_default_disable = false, has_fail_action = false, has_pass_action = false, node_id = 71 : i64} {
            obelisk.sv.assertion.clocking attributes {node_id = 72 : i64} {
              obelisk.sv.timing.signal_event attributes {edge_kind = 0 : i32, has_iff = false, node_id = 73 : i64} {
                obelisk.sv.expression.member_access attributes {is_signed = false, member_name = "cb", node_id = 74 : i64, referenced_path = "top.bus_if.cb", referenced_symbol = @s2.$root::@s4.top::@s5.top::@s12.bus_if::@s12.body::@s13.cb, semantic_type = !obelisk.void, virtual_interface_clock_event_edge = 1 : i32, virtual_interface_clock_event_has_iff, virtual_interface_clock_member = "clk", virtual_interface_clocking_block_event} {
                  obelisk.sv.expression.named_value attributes {is_signed = false, node_id = 75 : i64, referenced_path = "top.va", referenced_symbol = @s2.$root::@s4.top::@s5.top::@s13.va, semantic_type = !obelisk.virtual_interface<@s2.$root::@s5.top::@s12.bus_if, "">} {}
                  obelisk.sv.expression.named_value attributes {is_signed = false, node_id = 76 : i64, referenced_path = "top.bus_if.clk", referenced_symbol = @s2.$root::@s4.top::@s5.top::@s6.a_if::@s7.bus_if::@s8.clk, semantic_type = !obelisk.integral<1, false, true, 0 : 0, logic>} {}
                  obelisk.sv.expression.named_value attributes {is_signed = false, node_id = 77 : i64, referenced_path = "top.bus_if.clk", referenced_symbol = @s2.$root::@s4.top::@s5.top::@s6.a_if::@s7.bus_if::@s8.clk, semantic_type = !obelisk.integral<1, false, true, 0 : 0, logic>} {}
                }
              }
              obelisk.sv.assertion.simple attributes {has_repetition = false, is_null = false, node_id = 78 : i64, repetition_is_unbounded = false} {
                obelisk.sv.expression.named_value attributes {is_signed = false, node_id = 79 : i64, referenced_path = "top.a", referenced_symbol = @s2.$root::@s4.top::@s5.top::@s15.a, semantic_type = !obelisk.integral<1, false, true, 0 : 0, logic>} {}
              }
            }
          }
        }

        obelisk.sv.symbol.instance attributes {hierarchical_name = "top.bus_if", is_uninstantiated = false, is_virtual_interface_type_instance = true, name = "bus_if", node_id = 60 : i64, referenced_path = "bus_if", referenced_symbol = @s0.bus_if, sym_name = "s12.bus_if"} {
          obelisk.sv.symbol.instance_body attributes {hierarchical_name = "top.bus_if", name = "bus_if", node_id = 61 : i64, sym_name = "s12.body", time_precision_fs = 1000000 : i64, time_unit_fs = 1000000 : i64} {
            obelisk.sv.symbol.clocking_block attributes {hierarchical_name = "top.bus_if.cb", is_default = false, is_global = false, name = "cb", node_id = 62 : i64, sym_name = "s13.cb"} {}
          }
        }
      }
    }
  }
}

// CHECK-LABEL: obelisk_sim.func private @unit_0(
// CHECK-SAME: obelisk_sim.multiclock_sequence_coordinator
// CHECK-COUNT-2: obelisk_sim.virtual_interface.scope
// CHECK: obelisk_sim.suspend.clock_set
// CHECK: obelisk_sim.assert.clock_occurrence.consume
// CHECK: obelisk_sim.spawn @unit_0.fork.
// CHECK-LABEL: obelisk_sim.func private @unit_1(
// CHECK-SAME: obelisk_sim.multiclock_sequence_coordinator
// CHECK-COUNT-2: obelisk_sim.virtual_interface.scope
// CHECK: obelisk_sim.suspend.clock_set
// CHECK: obelisk_sim.assert.clock_occurrence.consume
// CHECK: arith.select
// CHECK: obelisk_sim.spawn @unit_1.fork.
// CHECK-LABEL: obelisk_sim.func private @unit_2(
// CHECK-NOT: obelisk_sim.multiclock_sequence_monitor
// CHECK: obelisk_sim.virtual_interface.scope
// CHECK: obelisk_sim.suspend.edge posedge
// CHECK-NOT: obelisk_sim.spawn
// CHECK-LABEL: obelisk_sim.func private @unit_3(
// CHECK: obelisk_sim.virtual_interface.scope
// CHECK-COUNT-2: obelisk_sim.observer.bind
// CHECK: obelisk_sim.suspend.observe
// CHECK-SAME: conditions 1 edges [1] indices [0]
// CHECK-NOT: obelisk.sv.
