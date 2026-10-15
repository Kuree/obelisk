// RUN: obelisk-opt %s '--lower-obelisk-to-sim=opt-level=0' | FileCheck %s
// RUN: not obelisk-opt %s '--test-obelisk-edit-attributes=match={name = "z"} set={is_checker_free_variable}' '--lower-obelisk-to-sim=opt-level=0' 2>&1 | FileCheck %s --check-prefix=FREE
// RUN: not obelisk-opt %s '--test-obelisk-edit-attributes=match={is_procedural = false} set={is_procedural = true}' '--lower-obelisk-to-sim=opt-level=0' 2>&1 | FileCheck %s --check-prefix=PROCEDURAL

// Source:
//   checker chk(logic clk, logic rst, logic a);
//     logic z = 0;
//     always_ff @(posedge clk or posedge rst) z <= rst ? a : !a;
//   endchecker
//   module m; logic clk, rst, a; chk u(clk, rst, a); endmodule
//
// IEEE 1800-2023 17.7.3: a static checker's procedures run in the Reactive
// region (10), so their nonblocking updates commit in Re-NBA. 17.5: its
// always_ff reads sampled values, except for the variables its event control
// names: `rst` is read directly and `a` through its sampled value.
// CHECK-LABEL: simulation.func private @unit_1(
// CHECK-SAME: %arg1: !simulation.ref<!simulation.logic<1>>
// CHECK-SAME: %arg2: !simulation.ref<!simulation.logic<1>>
// CHECK-SAME: %arg3: !simulation.ref<!simulation.logic<1>>
// CHECK-SAME: entry_kind = 5
// CHECK-SAME: home_region = 10
// CHECK-SAME: simulation.checker_procedure
// CHECK: simulation.suspend.any %arg1, %arg2 edges [1, 1]
// CHECK: simulation.ref.load %arg2
// CHECK: simulation.assert.sampled_read %arg0 from %arg3
// CHECK-NOT: simulation.ref.load %arg3

// FREE: error: IEEE 1800-2023 17.7 free checker variables are not executable yet
// PROCEDURAL: error: IEEE 1800-2023 17.3 procedural checker instances are not executable yet

