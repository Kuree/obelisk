// RUN: obelisk-opt %s '--lower-obelisk-to-sim=opt-level=0' | FileCheck %s

// IEEE 1800-2023 25.9: a virtual interface whose type has no elaborated
// instance can only be null. A member read, a method call and a clocking
// variable read through it lower to the runtime null-handle error rather than
// requiring an instance at compile time.
//
//   interface Bus;
//     logic clk;
//     int data;
//     task t();
//     endtask
//     clocking cb @(posedge clk);
//       input data;
//     endclocking
//   endinterface
//   module top;
//     virtual Bus v;
//     int value;
//     initial if (v != null) value = v.data;
//     initial if (v != null) v.t();
//     initial if (v != null) value = v.cb.data;
//   endmodule

// CHECK-LABEL: simulation.func private @unit_0(
// CHECK: simulation.bytes.constant "FATAL: {{.*}}: virtual interface member access used a null or invalid handle."
// CHECK: simulation.virtual_interface.equal
// CHECK: simulation.fatal
// CHECK-LABEL: simulation.func private @unit_1(
// CHECK: simulation.bytes.constant "FATAL: {{.*}}: virtual interface member access used a null or invalid handle."
// CHECK: simulation.virtual_interface.equal
// CHECK: simulation.fatal
// CHECK-NOT: simulation.call
// CHECK-LABEL: simulation.func private @unit_2(
// CHECK: simulation.bytes.constant "FATAL: {{.*}}: virtual interface member access used a null or invalid handle."
// CHECK: simulation.virtual_interface.equal
// CHECK: simulation.fatal
// CHECK-NOT: obelisk.sv.

