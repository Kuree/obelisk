// RUN: obelisk-opt %s --split-input-file --verify-diagnostics \
// RUN:   '--lower-obelisk-to-sim=opt-level=0' | FileCheck %s

// IEEE 1800-2023 9.4.2.4: a sequence in an event control is instantiated as
// if by an assert property statement, so an instance with actual arguments
// gets its own endpoint monitor over its substituted body. 9.4.2.3: `iff`
// qualifies the waiter, which resumes only at an endpoint where it holds.
//
// module top;
//   bit clk, go;
//   sequence s(bit flag); @(posedge clk) flag; endsequence
//   initial @(s(1) iff go);
// endmodule

module attributes {coverage.language_version = 2023 : i32} {
  obelisk.sv.symbol.definition @s0.top attributes {definition_kind = 0 : i32, hierarchical_name = "top", name = "top", node_id = 0 : i64} {
  }
  obelisk.sv.symbol.root @s1.$root attributes {hierarchical_name = "\\$root ", name = "$root", node_id = 1 : i64} {
    obelisk.sv.symbol.compilation_unit @s2 attributes {hierarchical_name = "$unit", node_id = 2 : i64, simulation.vpi_definition_name = "$unit"} {
    }
    obelisk.sv.symbol.instance @s3.top attributes {hierarchical_name = "top", is_uninstantiated = false, name = "top", node_id = 3 : i64, referenced_path = "top", referenced_symbol = @s0.top} {
      obelisk.sv.symbol.instance_body @s4.top attributes {hierarchical_name = "top", name = "top", node_id = 4 : i64, simulation.vpi_automatic = false, simulation.vpi_cell_instance = false, simulation.vpi_definition_name = "top", simulation.vpi_top = true, time_precision_fs = 1000000 : i64, time_unit_fs = 1000000 : i64, vpi_scope_kind = 32 : i32} {
        obelisk.sv.symbol.variable @s5.clk attributes {hierarchical_name = "top.clk", lifetime = 1 : i32, name = "clk", node_id = 5 : i64, semantic_type = !obelisk.integral<1, false, false, 0 : 0, bit>} {
        }
        obelisk.sv.symbol.variable @s6.go attributes {hierarchical_name = "top.go", lifetime = 1 : i32, name = "go", node_id = 6 : i64, semantic_type = !obelisk.integral<1, false, false, 0 : 0, bit>} {
        }
        obelisk.sv.symbol.sequence @s7.s attributes {has_default_instance = false, hierarchical_name = "top.s", name = "s", node_id = 7 : i64, port_count = 1 : i64, port_paths = ["top.s.flag"], port_symbols = [@s1.$root::@s3.top::@s4.top::@s7.s::@s8.flag]} {
          obelisk.sv.symbol.assertion_port @s8.flag attributes {has_default_value = false, hierarchical_name = "top.s.flag", is_local_variable = false, name = "flag", node_id = 8 : i64, semantic_type = !obelisk.integral<1, false, false, 0 : 0, bit>} {
          }
        }
        obelisk.sv.symbol.procedural_block @s9 attributes {hierarchical_name = "top", node_id = 9 : i64, procedure_kind = 0 : i32, time_precision_fs = 1000000 : i64, time_unit_fs = 1000000 : i64} {
          obelisk.sv.statement.timed attributes {node_id = 10 : i64} {
            obelisk.sv.timing.signal_event attributes {edge_kind = 0 : i32, has_iff = true, node_id = 11 : i64} {
              obelisk.sv.expression.assertion_instance attributes {argument_count = 1 : i64, argument_formal_paths = ["top.s.flag"], argument_formal_symbols = [@s1.$root::@s3.top::@s4.top::@s7.s::@s8.flag], argument_kinds = array<i64: 0>, has_expanded_body = true, is_recursive_property = false, is_signed = false, local_variable_count = 0 : i64, local_variable_has_initializer = array<i64>, local_variable_paths = [], local_variable_symbols = [], node_id = 12 : i64, referenced_path = "top.s", referenced_symbol = @s1.$root::@s3.top::@s4.top::@s7.s, semantic_type = !obelisk.sequence} {
                obelisk.sv.assertion.clocking attributes {node_id = 13 : i64} {
                  obelisk.sv.timing.signal_event attributes {edge_kind = 1 : i32, has_iff = false, node_id = 14 : i64} {
                    obelisk.sv.expression.named_value attributes {is_signed = false, node_id = 15 : i64, referenced_path = "top.clk", referenced_symbol = @s1.$root::@s3.top::@s4.top::@s5.clk, semantic_type = !obelisk.integral<1, false, false, 0 : 0, bit>} {
                    }
                  }
                  obelisk.sv.assertion.simple attributes {has_repetition = false, is_null = false, node_id = 16 : i64, repetition_is_unbounded = false} {
                    obelisk.sv.expression.conversion attributes {folded_constant = "1'b1", is_implicit = false, is_signed = false, node_id = 17 : i64, semantic_type = !obelisk.integral<1, false, false, 0 : 0, bit>} {
                      obelisk.sv.expression.integer_literal attributes {constant_value = "1", is_declared_unsized = true, is_signed = true, node_id = 18 : i64, semantic_type = !obelisk.integral<32, true, false, 31 : 0, int>} {
                      }
                    }
                  }
                }
                obelisk.sv.expression.integer_literal attributes {constant_value = "1", is_declared_unsized = true, is_signed = true, node_id = 19 : i64, semantic_type = !obelisk.integral<32, true, false, 31 : 0, int>} {
                }
              }
              obelisk.sv.expression.named_value attributes {is_signed = false, node_id = 20 : i64, referenced_path = "top.go", referenced_symbol = @s1.$root::@s3.top::@s4.top::@s6.go, semantic_type = !obelisk.integral<1, false, false, 0 : 0, bit>} {
              }
            }
            obelisk.sv.statement.empty attributes {node_id = 21 : i64} {
            }
          }
        }
      }
    }
  }
}

