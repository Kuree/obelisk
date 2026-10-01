// RUN: obelisk-opt %s '--lower-obelisk-to-sim=opt-level=0' | FileCheck %s
// RUN: sed 's/virtual_interface_clock_input_skew_one_step/virtual_interface_clock_input_skew_delay = "0"/' %s | obelisk-opt '--lower-obelisk-to-sim=opt-level=0' | FileCheck %s --check-prefix=INPUT-ZERO
// RUN: sed 's/virtual_interface_clock_input_skew_edge = 0 : i32, virtual_interface_clock_input_skew_one_step/virtual_interface_clock_input_skew_edge = 2 : i32, virtual_interface_clock_input_skew_edge_only/' %s | obelisk-opt '--lower-obelisk-to-sim=opt-level=0' | FileCheck %s --check-prefix=INPUT-EDGE
// RUN: sed 's/virtual_interface_clock_input_skew_one_step/virtual_interface_clock_input_skew_delay = "2"/' %s | obelisk-opt '--lower-obelisk-to-sim=opt-level=0' | FileCheck %s --check-prefix=INPUT-SKEW
// RUN: sed -e 's/test_output_iff, virtual_interface_clock_event_has_iff, //' -e '/test_output_iff_child/d' -e 's/virtual_interface_clock_output_skew_edge = 0 : i32/virtual_interface_clock_output_skew_edge = 2 : i32/g' %s | obelisk-opt '--lower-obelisk-to-sim=opt-level=0' | FileCheck %s --check-prefix=OUTPUT-EDGE
// RUN: sed 's/definition_kind = 0 : i32/definition_kind = 2 : i32/' %s | obelisk-opt '--lower-obelisk-to-sim=opt-level=0' | FileCheck %s --check-prefix=PROGRAM
// RUN: sed -e 's/member_name = "signal", node_id = 36/member_name = "ready", node_id = 36/' -e 's/member_name = "signal", node_id = 79/member_name = "ready", node_id = 79/' %s | obelisk-opt '--lower-obelisk-to-sim=opt-level=0' | FileCheck %s --check-prefix=NET-OUTPUT
// RUN: sed -e 's/member_name = "signal", node_id = 36/member_name = "driven", node_id = 36, virtual_interface_clocking_signal_member = "ready"/' -e 's/member_name = "ready", node_id = 38/member_name = "sampled", node_id = 38, virtual_interface_clocking_signal_member = "ready"/' -e 's/member_name = "signal", node_id = 79/member_name = "driven", node_id = 79, virtual_interface_clocking_signal_member = "ready"/' %s | obelisk-opt '--lower-obelisk-to-sim=opt-level=0' | FileCheck %s

