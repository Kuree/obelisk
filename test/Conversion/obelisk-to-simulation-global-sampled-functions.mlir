// RUN: obelisk-opt %s '--lower-obelisk-to-sim=opt-level=0' | FileCheck %s

// Hand-authored semantic fixture freezing all ten global sampled-value calls.
// The five past-side functions share one strictly-prior Postponed sampler. The
// future-side functions run in a detached resolver whose first operation waits
// for the strictly later global-clock occurrence and resumes in Reactive.
//
// CHECK-COUNT-1: debug "alternate-clock sampler"
// CHECK-COUNT-1: obelisk_sim.assert.clocked_sample_update
// CHECK-COUNT-5: obelisk_sim.assert.clocked_sample_read
// CHECK: obelisk_sim.func private @{{.*}} attributes {{.*}}obelisk_sim.global_future_resolver{{.*}}schedule.detached_controls
// CHECK-SAME: schedule.prime_on_spawn
// CHECK: obelisk_sim.suspend.edge posedge
// CHECK-SAME: obelisk_sim.global_future_wait
// CHECK-SAME: resume_region = 10 : i32
// CHECK-COUNT-5: obelisk_sim.assert.sampled_read
// CHECK: obelisk_sim.func private @{{.*}} attributes {{.*}}obelisk_sim.global_future_monitor
// CHECK: obelisk_sim.suspend.edge posedge
// CHECK-SAME: resume_region = 8 : i32
// CHECK: obelisk_sim.spawn @{{.*}}fork{{.*}}