// CHECK: simulation.code_unit.decl {{[0-9]+}} in 1 always hierarchy "top.s.$event12.$sequence_endpoint"
// CHECK-LABEL: simulation.func private @unit_0(
// CHECK-SAME: %[[CLOCK:[a-z0-9]+]]: !simulation.ref<i1>
// CHECK-SAME: %[[ENDPOINT:[a-z0-9]+]]: !simulation.event
// CHECK-SAME: simulation.hierarchical_name = "top.s.$event12"
// CHECK: simulation.suspend.edge posedge %[[CLOCK]]
// CHECK: simulation.event.trigger %[[ENDPOINT]]

// CHECK-LABEL: simulation.func private @unit_1(
// CHECK-SAME: %[[GO:[a-z0-9]+]]: !simulation.ref<i1>
// CHECK-SAME: %[[WAITED:[a-z0-9]+]]: !simulation.event
// CHECK: ^[[WAIT:bb[0-9]+]]:  // 2 preds
// CHECK-NEXT: simulation.suspend.event %[[WAITED]] to ^[[CHECK:bb[0-9]+]]
// CHECK-NEXT: ^[[CHECK]]:
// CHECK-NEXT: %[[QUALIFIED:.*]] = simulation.ref.load %[[GO]]
// CHECK: cf.cond_br %[[QUALIFIED]], ^{{bb[0-9]+}}, ^[[WAIT]]

// -----

// Arguments of a sequence event control shall be static (9.4.2.4).
//
// module top;
//   bit clk;
//   sequence s(bit flag); @(posedge clk) flag; endsequence
//   initial begin
//     automatic bit x = 1;
//     @(s(x));
//   end
// endmodule