module attributes {coverage.language_version = 2023 : i32} {
  obelisk.sv.symbol.definition @s0.Bus attributes {definition_kind = 1 : i32, hierarchical_name = "Bus", name = "Bus", node_id = 0 : i64} {
  }
  obelisk.sv.symbol.definition @s1.top attributes {definition_kind = 0 : i32, hierarchical_name = "top", name = "top", node_id = 1 : i64} {
  }
  obelisk.sv.symbol.root @s2.$root attributes {hierarchical_name = "\\$root ", name = "$root", node_id = 2 : i64} {
    obelisk.sv.symbol.compilation_unit @s3 attributes {hierarchical_name = "$unit", node_id = 3 : i64, simulation.vpi_definition_name = "$unit"} {
    }
    obelisk.sv.symbol.instance @s4.top attributes {hierarchical_name = "top", is_uninstantiated = false, name = "top", node_id = 4 : i64, referenced_path = "top", referenced_symbol = @s1.top} {
      obelisk.sv.symbol.instance_body @s5.top attributes {hierarchical_name = "top", name = "top", node_id = 5 : i64, simulation.vpi_automatic = false, simulation.vpi_cell_instance = false, simulation.vpi_definition_name = "top", simulation.vpi_top = true, time_precision_fs = 1000000 : i64, time_unit_fs = 1000000 : i64, vpi_scope_kind = 32 : i32} {
        obelisk.sv.symbol.variable @s6.v attributes {hierarchical_name = "top.v", lifetime = 1 : i32, name = "v", node_id = 6 : i64, semantic_type = !obelisk.virtual_interface<@s2.$root::@s5.top::@s7.Bus, "">} {
        }
        obelisk.sv.symbol.variable @s8.value attributes {hierarchical_name = "top.value", lifetime = 1 : i32, name = "value", node_id = 7 : i64, semantic_type = !obelisk.integral<32, true, false, 31 : 0, int>} {
        }
        obelisk.sv.symbol.procedural_block @s9 attributes {hierarchical_name = "top", node_id = 8 : i64, procedure_kind = 0 : i32, time_precision_fs = 1000000 : i64, time_unit_fs = 1000000 : i64} {
          obelisk.sv.statement.conditional attributes {check_kind = 0 : i32, condition_count = 1 : i64, condition_pattern_flags = array<i64: 0>, has_else = false, node_id = 9 : i64} {
            obelisk.sv.expression.binary_op attributes {is_signed = false, node_id = 10 : i64, operator_kind = 10 : i32, semantic_type = !obelisk.integral<1, false, false, 0 : 0, bit>} {
              obelisk.sv.expression.named_value attributes {is_signed = false, node_id = 11 : i64, referenced_path = "top.v", referenced_symbol = @s2.$root::@s4.top::@s5.top::@s6.v, semantic_type = !obelisk.virtual_interface<@s2.$root::@s5.top::@s7.Bus, "">} {
              }
              obelisk.sv.expression.null_literal attributes {is_signed = false, node_id = 12 : i64, semantic_type = !obelisk.null} {
              }
            }
            obelisk.sv.statement.expression_statement attributes {node_id = 13 : i64} {
              obelisk.sv.expression.assignment attributes {assignment_kind = 0 : i32, is_signed = true, node_id = 14 : i64, semantic_type = !obelisk.integral<32, true, false, 31 : 0, int>} {
                obelisk.sv.expression.named_value attributes {is_signed = true, node_id = 15 : i64, referenced_path = "top.value", referenced_symbol = @s2.$root::@s4.top::@s5.top::@s8.value, semantic_type = !obelisk.integral<32, true, false, 31 : 0, int>} {
                }
                obelisk.sv.expression.member_access attributes {is_signed = true, member_name = "data", node_id = 16 : i64, referenced_path = "top.Bus.data", referenced_symbol = @s2.$root::@s4.top::@s5.top::@s7.Bus::@s19.Bus::@s21.data, semantic_type = !obelisk.integral<32, true, false, 31 : 0, int>} {
                  obelisk.sv.expression.named_value attributes {is_signed = false, node_id = 17 : i64, referenced_path = "top.v", referenced_symbol = @s2.$root::@s4.top::@s5.top::@s6.v, semantic_type = !obelisk.virtual_interface<@s2.$root::@s5.top::@s7.Bus, "">} {
                  }
                }
              }
            }
          }
        }
        obelisk.sv.symbol.procedural_block @s10 attributes {hierarchical_name = "top", node_id = 18 : i64, procedure_kind = 0 : i32, time_precision_fs = 1000000 : i64, time_unit_fs = 1000000 : i64} {
          obelisk.sv.statement.conditional attributes {check_kind = 0 : i32, condition_count = 1 : i64, condition_pattern_flags = array<i64: 0>, has_else = false, node_id = 19 : i64} {
            obelisk.sv.expression.binary_op attributes {is_signed = false, node_id = 20 : i64, operator_kind = 10 : i32, semantic_type = !obelisk.integral<1, false, false, 0 : 0, bit>} {
              obelisk.sv.expression.named_value attributes {is_signed = false, node_id = 21 : i64, referenced_path = "top.v", referenced_symbol = @s2.$root::@s4.top::@s5.top::@s6.v, semantic_type = !obelisk.virtual_interface<@s2.$root::@s5.top::@s7.Bus, "">} {
              }
              obelisk.sv.expression.null_literal attributes {is_signed = false, node_id = 22 : i64, semantic_type = !obelisk.null} {
              }
            }
            obelisk.sv.statement.expression_statement attributes {node_id = 23 : i64} {
              obelisk.sv.expression.call attributes {argument_count = 0 : i64, callee_name = "t", constraint_restrictions = [], defaulted_arguments = array<i64>, has_inline_constraints = false, has_iterator_expression = false, has_output_arguments = false, has_this_class = true, is_signed = false, is_super_class = false, is_system_call = false, node_id = 24 : i64, referenced_path = "top.Bus.t", referenced_symbol = @s2.$root::@s4.top::@s5.top::@s7.Bus::@s19.Bus::@s22.t, semantic_type = !obelisk.void, subroutine_kind = 1 : i32} {
                obelisk.sv.expression.named_value attributes {is_signed = false, node_id = 25 : i64, referenced_path = "top.v", referenced_symbol = @s2.$root::@s4.top::@s5.top::@s6.v, semantic_type = !obelisk.virtual_interface<@s2.$root::@s5.top::@s7.Bus, "">} {
                }
              }
            }
          }
        }
        obelisk.sv.symbol.procedural_block @s11 attributes {hierarchical_name = "top", node_id = 26 : i64, procedure_kind = 0 : i32, time_precision_fs = 1000000 : i64, time_unit_fs = 1000000 : i64} {
          obelisk.sv.statement.conditional attributes {check_kind = 0 : i32, condition_count = 1 : i64, condition_pattern_flags = array<i64: 0>, has_else = false, node_id = 27 : i64} {
            obelisk.sv.expression.binary_op attributes {is_signed = false, node_id = 28 : i64, operator_kind = 10 : i32, semantic_type = !obelisk.integral<1, false, false, 0 : 0, bit>} {
              obelisk.sv.expression.named_value attributes {is_signed = false, node_id = 29 : i64, referenced_path = "top.v", referenced_symbol = @s2.$root::@s4.top::@s5.top::@s6.v, semantic_type = !obelisk.virtual_interface<@s2.$root::@s5.top::@s7.Bus, "">} {
              }
              obelisk.sv.expression.null_literal attributes {is_signed = false, node_id = 30 : i64, semantic_type = !obelisk.null} {
              }
            }
            obelisk.sv.statement.expression_statement attributes {node_id = 31 : i64} {
              obelisk.sv.expression.assignment attributes {assignment_kind = 0 : i32, is_signed = true, node_id = 32 : i64, semantic_type = !obelisk.integral<32, true, false, 31 : 0, int>} {
                obelisk.sv.expression.named_value attributes {is_signed = true, node_id = 33 : i64, referenced_path = "top.value", referenced_symbol = @s2.$root::@s4.top::@s5.top::@s8.value, semantic_type = !obelisk.integral<32, true, false, 31 : 0, int>} {
                }
                obelisk.sv.expression.member_access attributes {is_signed = true, member_name = "data", node_id = 34 : i64, referenced_path = "top.Bus.cb.data", referenced_symbol = @s2.$root::@s4.top::@s5.top::@s7.Bus::@s19.Bus::@s23.cb::@s24.data, semantic_type = !obelisk.integral<32, true, false, 31 : 0, int>, virtual_interface_access_direction = 0 : i32, virtual_interface_clock_event_edge = 1 : i32, virtual_interface_clock_input_skew_edge = 0 : i32, virtual_interface_clock_input_skew_one_step, virtual_interface_clock_member = "clk", virtual_interface_clock_output_skew_delay = "0", virtual_interface_clock_output_skew_edge = 0 : i32, virtual_interface_clock_time_precision_fs = 1000000 : i64, virtual_interface_clock_time_unit_fs = 1000000 : i64, virtual_interface_clocking, virtual_interface_clocking_block = "cb", virtual_interface_clocking_signal_member = "data"} {
                  obelisk.sv.expression.named_value attributes {is_signed = false, node_id = 35 : i64, referenced_path = "top.v", referenced_symbol = @s2.$root::@s4.top::@s5.top::@s6.v, semantic_type = !obelisk.virtual_interface<@s2.$root::@s5.top::@s7.Bus, "">} {
                  }
                }
              }
            }
          }
        }
        obelisk.sv.symbol.instance @s7.Bus attributes {hierarchical_name = "top.Bus", is_uninstantiated = false, is_virtual_interface_type_instance = true, name = "Bus", node_id = 47 : i64, referenced_path = "Bus", referenced_symbol = @s0.Bus} {
          obelisk.sv.symbol.instance_body @s19.Bus attributes {hierarchical_name = "top.Bus", is_virtual_interface_type_instance = true, name = "Bus", node_id = 48 : i64, simulation.vpi_automatic = false, simulation.vpi_cell_instance = false, simulation.vpi_definition_name = "Bus", simulation.vpi_top = false, time_precision_fs = 1000000 : i64, time_unit_fs = 1000000 : i64, virtual_interface_identity = @s2.$root::@s5.top::@s7.Bus, vpi_scope_kind = 601 : i32} {
            obelisk.sv.symbol.variable @s20.clk attributes {hierarchical_name = "top.Bus.clk", lifetime = 1 : i32, name = "clk", node_id = 49 : i64, semantic_type = !obelisk.integral<1, false, true, 0 : 0, logic>} {
            }
            obelisk.sv.symbol.variable @s21.data attributes {hierarchical_name = "top.Bus.data", lifetime = 1 : i32, name = "data", node_id = 50 : i64, semantic_type = !obelisk.integral<32, true, false, 31 : 0, int>} {
            }
            obelisk.sv.symbol.subroutine @s22.t attributes {default_lifetime = 1 : i32, hierarchical_name = "top.Bus.t", name = "t", node_id = 51 : i64, semantic_type = !obelisk.subroutine<() -> (), true>, subroutine_kind = 1 : i32, time_precision_fs = 1000000 : i64, time_unit_fs = 1000000 : i64} {
              obelisk.sv.statement.list attributes {node_id = 52 : i64} {
              }
            }
            obelisk.sv.symbol.clocking_block @s23.cb attributes {hierarchical_name = "top.Bus.cb", is_default = false, is_global = false, name = "cb", node_id = 53 : i64} {
              obelisk.sv.timing.signal_event attributes {edge_kind = 1 : i32, has_iff = false, node_id = 54 : i64} {
                obelisk.sv.expression.named_value attributes {is_signed = false, node_id = 55 : i64, referenced_path = "top.Bus.clk", referenced_symbol = @s2.$root::@s4.top::@s5.top::@s7.Bus::@s19.Bus::@s20.clk, semantic_type = !obelisk.integral<1, false, true, 0 : 0, logic>} {
                }
              }
              obelisk.sv.symbol.clock_var @s24.data attributes {direction = 0 : i32, has_input_delay = false, has_output_delay = false, hierarchical_name = "top.Bus.cb.data", input_edge = 0 : i32, lifetime = 1 : i32, name = "data", node_id = 56 : i64, output_edge = 0 : i32, semantic_type = !obelisk.integral<32, true, false, 31 : 0, int>} {
                obelisk.sv.expression.named_value attributes {is_signed = true, node_id = 57 : i64, referenced_path = "top.Bus.data", referenced_symbol = @s2.$root::@s4.top::@s5.top::@s7.Bus::@s19.Bus::@s21.data, semantic_type = !obelisk.integral<32, true, false, 31 : 0, int>} {
                }
              }
            }
          }
        }
      }
    }
  }
}