module {
  obelisk.sv.symbol.definition @s0.bus_if attributes {definition_kind = 1 : i32, hierarchical_name = "bus_if", name = "bus_if", node_id = 0 : i64} {}
  obelisk.sv.symbol.definition @s1.top attributes {definition_kind = 0 : i32, hierarchical_name = "top", name = "top", node_id = 1 : i64} {}
  obelisk.sv.symbol.root @s2.$root attributes {hierarchical_name = "\\$root ", name = "$root", node_id = 2 : i64} {
    obelisk.sv.symbol.compilation_unit @s3 attributes {hierarchical_name = "$unit", node_id = 3 : i64} {}
    obelisk.sv.symbol.instance @s4.top attributes {hierarchical_name = "top", is_uninstantiated = false, name = "top", node_id = 4 : i64, referenced_path = "top", referenced_symbol = @s1.top} {
      obelisk.sv.symbol.instance_body @s5.top attributes {hierarchical_name = "top", name = "top", node_id = 5 : i64, time_precision_fs = 1000000 : i64, time_unit_fs = 1000000 : i64} {
        obelisk.sv.symbol.instance @s6.bus attributes {hierarchical_name = "top.bus", is_uninstantiated = false, name = "bus", node_id = 6 : i64, referenced_path = "bus_if", referenced_symbol = @s0.bus_if} {
          obelisk.sv.symbol.instance_body @s7.bus_if attributes {hierarchical_name = "top.bus", name = "bus_if", node_id = 7 : i64, time_precision_fs = 1000000 : i64, time_unit_fs = 1000000 : i64, virtual_interface_identity = @s2.$root::@s5.top::@s9.bus_if} {
            obelisk.sv.symbol.variable @s12.signal attributes {hierarchical_name = "top.bus.signal", lifetime = 1 : i32, name = "signal", node_id = 23 : i64, semantic_type = !obelisk.integral<1, false, true, 0 : 0, logic>} {}
            obelisk.sv.symbol.net @s13.ready attributes {hierarchical_name = "top.bus.ready", is_implicit = false, name = "ready", net_kind = 1 : i32, node_id = 24 : i64, semantic_type = !obelisk.integral<1, false, true, 0 : 0, logic>} {}
            obelisk.sv.symbol.variable @s19.clk attributes {hierarchical_name = "top.bus.clk", lifetime = 1 : i32, name = "clk", node_id = 70 : i64, semantic_type = !obelisk.integral<1, false, true, 0 : 0, logic>} {}
          }
        }
        obelisk.sv.symbol.instance @s14.other attributes {hierarchical_name = "top.other", is_uninstantiated = false, name = "other", node_id = 50 : i64, referenced_path = "bus_if", referenced_symbol = @s0.bus_if} {
          obelisk.sv.symbol.instance_body @s15.bus_if attributes {hierarchical_name = "top.other", name = "bus_if", node_id = 51 : i64, time_precision_fs = 1000000 : i64, time_unit_fs = 1000000 : i64, virtual_interface_identity = @s2.$root::@s5.top::@s9.bus_if} {
            obelisk.sv.symbol.variable @s16.signal attributes {hierarchical_name = "top.other.signal", lifetime = 1 : i32, name = "signal", node_id = 52 : i64, semantic_type = !obelisk.integral<1, false, true, 0 : 0, logic>} {}
            obelisk.sv.symbol.net @s17.ready attributes {hierarchical_name = "top.other.ready", is_implicit = false, name = "ready", net_kind = 1 : i32, node_id = 53 : i64, semantic_type = !obelisk.integral<1, false, true, 0 : 0, logic>} {}
            obelisk.sv.symbol.variable @s20.clk attributes {hierarchical_name = "top.other.clk", lifetime = 1 : i32, name = "clk", node_id = 71 : i64, semantic_type = !obelisk.integral<1, false, true, 0 : 0, logic>} {}
          }
        }
        obelisk.sv.symbol.variable @s8.vif attributes {hierarchical_name = "top.vif", lifetime = 1 : i32, name = "vif", node_id = 8 : i64, semantic_type = !obelisk.virtual_interface<@s2.$root::@s5.top::@s9.bus_if, "">} {}
        // Keep an otherwise unused modport view so VPI retains both the
        // modport typespec and its base-interface parent typespec.
        obelisk.sv.symbol.variable @s104.vif_phy attributes {hierarchical_name = "top.vif_phy", lifetime = 1 : i32, name = "vif_phy", node_id = 104 : i64, semantic_type = !obelisk.virtual_interface<@s2.$root::@s5.top::@s9.bus_if, "phy">} {}
        obelisk.sv.symbol.procedural_block @s10 attributes {hierarchical_name = "top", node_id = 9 : i64, procedure_kind = 0 : i32, time_precision_fs = 1000000 : i64, time_unit_fs = 1000000 : i64} {
          obelisk.sv.statement.block attributes {node_id = 10 : i64} {
            obelisk.sv.statement.list attributes {node_id = 11 : i64} {
              obelisk.sv.statement.expression_statement attributes {node_id = 12 : i64} {
                obelisk.sv.expression.assignment attributes {assignment_kind = 0 : i32, is_signed = false, node_id = 13 : i64, semantic_type = !obelisk.virtual_interface<@s2.$root::@s5.top::@s9.bus_if, "">} {
                  obelisk.sv.expression.named_value attributes {is_signed = false, node_id = 14 : i64, referenced_path = "top.vif", referenced_symbol = @s2.$root::@s4.top::@s5.top::@s8.vif, semantic_type = !obelisk.virtual_interface<@s2.$root::@s5.top::@s9.bus_if, "">} {}
                  obelisk.sv.expression.arbitrary_symbol attributes {is_signed = false, node_id = 15 : i64, referenced_path = "top.bus", referenced_symbol = @s2.$root::@s4.top::@s5.top::@s6.bus, semantic_type = !obelisk.virtual_interface<@s2.$root::@s5.top::@s9.bus_if, "">} {}
                }
              }
              obelisk.sv.statement.expression_statement attributes {node_id = 85 : i64} {
                obelisk.sv.expression.call attributes {
                  argument_count = 2 : i64, callee_name = "$sformatf",
                  constraint_restrictions = [], defaulted_arguments = array<i64: 0, 0>,
                  has_inline_constraints = false, has_iterator_expression = false,
                  has_output_arguments = false, has_this_class = false,
                  is_signed = false, is_super_class = false,
                  is_system_call = true, node_id = 86 : i64,
                  semantic_type = !obelisk.string, subroutine_kind = 0 : i32,
                  system_library_cell = "work.top", system_scope_path = "top",
                  system_scope_symbol = @s2.$root::@s4.top::@s5.top
                } {
                  obelisk.sv.expression.string_literal attributes {
                    constant_value = "%p", is_signed = false,
                    node_id = 87 : i64,
                    semantic_type = !obelisk.ranged_packed_array<15 : 0 x !obelisk.integral<1, false, false, 0 : 0, bit>>
                  } {}
                  obelisk.sv.expression.named_value attributes {
                    is_signed = false, node_id = 88 : i64,
                    referenced_path = "top.vif",
                    referenced_symbol = @s2.$root::@s4.top::@s5.top::@s8.vif,
                    semantic_type = !obelisk.virtual_interface<@s2.$root::@s5.top::@s9.bus_if, "">
                  } {}
                }
              }
              obelisk.sv.statement.expression_statement attributes {node_id = 25 : i64} {
                obelisk.sv.expression.assignment attributes {assignment_kind = 0 : i32, is_signed = false, node_id = 26 : i64, semantic_type = !obelisk.integral<1, false, true, 0 : 0, logic>} {
                  obelisk.sv.expression.member_access attributes {is_signed = false, member_name = "signal", node_id = 27 : i64, referenced_path = "top.bus_if.signal", referenced_symbol = @s2.$root::@s4.top::@s5.top::@s6.bus::@s7.bus_if::@s12.signal, semantic_type = !obelisk.integral<1, false, true, 0 : 0, logic>} {
                    obelisk.sv.expression.named_value attributes {is_signed = false, node_id = 28 : i64, referenced_path = "top.vif", referenced_symbol = @s2.$root::@s4.top::@s5.top::@s8.vif, semantic_type = !obelisk.virtual_interface<@s2.$root::@s5.top::@s9.bus_if, "">} {}
                  }
                  obelisk.sv.expression.conversion attributes {is_signed = false, node_id = 29 : i64, semantic_type = !obelisk.integral<1, false, true, 0 : 0, logic>} {
                    obelisk.sv.expression.integer_literal attributes {constant_value = "1'b1", is_signed = false, node_id = 30 : i64, semantic_type = !obelisk.integral<1, false, false, 0 : 0, bit>} {}
                  }
                }
              }
              obelisk.sv.statement.expression_statement attributes {node_id = 31 : i64} {
                obelisk.sv.expression.member_access attributes {is_signed = false, member_name = "signal", node_id = 32 : i64, referenced_path = "top.bus_if.signal", referenced_symbol = @s2.$root::@s4.top::@s5.top::@s6.bus::@s7.bus_if::@s12.signal, semantic_type = !obelisk.integral<1, false, true, 0 : 0, logic>} {
                  obelisk.sv.expression.named_value attributes {is_signed = false, node_id = 33 : i64, referenced_path = "top.vif", referenced_symbol = @s2.$root::@s4.top::@s5.top::@s8.vif, semantic_type = !obelisk.virtual_interface<@s2.$root::@s5.top::@s9.bus_if, "">} {}
                }
              }
              obelisk.sv.statement.expression_statement attributes {node_id = 34 : i64} {
                obelisk.sv.expression.assignment attributes {assignment_kind = 0 : i32, is_signed = false, node_id = 35 : i64, semantic_type = !obelisk.integral<1, false, true, 0 : 0, logic>} {
                  obelisk.sv.expression.member_access attributes {is_signed = false, member_name = "signal", node_id = 36 : i64, referenced_path = "top.bus_if.signal", referenced_symbol = @s2.$root::@s4.top::@s5.top::@s6.bus::@s7.bus_if::@s12.signal, semantic_type = !obelisk.integral<1, false, true, 0 : 0, logic>, test_output_iff, virtual_interface_clock_event_has_iff, virtual_interface_access_direction = 1 : i32, virtual_interface_clock_event_edge = 1 : i32, virtual_interface_clock_member = "clk", virtual_interface_clock_output_skew_delay = "0", virtual_interface_clock_output_skew_edge = 0 : i32, virtual_interface_clock_time_precision_fs = 1000000 : i64, virtual_interface_clock_time_unit_fs = 1000000 : i64, virtual_interface_clocking, virtual_interface_clocking_block = "cb"} {
                    obelisk.sv.expression.named_value attributes {is_signed = false, node_id = 37 : i64, referenced_path = "top.vif", referenced_symbol = @s2.$root::@s4.top::@s5.top::@s8.vif, semantic_type = !obelisk.virtual_interface<@s2.$root::@s5.top::@s9.bus_if, "">} {}
                    obelisk.sv.expression.named_value attributes {is_signed = false, node_id = 102 : i64, referenced_path = "top.bus_if.clk", referenced_symbol = @s2.$root::@s4.top::@s5.top::@s6.bus::@s7.bus_if::@s19.clk, semantic_type = !obelisk.integral<1, false, true, 0 : 0, logic>, test_output_iff_child} {}
                    obelisk.sv.expression.named_value attributes {is_signed = false, node_id = 103 : i64, referenced_path = "top.bus_if.ready", referenced_symbol = @s2.$root::@s4.top::@s5.top::@s6.bus::@s7.bus_if::@s13.ready, semantic_type = !obelisk.integral<1, false, true, 0 : 0, logic>, test_output_iff_child} {}
                  }
                  obelisk.sv.expression.member_access attributes {is_signed = false, member_name = "ready", node_id = 38 : i64, referenced_path = "top.bus_if.ready", referenced_symbol = @s2.$root::@s4.top::@s5.top::@s6.bus::@s7.bus_if::@s13.ready, semantic_type = !obelisk.integral<1, false, true, 0 : 0, logic>, virtual_interface_access_direction = 0 : i32, virtual_interface_clock_event_edge = 1 : i32, virtual_interface_clock_event_has_iff, virtual_interface_clock_input_skew_edge = 0 : i32, virtual_interface_clock_input_skew_one_step, virtual_interface_clock_member = "clk", virtual_interface_clock_time_precision_fs = 1000000 : i64, virtual_interface_clock_time_unit_fs = 1000000 : i64, virtual_interface_clocking, virtual_interface_clocking_block = "cb"} {
                    obelisk.sv.expression.named_value attributes {is_signed = false, node_id = 39 : i64, referenced_path = "top.vif", referenced_symbol = @s2.$root::@s4.top::@s5.top::@s8.vif, semantic_type = !obelisk.virtual_interface<@s2.$root::@s5.top::@s9.bus_if, "">} {}
                    obelisk.sv.expression.named_value attributes {is_signed = false, node_id = 98 : i64, referenced_path = "top.bus_if.clk", referenced_symbol = @s2.$root::@s4.top::@s5.top::@s6.bus::@s7.bus_if::@s19.clk, semantic_type = !obelisk.integral<1, false, true, 0 : 0, logic>} {}
                    obelisk.sv.expression.binary_op attributes {is_signed = false, node_id = 99 : i64, operator_kind = 19 : i32, semantic_type = !obelisk.integral<1, false, true, 0 : 0, logic>} {
                      obelisk.sv.expression.named_value attributes {is_signed = false, node_id = 100 : i64, referenced_path = "top.bus_if.ready", referenced_symbol = @s2.$root::@s4.top::@s5.top::@s6.bus::@s7.bus_if::@s13.ready, semantic_type = !obelisk.integral<1, false, true, 0 : 0, logic>} {}
                      obelisk.sv.expression.named_value attributes {is_signed = false, node_id = 101 : i64, referenced_path = "top.bus_if.clk", referenced_symbol = @s2.$root::@s4.top::@s5.top::@s6.bus::@s7.bus_if::@s19.clk, semantic_type = !obelisk.integral<1, false, true, 0 : 0, logic>} {}
                    }
                  }
                }
              }
              obelisk.sv.statement.expression_statement attributes {node_id = 40 : i64} {
                obelisk.sv.expression.assignment attributes {assignment_kind = 0 : i32, is_signed = false, node_id = 41 : i64, semantic_type = !obelisk.virtual_interface<@s2.$root::@s5.top::@s9.bus_if, "">} {
                  obelisk.sv.expression.named_value attributes {is_signed = false, node_id = 42 : i64, referenced_path = "top.vif", referenced_symbol = @s2.$root::@s4.top::@s5.top::@s8.vif, semantic_type = !obelisk.virtual_interface<@s2.$root::@s5.top::@s9.bus_if, "">} {}
                  obelisk.sv.expression.conversion attributes {is_signed = false, node_id = 43 : i64, semantic_type = !obelisk.virtual_interface<@s2.$root::@s5.top::@s9.bus_if, "">} {
                    obelisk.sv.expression.null_literal attributes {is_signed = false, node_id = 44 : i64, semantic_type = !obelisk.null} {}
                  }
                }
              }
              obelisk.sv.statement.timed attributes {node_id = 73 : i64} {
                obelisk.sv.timing.signal_event attributes {edge_kind = 0 : i32, has_iff = false, node_id = 74 : i64} {
                  obelisk.sv.expression.member_access attributes {is_signed = false, member_name = "cb", node_id = 75 : i64, referenced_path = "top.bus_if.cb", referenced_symbol = @s2.$root::@s4.top::@s5.top::@s9.bus_if::@s11.bus_if::@s21.cb, semantic_type = !obelisk.void, virtual_interface_clock_event_edge = 1 : i32, virtual_interface_clock_event_has_iff, virtual_interface_clock_member = "clk", virtual_interface_clocking_block_event} {
                    obelisk.sv.expression.named_value attributes {is_signed = false, node_id = 76 : i64, referenced_path = "top.vif", referenced_symbol = @s2.$root::@s4.top::@s5.top::@s8.vif, semantic_type = !obelisk.virtual_interface<@s2.$root::@s5.top::@s9.bus_if, "">} {}
                    obelisk.sv.expression.named_value attributes {is_signed = false, node_id = 94 : i64, referenced_path = "top.bus_if.clk", referenced_symbol = @s2.$root::@s4.top::@s5.top::@s6.bus::@s7.bus_if::@s19.clk, semantic_type = !obelisk.integral<1, false, true, 0 : 0, logic>} {}
                    obelisk.sv.expression.binary_op attributes {is_signed = false, node_id = 95 : i64, operator_kind = 19 : i32, semantic_type = !obelisk.integral<1, false, true, 0 : 0, logic>} {
                      obelisk.sv.expression.named_value attributes {is_signed = false, node_id = 96 : i64, referenced_path = "top.bus_if.ready", referenced_symbol = @s2.$root::@s4.top::@s5.top::@s6.bus::@s7.bus_if::@s13.ready, semantic_type = !obelisk.integral<1, false, true, 0 : 0, logic>} {}
                      obelisk.sv.expression.named_value attributes {is_signed = false, node_id = 97 : i64, referenced_path = "top.bus_if.clk", referenced_symbol = @s2.$root::@s4.top::@s5.top::@s6.bus::@s7.bus_if::@s19.clk, semantic_type = !obelisk.integral<1, false, true, 0 : 0, logic>} {}
                    }
                  }
                }
                obelisk.sv.statement.conditional attributes {check_kind = 0 : i32, condition_count = 1 : i64, condition_pattern_flags = array<i64: 0>, has_else = false, node_id = 83 : i64} {
                  obelisk.sv.expression.integer_literal attributes {constant_value = "1'b1", is_signed = false, node_id = 84 : i64, semantic_type = !obelisk.integral<1, false, false, 0 : 0, bit>} {}
                  obelisk.sv.statement.expression_statement attributes {node_id = 77 : i64} {
                  obelisk.sv.expression.assignment attributes {assignment_kind = 0 : i32, is_signed = false, node_id = 78 : i64, semantic_type = !obelisk.integral<1, false, true, 0 : 0, logic>} {
                    obelisk.sv.expression.member_access attributes {is_signed = false, member_name = "signal", node_id = 79 : i64, referenced_path = "top.bus_if.signal", referenced_symbol = @s2.$root::@s4.top::@s5.top::@s6.bus::@s7.bus_if::@s12.signal, semantic_type = !obelisk.integral<1, false, true, 0 : 0, logic>, virtual_interface_access_direction = 1 : i32, virtual_interface_clock_event_edge = 1 : i32, virtual_interface_clock_member = "clk", virtual_interface_clock_output_skew_delay = "0", virtual_interface_clock_output_skew_edge = 0 : i32, virtual_interface_clock_time_precision_fs = 1000000 : i64, virtual_interface_clock_time_unit_fs = 1000000 : i64, virtual_interface_clocking, virtual_interface_clocking_block = "cb"} {
                      obelisk.sv.expression.named_value attributes {is_signed = false, node_id = 80 : i64, referenced_path = "top.vif", referenced_symbol = @s2.$root::@s4.top::@s5.top::@s8.vif, semantic_type = !obelisk.virtual_interface<@s2.$root::@s5.top::@s9.bus_if, "">} {}
                    }
                    obelisk.sv.expression.conversion attributes {is_signed = false, node_id = 81 : i64, semantic_type = !obelisk.integral<1, false, true, 0 : 0, logic>} {
                      obelisk.sv.expression.integer_literal attributes {constant_value = "1'b0", is_signed = false, node_id = 82 : i64, semantic_type = !obelisk.integral<1, false, false, 0 : 0, bit>} {}
                    }
                  }
                  }
                }
              }
              obelisk.sv.statement.timed attributes {node_id = 89 : i64} {
                obelisk.sv.timing.signal_event attributes {
                  edge_kind = 0 : i32, has_iff = false, node_id = 90 : i64
                } {
                  obelisk.sv.expression.member_access attributes {
                    is_signed = false, member_name = "signal", node_id = 91 : i64,
                    referenced_path = "top.bus_if.signal",
                    referenced_symbol = @s2.$root::@s4.top::@s5.top::@s6.bus::@s7.bus_if::@s12.signal,
                    semantic_type = !obelisk.integral<1, false, true, 0 : 0, logic>
                  } {
                    obelisk.sv.expression.named_value attributes {
                      is_signed = false, node_id = 92 : i64,
                      referenced_path = "top.vif",
                      referenced_symbol = @s2.$root::@s4.top::@s5.top::@s8.vif,
                      semantic_type = !obelisk.virtual_interface<@s2.$root::@s5.top::@s9.bus_if, "">
                    } {}
                  }
                }
                obelisk.sv.statement.empty attributes {node_id = 93 : i64} {}
              }
            }
          }
        }
        obelisk.sv.symbol.procedural_block @s18 attributes {hierarchical_name = "top", node_id = 60 : i64, procedure_kind = 3 : i32, time_precision_fs = 1000000 : i64, time_unit_fs = 1000000 : i64} {
          obelisk.sv.statement.block attributes {node_id = 61 : i64} {
            obelisk.sv.statement.list attributes {node_id = 62 : i64} {
              obelisk.sv.statement.expression_statement attributes {node_id = 63 : i64} {
                obelisk.sv.expression.member_access attributes {is_signed = false, member_name = "ready", node_id = 64 : i64, referenced_path = "top.bus_if.ready", referenced_symbol = @s2.$root::@s4.top::@s5.top::@s6.bus::@s7.bus_if::@s13.ready, semantic_type = !obelisk.integral<1, false, true, 0 : 0, logic>} {
                  obelisk.sv.expression.named_value attributes {is_signed = false, node_id = 65 : i64, referenced_path = "top.vif", referenced_symbol = @s2.$root::@s4.top::@s5.top::@s8.vif, semantic_type = !obelisk.virtual_interface<@s2.$root::@s5.top::@s9.bus_if, "">} {}
                }
              }
            }
          }
        }
        obelisk.sv.symbol.instance @s9.bus_if attributes {hierarchical_name = "top.bus_if", is_uninstantiated = false, is_virtual_interface_type_instance = true, name = "bus_if", node_id = 21 : i64, referenced_path = "bus_if", referenced_symbol = @s0.bus_if} {
          obelisk.sv.symbol.instance_body @s11.bus_if attributes {hierarchical_name = "top.bus_if", name = "bus_if", node_id = 22 : i64, time_precision_fs = 1000000 : i64, time_unit_fs = 1000000 : i64} {
            obelisk.sv.symbol.clocking_block @s21.cb attributes {hierarchical_name = "top.bus_if.cb", is_default = false, is_global = false, name = "cb", node_id = 72 : i64} {}
          }
        }
      }
    }
  }
}

