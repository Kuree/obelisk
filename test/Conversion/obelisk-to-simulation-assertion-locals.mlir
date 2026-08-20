// RUN: obelisk-opt %s --obelisk-sim-prepare | FileCheck %s --check-prefix=PREPARE
// RUN: obelisk-opt %s --pass-pipeline='builtin.module(obelisk-sim-prepare,obelisk_sim.design(obelisk_sim.func(obelisk-sim-lower-unit)))' | FileCheck %s --check-prefix=LOWER

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
        obelisk.sv.symbol.variable attributes {hierarchical_name = "t.rst", lifetime = 1 : i32, name = "rst", node_id = 8 : i64, semantic_type = !obelisk.integral<1, false, true, 0 : 0, logic>, sym_name = "s8.rst"} {
        }
        obelisk.sv.symbol.property attributes {has_default_instance = true, hierarchical_name = "t.p", name = "p", node_id = 9 : i64, port_count = 0 : i64, port_paths = [], port_symbols = [], sym_name = "s9.p"} {
          obelisk.sv.expression.assertion_instance attributes {argument_count = 0 : i64, argument_formal_paths = [], argument_formal_symbols = [], argument_kinds = array<i64>, has_expanded_body = true, is_recursive_property = false, is_signed = false, local_variable_count = 2 : i64, local_variable_has_initializer = array<i64: 1, 1>, local_variable_paths = ["t.p.x", "t.p.y"], local_variable_symbols = [@s1.$root::@s3.t::@s4.t::@s9.p::@s10.x, @s1.$root::@s3.t::@s4.t::@s9.p::@s11.y], node_id = 10 : i64, referenced_path = "t.p", referenced_symbol = @s1.$root::@s3.t::@s4.t::@s9.p, semantic_type = !obelisk.property} {
            obelisk.sv.assertion.clocking attributes {node_id = 11 : i64} {
              obelisk.sv.timing.signal_event attributes {edge_kind = 1 : i32, has_iff = false, node_id = 12 : i64} {
                obelisk.sv.expression.named_value attributes {is_signed = false, node_id = 13 : i64, referenced_path = "t.clk", referenced_symbol = @s1.$root::@s3.t::@s4.t::@s5.clk, semantic_type = !obelisk.integral<1, false, true, 0 : 0, logic>} {
                }
              }
              obelisk.sv.assertion.disable_iff attributes {node_id = 14 : i64} {
                obelisk.sv.expression.named_value attributes {is_signed = false, node_id = 15 : i64, referenced_path = "t.rst", referenced_symbol = @s1.$root::@s3.t::@s4.t::@s8.rst, semantic_type = !obelisk.integral<1, false, true, 0 : 0, logic>} {
                }
                obelisk.sv.assertion.binary attributes {node_id = 16 : i64, operator_kind = 11 : i32} {
                  obelisk.sv.assertion.sequence_with_match attributes {has_repetition = false, match_item_count = 1 : i64, node_id = 17 : i64, repetition_is_unbounded = false} {
                    obelisk.sv.assertion.simple attributes {has_repetition = false, is_null = false, node_id = 18 : i64, repetition_is_unbounded = false} {
                      obelisk.sv.expression.named_value attributes {is_signed = false, node_id = 19 : i64, referenced_path = "t.a", referenced_symbol = @s1.$root::@s3.t::@s4.t::@s6.a, semantic_type = !obelisk.integral<1, false, true, 0 : 0, logic>} {
                      }
                    }
                    obelisk.sv.expression.assignment attributes {assignment_kind = 0 : i32, is_signed = false, node_id = 20 : i64, semantic_type = !obelisk.integral<1, false, true, 0 : 0, logic>} {
                      obelisk.sv.expression.named_value attributes {is_signed = false, node_id = 21 : i64, referenced_path = "t.p.x", referenced_symbol = @s1.$root::@s3.t::@s4.t::@s9.p::@s10.x, semantic_type = !obelisk.integral<1, false, true, 0 : 0, logic>} {
                      }
                      obelisk.sv.expression.unary_op attributes {is_signed = false, node_id = 22 : i64, operator_kind = 9 : i32, semantic_type = !obelisk.integral<1, false, true, 0 : 0, logic>} {
                        obelisk.sv.expression.named_value attributes {is_signed = false, node_id = 23 : i64, referenced_path = "t.a", referenced_symbol = @s1.$root::@s3.t::@s4.t::@s6.a, semantic_type = !obelisk.integral<1, false, true, 0 : 0, logic>} {
                        }
                      }
                    }
                  }
                  obelisk.sv.assertion.sequence_concat attributes {delays = [{is_unbounded = false, max = 2 : i64, min = 2 : i64}], node_id = 24 : i64} {
                    obelisk.sv.assertion.simple attributes {has_repetition = false, is_null = false, node_id = 25 : i64, repetition_is_unbounded = false} {
                      obelisk.sv.expression.binary_op attributes {is_signed = false, node_id = 26 : i64, operator_kind = 19 : i32, semantic_type = !obelisk.integral<1, false, true, 0 : 0, logic>} {
                        obelisk.sv.expression.binary_op attributes {is_signed = false, node_id = 27 : i64, operator_kind = 9 : i32, semantic_type = !obelisk.integral<1, false, true, 0 : 0, logic>} {
                          obelisk.sv.expression.named_value attributes {is_signed = false, node_id = 28 : i64, referenced_path = "t.b", referenced_symbol = @s1.$root::@s3.t::@s4.t::@s7.b, semantic_type = !obelisk.integral<1, false, true, 0 : 0, logic>} {
                          }
                          obelisk.sv.expression.named_value attributes {is_signed = false, node_id = 29 : i64, referenced_path = "t.p.x", referenced_symbol = @s1.$root::@s3.t::@s4.t::@s9.p::@s10.x, semantic_type = !obelisk.integral<1, false, true, 0 : 0, logic>} {
                          }
                        }
                        obelisk.sv.expression.binary_op attributes {is_signed = false, node_id = 30 : i64, operator_kind = 10 : i32, semantic_type = !obelisk.integral<1, false, true, 0 : 0, logic>} {
                          obelisk.sv.expression.named_value attributes {is_signed = false, node_id = 31 : i64, referenced_path = "t.b", referenced_symbol = @s1.$root::@s3.t::@s4.t::@s7.b, semantic_type = !obelisk.integral<1, false, true, 0 : 0, logic>} {
                          }
                          obelisk.sv.expression.named_value attributes {is_signed = false, node_id = 32 : i64, referenced_path = "t.p.y", referenced_symbol = @s1.$root::@s3.t::@s4.t::@s9.p::@s11.y, semantic_type = !obelisk.integral<1, false, true, 0 : 0, logic>} {
                          }
                        }
                      }
                    }
                  }
                }
              }
            }
            obelisk.sv.expression.named_value attributes {is_signed = false, node_id = 33 : i64, referenced_path = "t.a", referenced_symbol = @s1.$root::@s3.t::@s4.t::@s6.a, semantic_type = !obelisk.integral<1, false, true, 0 : 0, logic>} {
            }
            obelisk.sv.expression.named_value attributes {is_signed = false, node_id = 34 : i64, referenced_path = "t.p.x", referenced_symbol = @s1.$root::@s3.t::@s4.t::@s9.p::@s10.x, semantic_type = !obelisk.integral<1, false, true, 0 : 0, logic>} {
            }
          }
          obelisk.sv.symbol.local_assertion_var attributes {hierarchical_name = "t.p.y", name = "y", node_id = 64 : i64, semantic_type = !obelisk.integral<1, false, true, 0 : 0, logic>, sym_name = "s14.y"} {
            obelisk.sv.expression.named_value attributes {is_signed = false, node_id = 65 : i64, referenced_path = "t.p.x", referenced_symbol = @s1.$root::@s3.t::@s4.t::@s9.p::@s13.x, semantic_type = !obelisk.integral<1, false, true, 0 : 0, logic>} {
            }
          }
          obelisk.sv.symbol.local_assertion_var attributes {hierarchical_name = "t.p.x", name = "x", node_id = 66 : i64, semantic_type = !obelisk.integral<1, false, true, 0 : 0, logic>, sym_name = "s13.x"} {
            obelisk.sv.expression.named_value attributes {is_signed = false, node_id = 67 : i64, referenced_path = "t.a", referenced_symbol = @s1.$root::@s3.t::@s4.t::@s6.a, semantic_type = !obelisk.integral<1, false, true, 0 : 0, logic>} {
            }
          }
          obelisk.sv.symbol.local_assertion_var attributes {hierarchical_name = "t.p.y", name = "y", node_id = 68 : i64, semantic_type = !obelisk.integral<1, false, true, 0 : 0, logic>, sym_name = "s11.y"} {
            obelisk.sv.expression.named_value attributes {is_signed = false, node_id = 69 : i64, referenced_path = "t.p.x", referenced_symbol = @s1.$root::@s3.t::@s4.t::@s9.p::@s10.x, semantic_type = !obelisk.integral<1, false, true, 0 : 0, logic>} {
            }
          }
          obelisk.sv.symbol.local_assertion_var attributes {hierarchical_name = "t.p.x", name = "x", node_id = 70 : i64, semantic_type = !obelisk.integral<1, false, true, 0 : 0, logic>, sym_name = "s10.x"} {
            obelisk.sv.expression.named_value attributes {is_signed = false, node_id = 71 : i64, referenced_path = "t.a", referenced_symbol = @s1.$root::@s3.t::@s4.t::@s6.a, semantic_type = !obelisk.integral<1, false, true, 0 : 0, logic>} {
            }
          }
        }
        obelisk.sv.symbol.procedural_block attributes {hierarchical_name = "t", node_id = 35 : i64, procedure_kind = 2 : i32, sym_name = "s12", time_precision_fs = 1000000 : i64, time_unit_fs = 1000000 : i64} {
          // A false implication antecedent is a vacuous success. Keep this as
          // cover property so the local-flow lowering must schedule its pass
          // action without running antecedent match items.
          obelisk.sv.statement.concurrent_assertion attributes {assertion_kind = 2 : i32, has_default_disable = false, has_fail_action = false, has_pass_action = true, node_id = 36 : i64, obelisk_sim.assertion_control_target_id = 101 : i64, obelisk_sim.assertion_controlled} {
            obelisk.sv.assertion.simple attributes {has_repetition = false, is_null = false, node_id = 37 : i64, repetition_is_unbounded = false} {
              obelisk.sv.expression.assertion_instance attributes {argument_count = 0 : i64, argument_formal_paths = [], argument_formal_symbols = [], argument_kinds = array<i64>, has_expanded_body = true, is_recursive_property = false, is_signed = false, local_variable_count = 2 : i64, local_variable_has_initializer = array<i64: 1, 1>, local_variable_paths = ["t.p.x", "t.p.y"], local_variable_symbols = [@s1.$root::@s3.t::@s4.t::@s9.p::@s13.x, @s1.$root::@s3.t::@s4.t::@s9.p::@s14.y], node_id = 38 : i64, referenced_path = "t.p", referenced_symbol = @s1.$root::@s3.t::@s4.t::@s9.p, semantic_type = !obelisk.property} {
                obelisk.sv.assertion.clocking attributes {node_id = 39 : i64} {
                  obelisk.sv.timing.signal_event attributes {edge_kind = 1 : i32, has_iff = false, node_id = 40 : i64} {
                    obelisk.sv.expression.named_value attributes {is_signed = false, node_id = 41 : i64, referenced_path = "t.clk", referenced_symbol = @s1.$root::@s3.t::@s4.t::@s5.clk, semantic_type = !obelisk.integral<1, false, true, 0 : 0, logic>} {
                    }
                  }
                  obelisk.sv.assertion.disable_iff attributes {node_id = 42 : i64} {
                    obelisk.sv.expression.named_value attributes {is_signed = false, node_id = 43 : i64, referenced_path = "t.rst", referenced_symbol = @s1.$root::@s3.t::@s4.t::@s8.rst, semantic_type = !obelisk.integral<1, false, true, 0 : 0, logic>} {
                    }
                    obelisk.sv.assertion.binary attributes {node_id = 44 : i64, operator_kind = 11 : i32} {
                      obelisk.sv.assertion.sequence_with_match attributes {has_repetition = false, match_item_count = 2 : i64, node_id = 45 : i64, repetition_is_unbounded = false} {
                        obelisk.sv.assertion.simple attributes {has_repetition = false, is_null = false, node_id = 46 : i64, repetition_is_unbounded = false} {
                          obelisk.sv.expression.named_value attributes {is_signed = false, node_id = 47 : i64, referenced_path = "t.a", referenced_symbol = @s1.$root::@s3.t::@s4.t::@s6.a, semantic_type = !obelisk.integral<1, false, true, 0 : 0, logic>} {
                          }
                        }
                        obelisk.sv.expression.assignment attributes {assignment_kind = 0 : i32, is_signed = false, node_id = 48 : i64, semantic_type = !obelisk.integral<1, false, true, 0 : 0, logic>} {
                          obelisk.sv.expression.named_value attributes {is_signed = false, node_id = 49 : i64, referenced_path = "t.p.x", referenced_symbol = @s1.$root::@s3.t::@s4.t::@s9.p::@s13.x, semantic_type = !obelisk.integral<1, false, true, 0 : 0, logic>} {
                          }
                          obelisk.sv.expression.unary_op attributes {is_signed = false, node_id = 50 : i64, operator_kind = 9 : i32, semantic_type = !obelisk.integral<1, false, true, 0 : 0, logic>} {
                            obelisk.sv.expression.named_value attributes {is_signed = false, node_id = 51 : i64, referenced_path = "t.a", referenced_symbol = @s1.$root::@s3.t::@s4.t::@s6.a, semantic_type = !obelisk.integral<1, false, true, 0 : 0, logic>} {
                            }
                          }
                        }
                        obelisk.sv.expression.call attributes {argument_count = 1 : i64, callee_name = "$display", constraint_restrictions = [], defaulted_arguments = array<i64: 0>, has_inline_constraints = false, has_iterator_expression = false, has_output_arguments = false, has_this_class = false, is_signed = false, is_super_class = false, is_system_call = true, node_id = 72 : i64, semantic_type = !obelisk.void, subroutine_kind = 1 : i32, system_library_cell = "work.t", system_scope_path = "t", system_scope_symbol = @s1.$root::@s3.t::@s4.t} {
                          obelisk.sv.expression.named_value attributes {is_signed = false, node_id = 73 : i64, referenced_path = "t.p.x", referenced_symbol = @s1.$root::@s3.t::@s4.t::@s9.p::@s13.x, semantic_type = !obelisk.integral<1, false, true, 0 : 0, logic>} {
                          }
                        }
                      }
                      obelisk.sv.assertion.sequence_concat attributes {delays = [{is_unbounded = false, max = 2 : i64, min = 2 : i64}], node_id = 52 : i64} {
                        obelisk.sv.assertion.simple attributes {has_repetition = false, is_null = false, node_id = 53 : i64, repetition_is_unbounded = false} {
                          obelisk.sv.expression.binary_op attributes {is_signed = false, node_id = 54 : i64, operator_kind = 19 : i32, semantic_type = !obelisk.integral<1, false, true, 0 : 0, logic>} {
                            obelisk.sv.expression.binary_op attributes {is_signed = false, node_id = 55 : i64, operator_kind = 9 : i32, semantic_type = !obelisk.integral<1, false, true, 0 : 0, logic>} {
                              obelisk.sv.expression.named_value attributes {is_signed = false, node_id = 56 : i64, referenced_path = "t.b", referenced_symbol = @s1.$root::@s3.t::@s4.t::@s7.b, semantic_type = !obelisk.integral<1, false, true, 0 : 0, logic>} {
                              }
                              obelisk.sv.expression.named_value attributes {is_signed = false, node_id = 57 : i64, referenced_path = "t.p.x", referenced_symbol = @s1.$root::@s3.t::@s4.t::@s9.p::@s13.x, semantic_type = !obelisk.integral<1, false, true, 0 : 0, logic>} {
                              }
                            }
                            obelisk.sv.expression.binary_op attributes {is_signed = false, node_id = 58 : i64, operator_kind = 10 : i32, semantic_type = !obelisk.integral<1, false, true, 0 : 0, logic>} {
                              obelisk.sv.expression.named_value attributes {is_signed = false, node_id = 59 : i64, referenced_path = "t.b", referenced_symbol = @s1.$root::@s3.t::@s4.t::@s7.b, semantic_type = !obelisk.integral<1, false, true, 0 : 0, logic>} {
                              }
                              obelisk.sv.expression.named_value attributes {is_signed = false, node_id = 60 : i64, referenced_path = "t.p.y", referenced_symbol = @s1.$root::@s3.t::@s4.t::@s9.p::@s14.y, semantic_type = !obelisk.integral<1, false, true, 0 : 0, logic>} {
                              }
                            }
                          }
                        }
                      }
                    }
                  }
                }
                obelisk.sv.expression.named_value attributes {is_signed = false, node_id = 61 : i64, referenced_path = "t.a", referenced_symbol = @s1.$root::@s3.t::@s4.t::@s6.a, semantic_type = !obelisk.integral<1, false, true, 0 : 0, logic>} {
                }
                obelisk.sv.expression.named_value attributes {is_signed = false, node_id = 62 : i64, referenced_path = "t.p.x", referenced_symbol = @s1.$root::@s3.t::@s4.t::@s9.p::@s13.x, semantic_type = !obelisk.integral<1, false, true, 0 : 0, logic>} {
                }
              }
            }
            obelisk.sv.statement.expression_statement attributes {node_id = 63 : i64} {
              obelisk.sv.expression.assignment attributes {assignment_kind = 0 : i32, is_signed = false, node_id = 74 : i64, semantic_type = !obelisk.integral<1, false, true, 0 : 0, logic>} {
                obelisk.sv.expression.named_value attributes {is_signed = false, node_id = 75 : i64, referenced_path = "t.b", referenced_symbol = @s1.$root::@s3.t::@s4.t::@s7.b, semantic_type = !obelisk.integral<1, false, true, 0 : 0, logic>} {
                }
                obelisk.sv.expression.named_value attributes {is_signed = false, node_id = 76 : i64, referenced_path = "t.a", referenced_symbol = @s1.$root::@s3.t::@s4.t::@s6.a, semantic_type = !obelisk.integral<1, false, true, 0 : 0, logic>} {
                }
              }
            }
          }
        }
      }
    }
  }
}


