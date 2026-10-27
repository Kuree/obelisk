// RUN: talos-opt %s --verify-diagnostics '--lower-talos-to-sim=opt-level=0' \
// RUN:   | FileCheck %s

// IEEE 1800-2023 16.13.6 and 9.4.4: `s.triggered` reads the endpoint of the
// sequence's monitor, so `wait (s.triggered)` is a level wait on it. 9.4.2.1:
// a sequence and a named event can share an event `or` list.
//
// module top;
//   bit clk;
//   event e;
//   sequence s; @(posedge clk) 1; endsequence
//   initial wait (s.triggered);
//   initial @(s or e);
// endmodule

module attributes {coverage.language_version = 2023 : i32} {
  talos.sv.symbol.definition @s0.top attributes {definition_kind = 0 : i32, hierarchical_name = "top", name = "top", node_id = 0 : i64} {
  }
  talos.sv.symbol.root @s1.$root attributes {hierarchical_name = "\\$root ", name = "$root", node_id = 1 : i64} {
    talos.sv.symbol.compilation_unit @s2 attributes {hierarchical_name = "$unit", node_id = 2 : i64, simulation.vpi_definition_name = "$unit"} {
    }
    talos.sv.symbol.instance @s3.top attributes {hierarchical_name = "top", is_uninstantiated = false, name = "top", node_id = 3 : i64, referenced_path = "top", referenced_symbol = @s0.top} {
      talos.sv.symbol.instance_body @s4.top attributes {hierarchical_name = "top", name = "top", node_id = 4 : i64, simulation.vpi_automatic = false, simulation.vpi_cell_instance = false, simulation.vpi_definition_name = "top", simulation.vpi_top = true, time_precision_fs = 1000000 : i64, time_unit_fs = 1000000 : i64, vpi_scope_kind = 32 : i32} {
        talos.sv.symbol.variable @s5.clk attributes {hierarchical_name = "top.clk", lifetime = 1 : i32, name = "clk", node_id = 5 : i64, semantic_type = !talos.integral<1, false, false, 0 : 0, bit>} {
        }
        talos.sv.symbol.variable @s6.e attributes {hierarchical_name = "top.e", lifetime = 1 : i32, name = "e", node_id = 6 : i64, semantic_type = !talos.event} {
        }
        talos.sv.symbol.sequence @s7.s attributes {has_default_instance = true, hierarchical_name = "top.s", name = "s", node_id = 7 : i64, port_count = 0 : i64, port_paths = [], port_symbols = []} {
          talos.sv.expression.assertion_instance attributes {argument_count = 0 : i64, argument_formal_paths = [], argument_formal_symbols = [], argument_kinds = array<i64>, has_expanded_body = true, is_recursive_property = false, is_signed = false, local_variable_count = 0 : i64, local_variable_has_initializer = array<i64>, local_variable_paths = [], local_variable_symbols = [], node_id = 8 : i64, referenced_path = "top.s", referenced_symbol = @s1.$root::@s3.top::@s4.top::@s7.s, semantic_type = !talos.sequence} {
            talos.sv.assertion.clocking attributes {node_id = 9 : i64} {
              talos.sv.timing.signal_event attributes {edge_kind = 1 : i32, has_iff = false, node_id = 10 : i64} {
                talos.sv.expression.named_value attributes {is_signed = false, node_id = 11 : i64, referenced_path = "top.clk", referenced_symbol = @s1.$root::@s3.top::@s4.top::@s5.clk, semantic_type = !talos.integral<1, false, false, 0 : 0, bit>} {
                }
              }
              talos.sv.assertion.simple attributes {has_repetition = false, is_null = false, node_id = 12 : i64, repetition_is_unbounded = false} {
                talos.sv.expression.integer_literal attributes {constant_value = "1", is_declared_unsized = true, is_signed = true, node_id = 13 : i64, semantic_type = !talos.integral<32, true, false, 31 : 0, int>} {
                }
              }
            }
          }
        }
        talos.sv.symbol.procedural_block @s8 attributes {hierarchical_name = "top", node_id = 14 : i64, procedure_kind = 0 : i32, time_precision_fs = 1000000 : i64, time_unit_fs = 1000000 : i64} {
          talos.sv.statement.wait attributes {node_id = 15 : i64} {
            talos.sv.expression.call attributes {argument_count = 1 : i64, callee_name = "triggered", constraint_restrictions = [], defaulted_arguments = array<i64: 0>, has_inline_constraints = false, has_iterator_expression = false, has_output_arguments = false, has_this_class = false, is_signed = false, is_super_class = false, is_system_call = true, node_id = 16 : i64, semantic_type = !talos.integral<1, false, false, 0 : 0, bit>, subroutine_kind = 0 : i32, system_library_cell = "work.top", system_scope_path = "top", system_scope_symbol = @s1.$root::@s3.top::@s4.top} {
              talos.sv.expression.assertion_instance attributes {argument_count = 0 : i64, argument_formal_paths = [], argument_formal_symbols = [], argument_kinds = array<i64>, has_expanded_body = true, is_recursive_property = false, is_signed = false, local_variable_count = 0 : i64, local_variable_has_initializer = array<i64>, local_variable_paths = [], local_variable_symbols = [], node_id = 17 : i64, referenced_path = "top.s", referenced_symbol = @s1.$root::@s3.top::@s4.top::@s7.s, semantic_type = !talos.sequence} {
                talos.sv.assertion.clocking attributes {node_id = 18 : i64} {
                  talos.sv.timing.signal_event attributes {edge_kind = 1 : i32, has_iff = false, node_id = 19 : i64} {
                    talos.sv.expression.named_value attributes {is_signed = false, node_id = 20 : i64, referenced_path = "top.clk", referenced_symbol = @s1.$root::@s3.top::@s4.top::@s5.clk, semantic_type = !talos.integral<1, false, false, 0 : 0, bit>} {
                    }
                  }
                  talos.sv.assertion.simple attributes {has_repetition = false, is_null = false, node_id = 21 : i64, repetition_is_unbounded = false} {
                    talos.sv.expression.integer_literal attributes {constant_value = "1", is_declared_unsized = true, is_signed = true, node_id = 22 : i64, semantic_type = !talos.integral<32, true, false, 31 : 0, int>} {
                    }
                  }
                }
              }
            }
            talos.sv.statement.empty attributes {node_id = 23 : i64} {
            }
          }
        }
        talos.sv.symbol.procedural_block @s9 attributes {hierarchical_name = "top", node_id = 24 : i64, procedure_kind = 0 : i32, time_precision_fs = 1000000 : i64, time_unit_fs = 1000000 : i64} {
          talos.sv.statement.timed attributes {node_id = 25 : i64} {
            talos.sv.timing.event_list attributes {event_count = 2 : i64, node_id = 26 : i64} {
              talos.sv.timing.signal_event attributes {edge_kind = 0 : i32, has_iff = false, node_id = 27 : i64} {
                talos.sv.expression.assertion_instance attributes {argument_count = 0 : i64, argument_formal_paths = [], argument_formal_symbols = [], argument_kinds = array<i64>, has_expanded_body = true, is_recursive_property = false, is_signed = false, local_variable_count = 0 : i64, local_variable_has_initializer = array<i64>, local_variable_paths = [], local_variable_symbols = [], node_id = 28 : i64, referenced_path = "top.s", referenced_symbol = @s1.$root::@s3.top::@s4.top::@s7.s, semantic_type = !talos.sequence} {
                  talos.sv.assertion.clocking attributes {node_id = 29 : i64} {
                    talos.sv.timing.signal_event attributes {edge_kind = 1 : i32, has_iff = false, node_id = 30 : i64} {
                      talos.sv.expression.named_value attributes {is_signed = false, node_id = 31 : i64, referenced_path = "top.clk", referenced_symbol = @s1.$root::@s3.top::@s4.top::@s5.clk, semantic_type = !talos.integral<1, false, false, 0 : 0, bit>} {
                      }
                    }
                    talos.sv.assertion.simple attributes {has_repetition = false, is_null = false, node_id = 32 : i64, repetition_is_unbounded = false} {
                      talos.sv.expression.integer_literal attributes {constant_value = "1", is_declared_unsized = true, is_signed = true, node_id = 33 : i64, semantic_type = !talos.integral<32, true, false, 31 : 0, int>} {
                      }
                    }
                  }
                }
              }
              talos.sv.timing.signal_event attributes {edge_kind = 0 : i32, has_iff = false, node_id = 34 : i64} {
                talos.sv.expression.named_value attributes {is_signed = false, node_id = 35 : i64, referenced_path = "top.e", referenced_symbol = @s1.$root::@s3.top::@s4.top::@s6.e, semantic_type = !talos.event} {
                }
              }
            }
            talos.sv.statement.empty attributes {node_id = 36 : i64} {
            }
          }
        }
      }
    }
  }
}