module {
  obelisk.sv.symbol.definition attributes {definition_kind = 0 : i32, hierarchical_name = "top", name = "top", node_id = 0 : i64, sym_name = "s0.top"} {
  }
  obelisk.sv.symbol.root attributes {hierarchical_name = "\\$root ", name = "$root", node_id = 1 : i64, sym_name = "s1.$root"} {
    obelisk.sv.symbol.compilation_unit attributes {hierarchical_name = "$unit", node_id = 2 : i64, sym_name = "s2"} {
    }
    obelisk.sv.symbol.instance attributes {hierarchical_name = "top", is_uninstantiated = false, name = "top", node_id = 3 : i64, referenced_path = "top", referenced_symbol = @s0.top, sym_name = "s3.top"} {
      obelisk.sv.symbol.instance_body attributes {hierarchical_name = "top", name = "top", node_id = 4 : i64, sym_name = "s4.top", time_precision_fs = 1000000 : i64, time_unit_fs = 1000000 : i64} {
        obelisk.sv.symbol.variable attributes {hierarchical_name = "top.gclk", lifetime = 1 : i32, name = "gclk", node_id = 5 : i64, semantic_type = !obelisk.integral<1, false, true, 0 : 0, logic>, sym_name = "s5.gclk"} {
        }
        obelisk.sv.symbol.variable attributes {hierarchical_name = "top.a", lifetime = 1 : i32, name = "a", node_id = 6 : i64, semantic_type = !obelisk.integral<1, false, true, 0 : 0, logic>, sym_name = "s6.a"} {
        }
        obelisk.sv.symbol.variable attributes {hierarchical_name = "top.b", lifetime = 1 : i32, name = "b", node_id = 7 : i64, semantic_type = !obelisk.integral<1, false, true, 0 : 0, logic>, sym_name = "s7.b"} {
        }
        obelisk.sv.symbol.clocking_block attributes {hierarchical_name = "top.gcb", is_default = false, is_global = true, name = "gcb", node_id = 8 : i64, sym_name = "s8.gcb"} {
          obelisk.sv.timing.signal_event attributes {edge_kind = 1 : i32, has_iff = false, node_id = 9 : i64} {
            obelisk.sv.expression.named_value attributes {is_signed = false, node_id = 10 : i64, referenced_path = "top.gclk", referenced_symbol = @s1.$root::@s3.top::@s4.top::@s5.gclk, semantic_type = !obelisk.integral<1, false, true, 0 : 0, logic>} {
            }
          }
        }
        obelisk.sv.symbol.procedural_block attributes {hierarchical_name = "top", node_id = 11 : i64, procedure_kind = 2 : i32, sym_name = "s9", time_precision_fs = 1000000 : i64, time_unit_fs = 1000000 : i64} {
          obelisk.sv.statement.timed attributes {node_id = 12 : i64} {
            obelisk.sv.timing.signal_event attributes {edge_kind = 1 : i32, has_iff = false, node_id = 13 : i64} {
              obelisk.sv.expression.named_value attributes {is_signed = false, node_id = 14 : i64, referenced_path = "top.gclk", referenced_symbol = @s1.$root::@s3.top::@s4.top::@s5.gclk, semantic_type = !obelisk.integral<1, false, true, 0 : 0, logic>} {
              }
            }
            obelisk.sv.statement.block attributes {node_id = 15 : i64} {
              obelisk.sv.statement.list attributes {node_id = 16 : i64} {
                obelisk.sv.statement.expression_statement attributes {node_id = 17 : i64} {
                  obelisk.sv.expression.assignment attributes {assignment_kind = 1 : i32, is_signed = false, node_id = 18 : i64, semantic_type = !obelisk.integral<1, false, true, 0 : 0, logic>} {
                    obelisk.sv.expression.named_value attributes {is_signed = false, node_id = 19 : i64, referenced_path = "top.b", referenced_symbol = @s1.$root::@s3.top::@s4.top::@s7.b, semantic_type = !obelisk.integral<1, false, true, 0 : 0, logic>} {
                    }
                    obelisk.sv.expression.conversion attributes {is_implicit = true, is_signed = false, node_id = 20 : i64, semantic_type = !obelisk.integral<1, false, true, 0 : 0, logic>} {
                      obelisk.sv.expression.call attributes {argument_count = 1 : i64, callee_name = "$past_gclk", clocking_block_event, clocking_event_edge = 1 : i32, clocking_event_path = "top.gclk", clocking_event_symbol = @s1.$root::@s3.top::@s4.top::@s5.gclk, constraint_restrictions = [], defaulted_arguments = array<i64: 0>, has_inline_constraints = false, has_iterator_expression = false, has_output_arguments = false, has_this_class = false, is_signed = false, is_super_class = false, is_system_call = true, node_id = 21 : i64, semantic_type = !obelisk.integral<1, false, true, 0 : 0, logic>, subroutine_kind = 0 : i32, system_library_cell = "work.top", system_scope_path = "top", system_scope_symbol = @s1.$root::@s3.top::@s4.top} {
                        obelisk.sv.expression.named_value attributes {is_signed = false, node_id = 22 : i64, referenced_path = "top.a", referenced_symbol = @s1.$root::@s3.top::@s4.top::@s6.a, semantic_type = !obelisk.integral<1, false, true, 0 : 0, logic>} {
                        }
                      }
                    }
                  }
                }
                obelisk.sv.statement.expression_statement attributes {node_id = 23 : i64} {
                  obelisk.sv.expression.assignment attributes {assignment_kind = 1 : i32, is_signed = false, node_id = 24 : i64, semantic_type = !obelisk.integral<1, false, true, 0 : 0, logic>} {
                    obelisk.sv.expression.named_value attributes {is_signed = false, node_id = 25 : i64, referenced_path = "top.b", referenced_symbol = @s1.$root::@s3.top::@s4.top::@s7.b, semantic_type = !obelisk.integral<1, false, true, 0 : 0, logic>} {
                    }
                    obelisk.sv.expression.conversion attributes {is_implicit = true, is_signed = false, node_id = 26 : i64, semantic_type = !obelisk.integral<1, false, true, 0 : 0, logic>} {
                      obelisk.sv.expression.call attributes {argument_count = 1 : i64, callee_name = "$rose_gclk", clocking_block_event, clocking_event_edge = 1 : i32, clocking_event_path = "top.gclk", clocking_event_symbol = @s1.$root::@s3.top::@s4.top::@s5.gclk, constraint_restrictions = [], defaulted_arguments = array<i64: 0>, has_inline_constraints = false, has_iterator_expression = false, has_output_arguments = false, has_this_class = false, is_signed = false, is_super_class = false, is_system_call = true, node_id = 27 : i64, semantic_type = !obelisk.integral<1, false, false, 0 : 0, bit>, subroutine_kind = 0 : i32, system_library_cell = "work.top", system_scope_path = "top", system_scope_symbol = @s1.$root::@s3.top::@s4.top} {
                        obelisk.sv.expression.named_value attributes {is_signed = false, node_id = 28 : i64, referenced_path = "top.a", referenced_symbol = @s1.$root::@s3.top::@s4.top::@s6.a, semantic_type = !obelisk.integral<1, false, true, 0 : 0, logic>} {
                        }
                      }
                    }
                  }
                }
                obelisk.sv.statement.expression_statement attributes {node_id = 29 : i64} {
                  obelisk.sv.expression.assignment attributes {assignment_kind = 1 : i32, is_signed = false, node_id = 30 : i64, semantic_type = !obelisk.integral<1, false, true, 0 : 0, logic>} {
                    obelisk.sv.expression.named_value attributes {is_signed = false, node_id = 31 : i64, referenced_path = "top.b", referenced_symbol = @s1.$root::@s3.top::@s4.top::@s7.b, semantic_type = !obelisk.integral<1, false, true, 0 : 0, logic>} {
                    }
                    obelisk.sv.expression.conversion attributes {is_implicit = true, is_signed = false, node_id = 32 : i64, semantic_type = !obelisk.integral<1, false, true, 0 : 0, logic>} {
                      obelisk.sv.expression.call attributes {argument_count = 1 : i64, callee_name = "$fell_gclk", clocking_block_event, clocking_event_edge = 1 : i32, clocking_event_path = "top.gclk", clocking_event_symbol = @s1.$root::@s3.top::@s4.top::@s5.gclk, constraint_restrictions = [], defaulted_arguments = array<i64: 0>, has_inline_constraints = false, has_iterator_expression = false, has_output_arguments = false, has_this_class = false, is_signed = false, is_super_class = false, is_system_call = true, node_id = 33 : i64, semantic_type = !obelisk.integral<1, false, false, 0 : 0, bit>, subroutine_kind = 0 : i32, system_library_cell = "work.top", system_scope_path = "top", system_scope_symbol = @s1.$root::@s3.top::@s4.top} {
                        obelisk.sv.expression.named_value attributes {is_signed = false, node_id = 34 : i64, referenced_path = "top.a", referenced_symbol = @s1.$root::@s3.top::@s4.top::@s6.a, semantic_type = !obelisk.integral<1, false, true, 0 : 0, logic>} {
                        }
                      }
                    }
                  }
                }
                obelisk.sv.statement.expression_statement attributes {node_id = 35 : i64} {
                  obelisk.sv.expression.assignment attributes {assignment_kind = 1 : i32, is_signed = false, node_id = 36 : i64, semantic_type = !obelisk.integral<1, false, true, 0 : 0, logic>} {
                    obelisk.sv.expression.named_value attributes {is_signed = false, node_id = 37 : i64, referenced_path = "top.b", referenced_symbol = @s1.$root::@s3.top::@s4.top::@s7.b, semantic_type = !obelisk.integral<1, false, true, 0 : 0, logic>} {
                    }
                    obelisk.sv.expression.conversion attributes {is_implicit = true, is_signed = false, node_id = 38 : i64, semantic_type = !obelisk.integral<1, false, true, 0 : 0, logic>} {
                      obelisk.sv.expression.call attributes {argument_count = 1 : i64, callee_name = "$stable_gclk", clocking_block_event, clocking_event_edge = 1 : i32, clocking_event_path = "top.gclk", clocking_event_symbol = @s1.$root::@s3.top::@s4.top::@s5.gclk, constraint_restrictions = [], defaulted_arguments = array<i64: 0>, has_inline_constraints = false, has_iterator_expression = false, has_output_arguments = false, has_this_class = false, is_signed = false, is_super_class = false, is_system_call = true, node_id = 39 : i64, semantic_type = !obelisk.integral<1, false, false, 0 : 0, bit>, subroutine_kind = 0 : i32, system_library_cell = "work.top", system_scope_path = "top", system_scope_symbol = @s1.$root::@s3.top::@s4.top} {
                        obelisk.sv.expression.named_value attributes {is_signed = false, node_id = 40 : i64, referenced_path = "top.a", referenced_symbol = @s1.$root::@s3.top::@s4.top::@s6.a, semantic_type = !obelisk.integral<1, false, true, 0 : 0, logic>} {
                        }
                      }
                    }
                  }
                }
                obelisk.sv.statement.expression_statement attributes {node_id = 41 : i64} {
                  obelisk.sv.expression.assignment attributes {assignment_kind = 1 : i32, is_signed = false, node_id = 42 : i64, semantic_type = !obelisk.integral<1, false, true, 0 : 0, logic>} {
                    obelisk.sv.expression.named_value attributes {is_signed = false, node_id = 43 : i64, referenced_path = "top.b", referenced_symbol = @s1.$root::@s3.top::@s4.top::@s7.b, semantic_type = !obelisk.integral<1, false, true, 0 : 0, logic>} {
                    }
                    obelisk.sv.expression.conversion attributes {is_implicit = true, is_signed = false, node_id = 44 : i64, semantic_type = !obelisk.integral<1, false, true, 0 : 0, logic>} {
                      obelisk.sv.expression.call attributes {argument_count = 1 : i64, callee_name = "$changed_gclk", clocking_block_event, clocking_event_edge = 1 : i32, clocking_event_path = "top.gclk", clocking_event_symbol = @s1.$root::@s3.top::@s4.top::@s5.gclk, constraint_restrictions = [], defaulted_arguments = array<i64: 0>, has_inline_constraints = false, has_iterator_expression = false, has_output_arguments = false, has_this_class = false, is_signed = false, is_super_class = false, is_system_call = true, node_id = 45 : i64, semantic_type = !obelisk.integral<1, false, false, 0 : 0, bit>, subroutine_kind = 0 : i32, system_library_cell = "work.top", system_scope_path = "top", system_scope_symbol = @s1.$root::@s3.top::@s4.top} {
                        obelisk.sv.expression.named_value attributes {is_signed = false, node_id = 46 : i64, referenced_path = "top.a", referenced_symbol = @s1.$root::@s3.top::@s4.top::@s6.a, semantic_type = !obelisk.integral<1, false, true, 0 : 0, logic>} {
                        }
                      }
                    }
                  }
                }
              }
            }
          }
        }
        obelisk.sv.symbol.procedural_block attributes {hierarchical_name = "top", node_id = 47 : i64, procedure_kind = 2 : i32, sym_name = "s10", time_precision_fs = 1000000 : i64, time_unit_fs = 1000000 : i64} {
          obelisk.sv.statement.concurrent_assertion attributes {assertion_kind = 0 : i32, has_default_disable = false, has_fail_action = false, has_pass_action = true, node_id = 48 : i64} {
            obelisk.sv.assertion.clocking attributes {node_id = 49 : i64} {
              obelisk.sv.timing.signal_event attributes {edge_kind = 1 : i32, has_iff = false, node_id = 50 : i64} {
                obelisk.sv.expression.named_value attributes {is_signed = false, node_id = 51 : i64, referenced_path = "top.gclk", referenced_symbol = @s1.$root::@s3.top::@s4.top::@s5.gclk, semantic_type = !obelisk.integral<1, false, true, 0 : 0, logic>} {
                }
              }
              obelisk.sv.assertion.simple attributes {has_repetition = false, is_null = false, node_id = 52 : i64, repetition_is_unbounded = false} {
                obelisk.sv.expression.binary_op attributes {is_signed = false, node_id = 53 : i64, operator_kind = 20 : i32, semantic_type = !obelisk.integral<1, false, false, 0 : 0, bit>} {
                  obelisk.sv.expression.binary_op attributes {is_signed = false, node_id = 54 : i64, operator_kind = 20 : i32, semantic_type = !obelisk.integral<1, false, false, 0 : 0, bit>} {
                    obelisk.sv.expression.binary_op attributes {is_signed = false, node_id = 55 : i64, operator_kind = 20 : i32, semantic_type = !obelisk.integral<1, false, false, 0 : 0, bit>} {
                      obelisk.sv.expression.binary_op attributes {is_signed = false, node_id = 56 : i64, operator_kind = 20 : i32, semantic_type = !obelisk.integral<1, false, false, 0 : 0, bit>} {
                        obelisk.sv.expression.call attributes {argument_count = 1 : i64, callee_name = "$future_gclk", clocking_block_event, clocking_event_edge = 1 : i32, clocking_event_path = "top.gclk", clocking_event_symbol = @s1.$root::@s3.top::@s4.top::@s5.gclk, constraint_restrictions = [], defaulted_arguments = array<i64: 0>, has_inline_constraints = false, has_iterator_expression = false, has_output_arguments = false, has_this_class = false, is_signed = false, is_super_class = false, is_system_call = true, node_id = 57 : i64, semantic_type = !obelisk.integral<1, false, true, 0 : 0, logic>, subroutine_kind = 0 : i32, system_library_cell = "work.top", system_scope_path = "top", system_scope_symbol = @s1.$root::@s3.top::@s4.top} {
                          obelisk.sv.expression.named_value attributes {is_signed = false, node_id = 58 : i64, referenced_path = "top.a", referenced_symbol = @s1.$root::@s3.top::@s4.top::@s6.a, semantic_type = !obelisk.integral<1, false, true, 0 : 0, logic>} {
                          }
                        }
                        obelisk.sv.expression.call attributes {argument_count = 1 : i64, callee_name = "$rising_gclk", clocking_block_event, clocking_event_edge = 1 : i32, clocking_event_path = "top.gclk", clocking_event_symbol = @s1.$root::@s3.top::@s4.top::@s5.gclk, constraint_restrictions = [], defaulted_arguments = array<i64: 0>, has_inline_constraints = false, has_iterator_expression = false, has_output_arguments = false, has_this_class = false, is_signed = false, is_super_class = false, is_system_call = true, node_id = 59 : i64, semantic_type = !obelisk.integral<1, false, false, 0 : 0, bit>, subroutine_kind = 0 : i32, system_library_cell = "work.top", system_scope_path = "top", system_scope_symbol = @s1.$root::@s3.top::@s4.top} {
                          obelisk.sv.expression.named_value attributes {is_signed = false, node_id = 60 : i64, referenced_path = "top.a", referenced_symbol = @s1.$root::@s3.top::@s4.top::@s6.a, semantic_type = !obelisk.integral<1, false, true, 0 : 0, logic>} {
                          }
                        }
                      }
                      obelisk.sv.expression.call attributes {argument_count = 1 : i64, callee_name = "$falling_gclk", clocking_block_event, clocking_event_edge = 1 : i32, clocking_event_path = "top.gclk", clocking_event_symbol = @s1.$root::@s3.top::@s4.top::@s5.gclk, constraint_restrictions = [], defaulted_arguments = array<i64: 0>, has_inline_constraints = false, has_iterator_expression = false, has_output_arguments = false, has_this_class = false, is_signed = false, is_super_class = false, is_system_call = true, node_id = 61 : i64, semantic_type = !obelisk.integral<1, false, false, 0 : 0, bit>, subroutine_kind = 0 : i32, system_library_cell = "work.top", system_scope_path = "top", system_scope_symbol = @s1.$root::@s3.top::@s4.top} {
                        obelisk.sv.expression.named_value attributes {is_signed = false, node_id = 62 : i64, referenced_path = "top.a", referenced_symbol = @s1.$root::@s3.top::@s4.top::@s6.a, semantic_type = !obelisk.integral<1, false, true, 0 : 0, logic>} {
                        }
                      }
                    }
                    obelisk.sv.expression.call attributes {argument_count = 1 : i64, callee_name = "$steady_gclk", clocking_block_event, clocking_event_edge = 1 : i32, clocking_event_path = "top.gclk", clocking_event_symbol = @s1.$root::@s3.top::@s4.top::@s5.gclk, constraint_restrictions = [], defaulted_arguments = array<i64: 0>, has_inline_constraints = false, has_iterator_expression = false, has_output_arguments = false, has_this_class = false, is_signed = false, is_super_class = false, is_system_call = true, node_id = 63 : i64, semantic_type = !obelisk.integral<1, false, false, 0 : 0, bit>, subroutine_kind = 0 : i32, system_library_cell = "work.top", system_scope_path = "top", system_scope_symbol = @s1.$root::@s3.top::@s4.top} {
                      obelisk.sv.expression.named_value attributes {is_signed = false, node_id = 64 : i64, referenced_path = "top.a", referenced_symbol = @s1.$root::@s3.top::@s4.top::@s6.a, semantic_type = !obelisk.integral<1, false, true, 0 : 0, logic>} {
                      }
                    }
                  }
                  obelisk.sv.expression.call attributes {argument_count = 1 : i64, callee_name = "$changing_gclk", clocking_block_event, clocking_event_edge = 1 : i32, clocking_event_path = "top.gclk", clocking_event_symbol = @s1.$root::@s3.top::@s4.top::@s5.gclk, constraint_restrictions = [], defaulted_arguments = array<i64: 0>, has_inline_constraints = false, has_iterator_expression = false, has_output_arguments = false, has_this_class = false, is_signed = false, is_super_class = false, is_system_call = true, node_id = 65 : i64, semantic_type = !obelisk.integral<1, false, false, 0 : 0, bit>, subroutine_kind = 0 : i32, system_library_cell = "work.top", system_scope_path = "top", system_scope_symbol = @s1.$root::@s3.top::@s4.top} {
                    obelisk.sv.expression.named_value attributes {is_signed = false, node_id = 66 : i64, referenced_path = "top.a", referenced_symbol = @s1.$root::@s3.top::@s4.top::@s6.a, semantic_type = !obelisk.integral<1, false, true, 0 : 0, logic>} {
                    }
                  }
                }
              }
            }
            obelisk.sv.statement.empty attributes {node_id = 67 : i64} {
            }
          }
        }
      }
    }
  }
}