// CHECK: simulation.scope.decl [[BUS:[0-9]+]] parent 1 hierarchy "top.bus" debug "bus_if" interface "@s2.$root::@s5.top::@s9.bus_if"
// CHECK: simulation.scope.decl [[OTHER:[0-9]+]] parent 1 hierarchy "top.other" debug "bus_if" interface "@s2.$root::@s5.top::@s9.bus_if"
// CHECK: simulation.vpi_object.anchor @[[BUS_ANCHOR:__obelisk_vpi_anchor_[0-9]+]] id {{[0-9]+}} type 601 in [[BUS]] {{.*}}hierarchy "top.bus" debug "bus_if"
// CHECK: simulation.vpi_typespec.decl @[[VIF_TYPESPEC:__obelisk_vpi_interface_typespec_[0-9A-F]+_modport_]] id {{[0-9]+}} in [[BUS]] owner @[[BUS_ANCHOR]] hierarchy "@s2.$root::@s5.top::@s9.bus_if" debug "bus_if" {{.*}}symbol = @[[VIF_TYPESPEC]], modport = ""
// CHECK: simulation.vpi_typespec.decl @[[VIF_PHY_TYPESPEC:__obelisk_vpi_interface_typespec_[0-9A-F]+_modport_[0-9A-F]+]] id {{[0-9]+}} in [[BUS]] owner @[[BUS_ANCHOR]] hierarchy "@s2.$root::@s5.top::@s9.bus_if" debug "bus_if" {{.*}}symbol = @[[VIF_PHY_TYPESPEC]], modport = "phy"
// CHECK-DAG: simulation.storage.decl [[SIGNAL:[0-9]+]] in [[BUS]] : !simulation.logic<1> design hierarchy "top.bus.signal"
// CHECK-DAG: simulation.net.decl [[READY:[0-9]+]] in [[BUS]] : !simulation.logic<1> design hierarchy "top.bus.ready"
// CHECK-DAG: simulation.storage.decl [[CLK:[0-9]+]] in [[BUS]] : !simulation.logic<1> design hierarchy "top.bus.clk"
// CHECK-DAG: simulation.storage.decl [[OTHER_SIGNAL:[0-9]+]] in [[OTHER]] : !simulation.logic<1> design hierarchy "top.other.signal"
// CHECK-DAG: simulation.net.decl [[OTHER_READY:[0-9]+]] in [[OTHER]] : !simulation.logic<1> design hierarchy "top.other.ready"
// CHECK-DAG: simulation.storage.decl [[OTHER_CLK:[0-9]+]] in [[OTHER]] : !simulation.logic<1> design hierarchy "top.other.clk"
// CHECK: simulation.storage.decl {{[0-9]+}} in 1 : !simulation.virtual_interface<"@s2.$root::@s5.top::@s9.bus_if", ""> design hierarchy "top.vif" {{.*}}symbol = @[[VIF_TYPESPEC]], modport = ""
// CHECK: simulation.storage.decl {{[0-9]+}} in 1 : !simulation.virtual_interface<"@s2.$root::@s5.top::@s9.bus_if", "phy"> design hierarchy "top.vif_phy" {{.*}}symbol = @[[VIF_PHY_TYPESPEC]], modport = "phy"
// The value-returning observer exercises the typed fatal-path return while
// per-pass verification is active; it is inlined into the timed process later.
// CHECK: simulation.code_unit.decl {{[0-9]+}} in 1 observer hierarchy "top.$code_unit_9.$observer.91.primary"
// Each elaborated input has an Observed-region sampler gated by the declared
// clocking-event iff from the same selected interface instance.
// CHECK-LABEL: simulation.func private @unit_0.$clocking_input.{{[0-9]+}}
// CHECK: simulation.observer.bind
// CHECK: simulation.observer.bind
// CHECK-SAME: !simulation.net
// CHECK: simulation.suspend.observe
// CHECK-SAME: conditions 1 edges [1] indices [0]
// CHECK-SAME: resume_region = 8 : i32
// CHECK: simulation.assert.sampled_read
// CHECK: simulation.assert.clocked_sample_update
// A clocking output drive is outlined so evaluating the assignment does not
// block its caller; the child waits for the selected conditioned clock event
// and publishes NBA. The condition binds the selected instance's net handle.
// CHECK-LABEL: simulation.func private @unit_0.$clocking_output.36
// CHECK: simulation.observer.bind
// CHECK: simulation.observer.bind
// CHECK-SAME: !simulation.net
// CHECK: simulation.suspend.observe
// CHECK-SAME: conditions 1 edges [1] indices [0]
// CHECK: simulation.nba.enqueue
// A drive issued after @(vif.cb) belongs to the current occurrence and does
// not suspend for another edge.
// CHECK-LABEL: simulation.func private @unit_0.$clocking_output.79
// CHECK-SAME: home_region = 10 : i32
// CHECK-NOT: simulation.suspend.edge
// CHECK: simulation.nba.enqueue
// CHECK-LABEL: simulation.func private @unit_0(
// CHECK-DAG: simulation.string.output_format {{.*}} flags = [32, 256] {{.*}} !simulation.virtual_interface<"@s2.$root::@s5.top::@s9.bus_if", "">
// CHECK-DAG: simulation.virtual_interface.scope
// CHECK-DAG: simulation.context.storage %arg0{{.*}}[[SIGNAL]]
// CHECK-DAG: simulation.context.storage %arg0{{.*}}[[OTHER_SIGNAL]]
// CHECK-DAG: simulation.ref.store
// CHECK-DAG: simulation.ref.load
// Reads select the retained value for the handle's scope instead of
// resampling at each use.
// CHECK: simulation.assert.clocked_sample_read
// CHECK: simulation.spawn @{{.*}}clocking_output
// A virtual clocking-block event selects the clock and every arbitrary iff
// capture from the same interface instance, then resumes in Reactive.
// CHECK: simulation.virtual_interface.scope
// CHECK-COUNT-2: simulation.observer.bind
// CHECK: simulation.suspend.observe
// CHECK-SAME: conditions 1 edges [1] indices [0]
// CHECK-SAME: resume_region = 10 : i32
// CHECK-DAG: virtual interface signal used a null or invalid interface handle.
// CHECK-LABEL: simulation.func private @unit_1(
// CHECK-SAME: entry_kind = 4 : i32
// CHECK-DAG: %[[READY_HANDLE:.*]] = simulation.context.net %arg0{{.*}}[[READY]]
// CHECK-DAG: %[[OTHER_READY_HANDLE:.*]] = simulation.context.net %arg0{{.*}}[[OTHER_READY]]
// The handle itself and both possible member nets participate in implicit
// sensitivity; the final two operands are loop-carried rematerializations.
// CHECK: simulation.suspend.any {{.*}} edges [0, 0, 0]
// CHECK-NOT: obelisk.sv.
// Each possible interface instance maintains its own delayed net mirror. The
// selected instance's conditioned clock sampler reads that mirror.
// INPUT-SKEW-COUNT-2: always hierarchy "unit_0.$clocking_input_delay.
// INPUT-SKEW-LABEL: simulation.func private @unit_0.$clocking_input_delay.{{[0-9]+}}.commit
// INPUT-SKEW: simulation.time.constant 2{{$|[^0-9]}}
// INPUT-SKEW: simulation.suspend.delay
// INPUT-SKEW-SAME: resume_region = 16 : i32
// INPUT-SKEW: simulation.assert.clocked_sample_update
// INPUT-SKEW-LABEL: simulation.func private @unit_0.$clocking_input_delay.{{[0-9]+}}(
// INPUT-SKEW: simulation.assert.sampled_read
// INPUT-SKEW: simulation.suspend.change
// INPUT-SKEW: simulation.net.read
// INPUT-SKEW: simulation.spawn @unit_0.$clocking_input_delay.{{[0-9]+}}.commit
// INPUT-SKEW-LABEL: simulation.func private @unit_0.$clocking_input.{{[0-9]+}}
// INPUT-SKEW: simulation.suspend.observe
// INPUT-SKEW-SAME: conditions 1 edges [1] indices [0]
// INPUT-SKEW: simulation.assert.clocked_sample_read
// INPUT-SKEW: simulation.assert.clocked_sample_update
// INPUT-SKEW-NOT: obelisk.sv.
// INPUT-ZERO: simulation.assert.clocked_sample_update
// INPUT-ZERO: simulation.net.read
// INPUT-EDGE: simulation.suspend.observe
// INPUT-EDGE-SAME: conditions 1 edges [2] indices [0]
// INPUT-EDGE: simulation.assert.clocked_sample_update
// A drive after @(vif.cb) reuses that qualified occurrence, but a distinct
// output edge remains a future synchronization point in Reactive.
// OUTPUT-EDGE-LABEL: simulation.func private @unit_0.$clocking_output.79
// OUTPUT-EDGE-SAME: home_region = 10 : i32
// OUTPUT-EDGE: simulation.suspend.edge negedge
// OUTPUT-EDGE: simulation.nba.enqueue
// Program-domain clocking output helpers preserve Reactive/Program so their
// NBA is committed through the Re-NBA path.
// PROGRAM-LABEL: simulation.func private @unit_0.$clocking_output.79
// PROGRAM-SAME: domain = 1 : i32
// PROGRAM-SAME: home_region = 10 : i32
// PROGRAM: simulation.nba.enqueue
// A net output gets one persistent procedural driver per clocking output and
// elaborated interface instance. All syntactic sites select the shared driver.
// NET-OUTPUT-DAG: hierarchy "top.bus.ready.$clocking_output.top.bus_if.signal" debug "virtual clocking output"
// NET-OUTPUT-DAG: hierarchy "top.other.ready.$clocking_output.top.bus_if.signal" debug "virtual clocking output"
// NET-OUTPUT-LABEL: simulation.func private @unit_0.$clocking_output.36
// NET-OUTPUT-SAME: %{{.*}}: !simulation.driver<!simulation.logic<1>>
// NET-OUTPUT: simulation.nba.enqueue
// NET-OUTPUT-SAME: !simulation.driver<!simulation.logic<1>>
// NET-OUTPUT-LABEL: simulation.func private @unit_0(
// NET-OUTPUT-COUNT-2: simulation.context.driver
// NET-OUTPUT: simulation.spawn @unit_0.$clocking_output.36
// NET-OUTPUT-SAME: !simulation.driver<!simulation.logic<1>>