// CHECK: simulation.code_unit.decl {{[0-9]+}} in 1 always hierarchy "top.s.$sequence_endpoint"

// CHECK-LABEL: simulation.func private @unit_1(
// CHECK-SAME: %[[ENDPOINT:[a-z0-9]+]]: !simulation.event
// CHECK: %[[TRIGGERED:.*]] = simulation.event.triggered %[[ENDPOINT]]
// CHECK: cf.cond_br %[[TRIGGERED]]
// CHECK: simulation.observer.bind
// CHECK: simulation.suspend.observe

// CHECK: simulation.func private @unit_2.$clocking_event_primary.{{[0-9]+}}(
// CHECK: simulation.event.triggered
// CHECK: simulation.func private @unit_2.$clocking_event_primary.{{[0-9]+}}(
// CHECK: simulation.event.triggered
// CHECK-LABEL: simulation.func private @unit_2(
// CHECK: %[[FIRST:.*]] = simulation.observer.bind @unit_2.$clocking_event_primary.{{[0-9]+}}
// CHECK: %[[SECOND:.*]] = simulation.observer.bind @unit_2.$clocking_event_primary.{{[0-9]+}}
// CHECK: simulation.suspend.observe %[[FIRST]], %[[SECOND]], {{.*}} edges [0, 0]
