// RUN: obelisk-opt %s '--lower-obelisk-to-sim=opt-level=0' | FileCheck %s

module {
  obelisk.sv.symbol.definition attributes {definition_kind = 0 : i32, hierarchical_name = "simulation_named_event", name = "simulation_named_event", node_id = 0 : i64, sym_name = "s0.simulation_named_event"} {
  }
  obelisk.sv.symbol.root attributes {hierarchical_name = "\\$root ", name = "$root", node_id = 1 : i64, sym_name = "s1.$root"} {
    obelisk.sv.symbol.compilation_unit attributes {hierarchical_name = "$unit", node_id = 2 : i64, sym_name = "s2"} {
    }
    obelisk.sv.symbol.instance attributes {hierarchical_name = "simulation_named_event", is_uninstantiated = false, name = "simulation_named_event", node_id = 3 : i64, referenced_path = "simulation_named_event", referenced_symbol = @s0.simulation_named_event, sym_name = "s3.simulation_named_event"} {
      obelisk.sv.symbol.instance_body attributes {hierarchical_name = "simulation_named_event", name = "simulation_named_event", node_id = 4 : i64, sym_name = "s4.simulation_named_event"} {
        obelisk.sv.symbol.variable attributes {hierarchical_name = "simulation_named_event.ready", lifetime = 1 : i32, name = "ready", node_id = 5 : i64, semantic_type = !obelisk.event, sym_name = "s5.ready"} {
        }
        obelisk.sv.symbol.variable attributes {hierarchical_name = "simulation_named_event.other", lifetime = 1 : i32, name = "other", node_id = 6 : i64, semantic_type = !obelisk.event, sym_name = "s6.other"} {
        }
        obelisk.sv.symbol.procedural_block attributes {hierarchical_name = "simulation_named_event", node_id = 7 : i64, procedure_kind = 0 : i32, sym_name = "s7", time_precision_fs = 1000000 : i64, time_unit_fs = 1000000 : i64} {
          obelisk.sv.statement.block attributes {node_id = 8 : i64} {
            obelisk.sv.statement.list attributes {node_id = 9 : i64} {
              obelisk.sv.statement.expression_statement attributes {node_id = 10 : i64} {
                obelisk.sv.expression.call attributes {argument_count = 3 : i64, callee_name = "$display", constraint_restrictions = [], defaulted_arguments = array<i64: 0, 0, 0>, has_inline_constraints = false, has_iterator_expression = false, has_output_arguments = false, has_this_class = false, is_super_class = false, is_system_call = true, node_id = 11 : i64, semantic_type = !obelisk.void, subroutine_kind = 1 : i32, system_library_cell = "work.simulation_named_event", system_scope_path = "simulation_named_event", system_scope_symbol = @s1.$root::@s3.simulation_named_event::@s4.simulation_named_event} {
                  obelisk.sv.expression.string_literal attributes {constant_value = "equal=%0d distinct=%0d", node_id = 12 : i64, semantic_type = !obelisk.ranged_packed_array<175 : 0 x !obelisk.integral<1, false, false, 0 : 0, bit>>} {
                  }
                  obelisk.sv.expression.binary_op attributes {node_id = 13 : i64, operator_kind = 9 : i32, semantic_type = !obelisk.integral<1, false, false, 0 : 0, bit>} {
                    obelisk.sv.expression.named_value attributes {node_id = 14 : i64, referenced_path = "simulation_named_event.ready", referenced_symbol = @s1.$root::@s3.simulation_named_event::@s4.simulation_named_event::@s5.ready, semantic_type = !obelisk.event} {
                    }
                    obelisk.sv.expression.named_value attributes {node_id = 15 : i64, referenced_path = "simulation_named_event.ready", referenced_symbol = @s1.$root::@s3.simulation_named_event::@s4.simulation_named_event::@s5.ready, semantic_type = !obelisk.event} {
                    }
                  }
                  obelisk.sv.expression.binary_op attributes {node_id = 16 : i64, operator_kind = 10 : i32, semantic_type = !obelisk.integral<1, false, false, 0 : 0, bit>} {
                    obelisk.sv.expression.named_value attributes {node_id = 17 : i64, referenced_path = "simulation_named_event.ready", referenced_symbol = @s1.$root::@s3.simulation_named_event::@s4.simulation_named_event::@s5.ready, semantic_type = !obelisk.event} {
                    }
                    obelisk.sv.expression.named_value attributes {node_id = 18 : i64, referenced_path = "simulation_named_event.other", referenced_symbol = @s1.$root::@s3.simulation_named_event::@s4.simulation_named_event::@s6.other, semantic_type = !obelisk.event} {
                    }
                  }
                }
              }
              obelisk.sv.statement.timed attributes {node_id = 19 : i64} {
                obelisk.sv.timing.signal_event attributes {edge_kind = 0 : i32, has_iff = false, node_id = 20 : i64} {
                  obelisk.sv.expression.named_value attributes {node_id = 21 : i64, referenced_path = "simulation_named_event.ready", referenced_symbol = @s1.$root::@s3.simulation_named_event::@s4.simulation_named_event::@s5.ready, semantic_type = !obelisk.event} {
                  }
                }
                obelisk.sv.statement.empty attributes {node_id = 22 : i64} {
                }
              }
              obelisk.sv.statement.expression_statement attributes {node_id = 23 : i64} {
                obelisk.sv.expression.call attributes {argument_count = 2 : i64, callee_name = "$display", constraint_restrictions = [], defaulted_arguments = array<i64: 0, 0>, has_inline_constraints = false, has_iterator_expression = false, has_output_arguments = false, has_this_class = false, is_super_class = false, is_system_call = true, node_id = 24 : i64, semantic_type = !obelisk.void, subroutine_kind = 1 : i32, system_library_cell = "work.simulation_named_event", system_scope_path = "simulation_named_event", system_scope_symbol = @s1.$root::@s3.simulation_named_event::@s4.simulation_named_event} {
                  obelisk.sv.expression.string_literal attributes {constant_value = "blocking=%0d", node_id = 25 : i64, semantic_type = !obelisk.ranged_packed_array<95 : 0 x !obelisk.integral<1, false, false, 0 : 0, bit>>} {
                  }
                  obelisk.sv.expression.call attributes {argument_count = 1 : i64, callee_name = "triggered", constraint_restrictions = [], defaulted_arguments = array<i64: 0>, has_inline_constraints = false, has_iterator_expression = false, has_output_arguments = false, has_this_class = false, is_super_class = false, is_system_call = true, node_id = 26 : i64, semantic_type = !obelisk.integral<1, false, false, 0 : 0, bit>, subroutine_kind = 0 : i32, system_library_cell = "work.simulation_named_event", system_scope_path = "simulation_named_event", system_scope_symbol = @s1.$root::@s3.simulation_named_event::@s4.simulation_named_event} {
                    obelisk.sv.expression.named_value attributes {node_id = 27 : i64, referenced_path = "simulation_named_event.ready", referenced_symbol = @s1.$root::@s3.simulation_named_event::@s4.simulation_named_event::@s5.ready, semantic_type = !obelisk.event} {
                    }
                  }
                }
              }
              obelisk.sv.statement.timed attributes {node_id = 28 : i64} {
                obelisk.sv.timing.signal_event attributes {edge_kind = 0 : i32, has_iff = false, node_id = 29 : i64} {
                  obelisk.sv.expression.named_value attributes {node_id = 30 : i64, referenced_path = "simulation_named_event.ready", referenced_symbol = @s1.$root::@s3.simulation_named_event::@s4.simulation_named_event::@s5.ready, semantic_type = !obelisk.event} {
                  }
                }
                obelisk.sv.statement.empty attributes {node_id = 31 : i64} {
                }
              }
              obelisk.sv.statement.expression_statement attributes {node_id = 32 : i64} {
                obelisk.sv.expression.call attributes {argument_count = 2 : i64, callee_name = "$display", constraint_restrictions = [], defaulted_arguments = array<i64: 0, 0>, has_inline_constraints = false, has_iterator_expression = false, has_output_arguments = false, has_this_class = false, is_super_class = false, is_system_call = true, node_id = 33 : i64, semantic_type = !obelisk.void, subroutine_kind = 1 : i32, system_library_cell = "work.simulation_named_event", system_scope_path = "simulation_named_event", system_scope_symbol = @s1.$root::@s3.simulation_named_event::@s4.simulation_named_event} {
                  obelisk.sv.expression.string_literal attributes {constant_value = "nonblocking=%0d", node_id = 34 : i64, semantic_type = !obelisk.ranged_packed_array<119 : 0 x !obelisk.integral<1, false, false, 0 : 0, bit>>} {
                  }
                  obelisk.sv.expression.call attributes {argument_count = 1 : i64, callee_name = "triggered", constraint_restrictions = [], defaulted_arguments = array<i64: 0>, has_inline_constraints = false, has_iterator_expression = false, has_output_arguments = false, has_this_class = false, is_super_class = false, is_system_call = true, node_id = 35 : i64, semantic_type = !obelisk.integral<1, false, false, 0 : 0, bit>, subroutine_kind = 0 : i32, system_library_cell = "work.simulation_named_event", system_scope_path = "simulation_named_event", system_scope_symbol = @s1.$root::@s3.simulation_named_event::@s4.simulation_named_event} {
                    obelisk.sv.expression.named_value attributes {node_id = 36 : i64, referenced_path = "simulation_named_event.ready", referenced_symbol = @s1.$root::@s3.simulation_named_event::@s4.simulation_named_event::@s5.ready, semantic_type = !obelisk.event} {
                    }
                  }
                }
              }
              obelisk.sv.statement.timed attributes {node_id = 37 : i64} {
                obelisk.sv.timing.delay attributes {node_id = 38 : i64} {
                  obelisk.sv.expression.integer_literal attributes {constant_value = "1", node_id = 39 : i64, semantic_type = !obelisk.integral<32, true, false, 31 : 0, int>} {
                  }
                }
                obelisk.sv.statement.empty attributes {node_id = 40 : i64} {
                }
              }
              obelisk.sv.statement.expression_statement attributes {node_id = 41 : i64} {
                obelisk.sv.expression.call attributes {argument_count = 2 : i64, callee_name = "$display", constraint_restrictions = [], defaulted_arguments = array<i64: 0, 0>, has_inline_constraints = false, has_iterator_expression = false, has_output_arguments = false, has_this_class = false, is_super_class = false, is_system_call = true, node_id = 42 : i64, semantic_type = !obelisk.void, subroutine_kind = 1 : i32, system_library_cell = "work.simulation_named_event", system_scope_path = "simulation_named_event", system_scope_symbol = @s1.$root::@s3.simulation_named_event::@s4.simulation_named_event} {
                  obelisk.sv.expression.string_literal attributes {constant_value = "cleared=%0d", node_id = 43 : i64, semantic_type = !obelisk.ranged_packed_array<87 : 0 x !obelisk.integral<1, false, false, 0 : 0, bit>>} {
                  }
                  obelisk.sv.expression.call attributes {argument_count = 1 : i64, callee_name = "triggered", constraint_restrictions = [], defaulted_arguments = array<i64: 0>, has_inline_constraints = false, has_iterator_expression = false, has_output_arguments = false, has_this_class = false, is_super_class = false, is_system_call = true, node_id = 44 : i64, semantic_type = !obelisk.integral<1, false, false, 0 : 0, bit>, subroutine_kind = 0 : i32, system_library_cell = "work.simulation_named_event", system_scope_path = "simulation_named_event", system_scope_symbol = @s1.$root::@s3.simulation_named_event::@s4.simulation_named_event} {
                    obelisk.sv.expression.named_value attributes {node_id = 45 : i64, referenced_path = "simulation_named_event.ready", referenced_symbol = @s1.$root::@s3.simulation_named_event::@s4.simulation_named_event::@s5.ready, semantic_type = !obelisk.event} {
                    }
                  }
                }
              }
            }
          }
        }
        obelisk.sv.symbol.procedural_block attributes {hierarchical_name = "simulation_named_event", node_id = 46 : i64, procedure_kind = 0 : i32, sym_name = "s8", time_precision_fs = 1000000 : i64, time_unit_fs = 1000000 : i64} {
          obelisk.sv.statement.block attributes {node_id = 47 : i64} {
            obelisk.sv.statement.list attributes {node_id = 48 : i64} {
              obelisk.sv.statement.timed attributes {node_id = 49 : i64} {
                obelisk.sv.timing.delay attributes {node_id = 50 : i64} {
                  obelisk.sv.expression.integer_literal attributes {constant_value = "1", node_id = 51 : i64, semantic_type = !obelisk.integral<32, true, false, 31 : 0, int>} {
                  }
                }
                obelisk.sv.statement.event_trigger attributes {node_id = 52 : i64} {
                  obelisk.sv.expression.named_value attributes {node_id = 53 : i64, referenced_path = "simulation_named_event.ready", referenced_symbol = @s1.$root::@s3.simulation_named_event::@s4.simulation_named_event::@s5.ready, semantic_type = !obelisk.event} {
                  }
                }
              }
              obelisk.sv.statement.event_trigger attributes {has_timing_control = true, is_nonblocking = true, node_id = 54 : i64} {
                obelisk.sv.expression.named_value attributes {node_id = 55 : i64, referenced_path = "simulation_named_event.ready", referenced_symbol = @s1.$root::@s3.simulation_named_event::@s4.simulation_named_event::@s5.ready, semantic_type = !obelisk.event} {
                }
                obelisk.sv.timing.delay attributes {node_id = 56 : i64} {
                  obelisk.sv.expression.integer_literal attributes {constant_value = "2", node_id = 57 : i64, semantic_type = !obelisk.integral<32, true, false, 31 : 0, int>} {
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

// A design-lifetime event is a direct scheduler object shared by every
// process capture; it is not represented as packed mutable storage.
// CHECK: %[[EVENT:.*]] = obelisk_sim.context.event %{{.*}}[0] : !obelisk_sim.event
// CHECK: obelisk_sim.spawn {{.*}}%[[EVENT]]
// CHECK: obelisk_sim.spawn {{.*}}%[[EVENT]]
// CHECK: obelisk_sim.event.equal
// CHECK: obelisk_sim.event.equal
// CHECK: obelisk_sim.suspend.event %{{.*}} to
// CHECK: obelisk_sim.event.triggered %{{.*}}
// CHECK: obelisk_sim.suspend.event %{{.*}} to
// CHECK: obelisk_sim.event.triggered %{{.*}}
// CHECK: obelisk_sim.event.triggered %{{.*}}
// CHECK: obelisk_sim.event.trigger %{{.*}} nonblocking = false
// CHECK: obelisk_sim.event.trigger %{{.*}} after %{{.*}} nonblocking = true
// CHECK-NOT: obelisk.sv.