// The normalized types are frozen before the declaration inventory is
// separated from its executable monitor.
// PREPARE: local_variable_count = 2 : i64
// PREPARE-SAME: obelisk_sim.assertion_local_types = [!obelisk_sim.logic<1>, !obelisk_sim.logic<1>]

// The cover-property pass callback performs the observable action in
// Reactive. Both a completed consequent and a false current antecedent use
// this callback; the latter does not execute antecedent match items.
// LOWER: obelisk_sim.func private @[[PASS_CALLBACK:unit_0\.fork\.36\.0\.0]](
// LOWER: obelisk_sim.ref.load %arg1
// LOWER: obelisk_sim.ref.store {{.*}} to %arg2

// Match-call arguments are captured after preceding local assignments, while
// the call itself executes in a detached Reactive callback.
// LOWER: obelisk_sim.func private @[[MATCH_CALL:[^(]+]](%arg0: !obelisk_sim.context {{.*}}, %arg1: !obelisk_sim.logic<1>
// LOWER-SAME: domain = 0 : i32
// LOWER-SAME: home_region = 10 : i32
// LOWER-SAME: obelisk_sim.concurrent_match_call
// LOWER-SAME: obelisk_sim.detached_controls
// LOWER: obelisk_sim.display %arg0 {{.*}}(%arg1)