module attributes {coverage.language_version = 2023 : i32} {
  obelisk.sv.symbol.definition @s0.m attributes {definition_kind = 0 : i32, hierarchical_name = "m", name = "m", node_id = 0 : i64} {
  }
  obelisk.sv.symbol.root @s1.$root attributes {hierarchical_name = "\\$root ", name = "$root", node_id = 1 : i64} {
    obelisk.sv.symbol.compilation_unit @s2 attributes {hierarchical_name = "$unit", node_id = 2 : i64, simulation.vpi_definition_name = "$unit"} {
      obelisk.sv.symbol.checker @s3.chk attributes {hierarchical_name = "chk", name = "chk", node_id = 3 : i64, port_count = 3 : i64, port_paths = ["chk.clk", "chk.rst", "chk.a"], port_symbols = [@s1.$root::@s2::@s3.chk::@s4.clk, @s1.$root::@s2::@s3.chk::@s5.rst, @s1.$root::@s2::@s3.chk::@s6.a]} {
        obelisk.sv.symbol.assertion_port @s4.clk attributes {direction = 0 : i32, has_default_value = false, hierarchical_name = "chk.clk", is_local_variable = true, name = "clk", node_id = 4 : i64, semantic_type = !obelisk.integral<1, false, true, 0 : 0, logic>} {
        }
        obelisk.sv.symbol.assertion_port @s5.rst attributes {direction = 0 : i32, has_default_value = false, hierarchical_name = "chk.rst", is_local_variable = true, name = "rst", node_id = 5 : i64, semantic_type = !obelisk.integral<1, false, true, 0 : 0, logic>} {
        }
        obelisk.sv.symbol.assertion_port @s6.a attributes {direction = 0 : i32, has_default_value = false, hierarchical_name = "chk.a", is_local_variable = true, name = "a", node_id = 6 : i64, semantic_type = !obelisk.integral<1, false, true, 0 : 0, logic>} {
        }
      }
    }
    obelisk.sv.symbol.instance @s7.m attributes {hierarchical_name = "m", is_uninstantiated = false, name = "m", node_id = 7 : i64, referenced_path = "m", referenced_symbol = @s0.m} {
      obelisk.sv.symbol.instance_body @s8.m attributes {hierarchical_name = "m", name = "m", node_id = 8 : i64, simulation.vpi_automatic = false, simulation.vpi_cell_instance = false, simulation.vpi_definition_name = "m", simulation.vpi_top = true, time_precision_fs = 1000000 : i64, time_unit_fs = 1000000 : i64, vpi_scope_kind = 32 : i32} {
        obelisk.sv.symbol.variable @s9.clk attributes {hierarchical_name = "m.clk", lifetime = 1 : i32, name = "clk", node_id = 9 : i64, semantic_type = !obelisk.integral<1, false, true, 0 : 0, logic>} {
        }
        obelisk.sv.symbol.variable @s10.rst attributes {hierarchical_name = "m.rst", lifetime = 1 : i32, name = "rst", node_id = 10 : i64, semantic_type = !obelisk.integral<1, false, true, 0 : 0, logic>} {
        }
        obelisk.sv.symbol.variable @s11.a attributes {hierarchical_name = "m.a", lifetime = 1 : i32, name = "a", node_id = 11 : i64, semantic_type = !obelisk.integral<1, false, true, 0 : 0, logic>} {
        }
        obelisk.sv.symbol.checker_instance @s12.u attributes {connection_actual_kinds = array<i64: 0, 0, 0>, connection_attribute_counts = array<i64: 0, 0, 0>, connection_count = 3 : i64, connection_formal_paths = ["m.u.clk", "m.u.rst", "m.u.a"], connection_formal_symbols = [@s1.$root::@s7.m::@s8.m::@s12.u::@s14.chk::@s13.clk, @s1.$root::@s7.m::@s8.m::@s12.u::@s14.chk::@s15.rst, @s1.$root::@s7.m::@s8.m::@s12.u::@s14.chk::@s16.a], connection_has_actual = array<i64: 1, 1, 1>, connection_has_output_initial = array<i64: 0, 0, 0>, hierarchical_name = "m.u", is_procedural = false, name = "u", node_id = 12 : i64, referenced_checker_path = "chk", referenced_checker_symbol = @s1.$root::@s2::@s3.chk} {
          obelisk.sv.symbol.checker_instance_body @s14.chk attributes {hierarchical_name = "m.u", instance_depth = 0 : i64, instance_flags = 0 : i64, is_procedural = false, name = "chk", node_id = 13 : i64, parent_instance_path = "m.u", parent_instance_symbol = @s1.$root::@s7.m::@s8.m::@s12.u, referenced_checker_path = "chk", referenced_checker_symbol = @s1.$root::@s2::@s3.chk} {
            obelisk.sv.symbol.assertion_port @s13.clk attributes {direction = 0 : i32, has_default_value = false, hierarchical_name = "m.u.clk", is_local_variable = true, name = "clk", node_id = 14 : i64, semantic_type = !obelisk.integral<1, false, true, 0 : 0, logic>} {
            }
            obelisk.sv.symbol.assertion_port @s15.rst attributes {direction = 0 : i32, has_default_value = false, hierarchical_name = "m.u.rst", is_local_variable = true, name = "rst", node_id = 15 : i64, semantic_type = !obelisk.integral<1, false, true, 0 : 0, logic>} {
            }
            obelisk.sv.symbol.assertion_port @s16.a attributes {direction = 0 : i32, has_default_value = false, hierarchical_name = "m.u.a", is_local_variable = true, name = "a", node_id = 16 : i64, semantic_type = !obelisk.integral<1, false, true, 0 : 0, logic>} {
            }
            obelisk.sv.symbol.variable @s17.z attributes {hierarchical_name = "m.u.z", lifetime = 1 : i32, name = "z", node_id = 17 : i64, semantic_type = !obelisk.integral<1, false, true, 0 : 0, logic>} {
              obelisk.sv.expression.conversion attributes {folded_constant = "1'b0", is_implicit = true, is_signed = false, node_id = 18 : i64, semantic_type = !obelisk.integral<1, false, true, 0 : 0, logic>} {
                obelisk.sv.expression.conversion attributes {folded_constant = "0", is_implicit = true, is_signed = true, node_id = 19 : i64, semantic_type = !obelisk.ranged_packed_array<31 : 0 x !obelisk.integral<1, true, true, 0 : 0, logic>>} {
                  obelisk.sv.expression.integer_literal attributes {constant_value = "0", is_declared_unsized = true, is_signed = true, node_id = 20 : i64, semantic_type = !obelisk.integral<32, true, false, 31 : 0, int>} {
                  }
                }
              }
            }
            obelisk.sv.symbol.procedural_block @s18 attributes {hierarchical_name = "m.u", node_id = 21 : i64, procedure_kind = 5 : i32, time_precision_fs = 1000000 : i64, time_unit_fs = 1000000 : i64} {
              obelisk.sv.statement.timed attributes {node_id = 22 : i64} {
                obelisk.sv.timing.event_list attributes {event_count = 2 : i64, node_id = 23 : i64} {
                  obelisk.sv.timing.signal_event attributes {edge_kind = 1 : i32, has_iff = false, node_id = 24 : i64} {
                    obelisk.sv.expression.named_value attributes {is_signed = false, node_id = 25 : i64, referenced_path = "m.clk", referenced_symbol = @s1.$root::@s7.m::@s8.m::@s9.clk, semantic_type = !obelisk.integral<1, false, true, 0 : 0, logic>} {
                    }
                  }
                  obelisk.sv.timing.signal_event attributes {edge_kind = 1 : i32, has_iff = false, node_id = 26 : i64} {
                    obelisk.sv.expression.named_value attributes {is_signed = false, node_id = 27 : i64, referenced_path = "m.rst", referenced_symbol = @s1.$root::@s7.m::@s8.m::@s10.rst, semantic_type = !obelisk.integral<1, false, true, 0 : 0, logic>} {
                    }
                  }
                }
                obelisk.sv.statement.expression_statement attributes {node_id = 28 : i64} {
                  obelisk.sv.expression.assignment attributes {assignment_kind = 1 : i32, is_signed = false, node_id = 29 : i64, semantic_type = !obelisk.integral<1, false, true, 0 : 0, logic>} {
                    obelisk.sv.expression.named_value attributes {is_signed = false, node_id = 30 : i64, referenced_path = "m.u.z", referenced_symbol = @s1.$root::@s7.m::@s8.m::@s12.u::@s14.chk::@s17.z, semantic_type = !obelisk.integral<1, false, true, 0 : 0, logic>} {
                    }
                    obelisk.sv.expression.conditional_op attributes {condition_count = 1 : i64, condition_pattern_flags = array<i64: 0>, is_signed = false, node_id = 31 : i64, semantic_type = !obelisk.integral<1, false, true, 0 : 0, logic>} {
                      obelisk.sv.expression.named_value attributes {is_signed = false, node_id = 32 : i64, referenced_path = "m.rst", referenced_symbol = @s1.$root::@s7.m::@s8.m::@s10.rst, semantic_type = !obelisk.integral<1, false, true, 0 : 0, logic>} {
                      }
                      obelisk.sv.expression.named_value attributes {is_signed = false, node_id = 33 : i64, referenced_path = "m.a", referenced_symbol = @s1.$root::@s7.m::@s8.m::@s11.a, semantic_type = !obelisk.integral<1, false, true, 0 : 0, logic>} {
                      }
                      obelisk.sv.expression.unary_op attributes {is_signed = false, node_id = 34 : i64, operator_kind = 9 : i32, semantic_type = !obelisk.integral<1, false, true, 0 : 0, logic>} {
                        obelisk.sv.expression.named_value attributes {is_signed = false, node_id = 35 : i64, referenced_path = "m.a", referenced_symbol = @s1.$root::@s7.m::@s8.m::@s11.a, semantic_type = !obelisk.integral<1, false, true, 0 : 0, logic>} {
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