module attributes {coverage.language_version = 2023 : i32} {
  obelisk.sv.symbol.definition @s0.top attributes {definition_kind = 0 : i32, hierarchical_name = "top", name = "top", node_id = 0 : i64} {
  }
  obelisk.sv.symbol.root @s1.$root attributes {hierarchical_name = "\\$root ", name = "$root", node_id = 1 : i64} {
    obelisk.sv.symbol.compilation_unit @s2 attributes {hierarchical_name = "$unit", node_id = 2 : i64, simulation.vpi_definition_name = "$unit"} {
    }
    obelisk.sv.symbol.instance @s3.top attributes {hierarchical_name = "top", is_uninstantiated = false, name = "top", node_id = 3 : i64, referenced_path = "top", referenced_symbol = @s0.top} {
      obelisk.sv.symbol.instance_body @s4.top attributes {hierarchical_name = "top", name = "top", node_id = 4 : i64, simulation.vpi_automatic = false, simulation.vpi_cell_instance = false, simulation.vpi_definition_name = "top", simulation.vpi_top = true, time_precision_fs = 1000000 : i64, time_unit_fs = 1000000 : i64, vpi_scope_kind = 32 : i32} {
        obelisk.sv.symbol.variable @s5.clk attributes {hierarchical_name = "top.clk", lifetime = 1 : i32, name = "clk", node_id = 5 : i64, semantic_type = !obelisk.integral<1, false, false, 0 : 0, bit>} {
        }
        obelisk.sv.symbol.sequence @s6.s attributes {has_default_instance = false, hierarchical_name = "top.s", name = "s", node_id = 6 : i64, port_count = 1 : i64, port_paths = ["top.s.flag"], port_symbols = [@s1.$root::@s3.top::@s4.top::@s6.s::@s7.flag]} {
          obelisk.sv.symbol.assertion_port @s7.flag attributes {has_default_value = false, hierarchical_name = "top.s.flag", is_local_variable = false, name = "flag", node_id = 7 : i64, semantic_type = !obelisk.integral<1, false, false, 0 : 0, bit>} {
          }
        }
        obelisk.sv.symbol.statement_block @s8 attributes {block_kind = 0 : i32, hierarchical_name = "top", node_id = 8 : i64} {
          obelisk.sv.symbol.variable @s9.x attributes {hierarchical_name = "top.x", name = "x", node_id = 9 : i64, semantic_type = !obelisk.integral<1, false, false, 0 : 0, bit>} {
            obelisk.sv.expression.conversion attributes {folded_constant = "1'b1", is_implicit = true, is_signed = false, node_id = 10 : i64, semantic_type = !obelisk.integral<1, false, false, 0 : 0, bit>} {
              obelisk.sv.expression.integer_literal attributes {constant_value = "1", is_declared_unsized = true, is_signed = true, node_id = 11 : i64, semantic_type = !obelisk.integral<32, true, false, 31 : 0, int>} {
              }
            }
          }
        }
        obelisk.sv.symbol.procedural_block @s10 attributes {hierarchical_name = "top", node_id = 12 : i64, procedure_kind = 0 : i32, time_precision_fs = 1000000 : i64, time_unit_fs = 1000000 : i64} {
          obelisk.sv.statement.block attributes {node_id = 13 : i64} {
            obelisk.sv.statement.list attributes {node_id = 14 : i64} {
              obelisk.sv.statement.variable_declaration attributes {node_id = 15 : i64, referenced_path = "top.x", referenced_symbol = @s1.$root::@s3.top::@s4.top::@s8::@s9.x} {
              }
              obelisk.sv.statement.timed attributes {node_id = 16 : i64} {
                obelisk.sv.timing.signal_event attributes {edge_kind = 0 : i32, has_iff = false, node_id = 17 : i64} {
                  // expected-error@+1 {{arguments of a sequence event control shall be static}}
                  obelisk.sv.expression.assertion_instance attributes {argument_count = 1 : i64, argument_formal_paths = ["top.s.flag"], argument_formal_symbols = [@s1.$root::@s3.top::@s4.top::@s6.s::@s7.flag], argument_kinds = array<i64: 0>, has_expanded_body = true, is_recursive_property = false, is_signed = false, local_variable_count = 0 : i64, local_variable_has_initializer = array<i64>, local_variable_paths = [], local_variable_symbols = [], node_id = 18 : i64, referenced_path = "top.s", referenced_symbol = @s1.$root::@s3.top::@s4.top::@s6.s, semantic_type = !obelisk.sequence} {
                    obelisk.sv.assertion.clocking attributes {node_id = 19 : i64} {
                      obelisk.sv.timing.signal_event attributes {edge_kind = 1 : i32, has_iff = false, node_id = 20 : i64} {
                        obelisk.sv.expression.named_value attributes {is_signed = false, node_id = 21 : i64, referenced_path = "top.clk", referenced_symbol = @s1.$root::@s3.top::@s4.top::@s5.clk, semantic_type = !obelisk.integral<1, false, false, 0 : 0, bit>} {
                        }
                      }
                      obelisk.sv.assertion.simple attributes {has_repetition = false, is_null = false, node_id = 22 : i64, repetition_is_unbounded = false} {
                        obelisk.sv.expression.named_value attributes {is_signed = false, node_id = 23 : i64, referenced_path = "top.x", referenced_symbol = @s1.$root::@s3.top::@s4.top::@s8::@s9.x, semantic_type = !obelisk.integral<1, false, false, 0 : 0, bit>} {
                        }
                      }
                    }
                    obelisk.sv.expression.named_value attributes {is_signed = false, node_id = 24 : i64, referenced_path = "top.x", referenced_symbol = @s1.$root::@s3.top::@s4.top::@s8::@s9.x, semantic_type = !obelisk.integral<1, false, false, 0 : 0, bit>} {
                    }
                  }
                }
                obelisk.sv.statement.empty attributes {node_id = 25 : i64} {
                }
              }
            }
          }
        }
      }
    }
  }
}