// Each of the two locals owns a cell at every one of the three sequence ages.
// LOWER-LABEL: obelisk_sim.func private @unit_0(
// LOWER: %[[STATE:.*]] = obelisk_sim.ref.alloc {{.*}} -> !obelisk_sim.ref<i64>
// LOWER-COUNT-6: obelisk_sim.ref.alloc {{.*}} -> !obelisk_sim.ref<!obelisk_sim.logic<1>>
// LOWER: obelisk_sim.suspend.edge posedge
// LOWER-SAME: resume_region = 8 : i32

// A true disable condition cancels the sampled attempt before predicates or
// local initializers execute, and advances the report-cancellation epoch.
// LOWER: %[[DISABLE:.*]] = obelisk_sim.ref.load %arg4
// LOWER: %[[DISABLED:.*]] = obelisk_sim.logic.is_true %[[DISABLE]]
// LOWER: cf.cond_br %[[DISABLED]], ^[[CANCEL:bb[0-9]+]], ^[[EVALUATE:bb[0-9]+]]
// LOWER: ^[[CANCEL]]:
// LOWER: obelisk_sim.ref.store {{.*}} to %[[STATE]]
// LOWER: arith.addi
// LOWER: ^[[EVALUATE]]:
// LOWER: obelisk_sim.spawn @[[PASS_CALLBACK]]

// Off skips the complete new-attempt path—including local initialization and
// age-zero match items—but the already-live age state above still advances.
// LOWER: %[[ENABLED:.*]] = obelisk_sim.assert.enabled %arg0 assertion 101 {obelisk_sim.concurrent_attempt_enable}
// LOWER-NEXT: cf.cond_br %[[ENABLED]], ^[[START:bb[0-9]+]], ^[[AFTER_START:bb[0-9]+]]({{.*}} : i64)
// LOWER: ^[[AFTER_START]]({{.*}}: i64):
// LOWER: obelisk_sim.ref.store
// LOWER: ^[[START]]:

// Initializers execute in declaration order. Thus y's initializer observes
// x's sampled initializer value. The successful antecedent then applies its
// match assignment to x, while y retains its distinct per-attempt value.
// LOWER: %[[INIT_X:.*]] = obelisk_sim.assert.sampled_read %arg0 from %arg2
// LOWER: %[[ANTECEDENT:.*]] = obelisk_sim.assert.sampled_read %arg0 from %arg2
// LOWER: obelisk_sim.spawn @[[PASS_CALLBACK]]
// LOWER: %[[MATCH_SOURCE:.*]] = obelisk_sim.assert.sampled_read %arg0 from %arg2
// LOWER: %[[MATCH_X:.*]] = obelisk_sim.logic.unary logical_not %[[MATCH_SOURCE]]
// LOWER-NOT: obelisk_sim.display
// LOWER: obelisk_sim.spawn @[[MATCH_CALL]](%arg0, %[[MATCH_X]])
// LOWER-NOT: obelisk_sim.spawn @[[PASS_CALLBACK]]
// LOWER: obelisk_sim.ref.store %[[MATCH_X]] to %[[X_AGE1:.*]]
// LOWER-NEXT: obelisk_sim.ref.store %[[INIT_X]] to %[[Y_AGE1:.*]]
// LOWER-NOT: obelisk.sv.
