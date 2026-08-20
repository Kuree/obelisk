// RUN: obelisk-opt %s '--lower-obelisk-to-sim=opt-level=0' | FileCheck %s
// RUN: obelisk-opt %s '--lower-obelisk-to-sim=opt-level=3' -o /dev/null
// RUN: obelisk-opt %s '--lower-obelisk-to-sim=opt-level=3' '--encode-obelisk-sim-to-bytecode=vpi=off' -o /dev/null
// RUN: obelisk-opt %s '--lower-obelisk-to-sim=opt-level=0' -o %t.threaded
// RUN: obelisk-opt %s '--lower-obelisk-to-sim=opt-level=0' --mlir-disable-threading -o %t.serial
// RUN: diff %t.threaded %t.serial

module attributes {llvm.data_layout = "e-m:e-p:64:64-i64:64-n8:16:32:64-S128", llvm.target_triple = "x86_64-unknown-linux-gnu"} {
  obelisk.sv.symbol.definition attributes {definition_kind = 0 : i32, hierarchical_name = "top", name = "top", node_id = 0 : i64, sym_name = "s0.top"} {
  }
  obelisk.sv.symbol.root attributes {hierarchical_name = "\\$root ", name = "$root", node_id = 1 : i64, sym_name = "s1.$root"} {
    obelisk.sv.symbol.compilation_unit attributes {hierarchical_name = "$unit", node_id = 2 : i64, sym_name = "s2"} {
    }
    obelisk.sv.symbol.instance attributes {hierarchical_name = "top", is_uninstantiated = false, name = "top", node_id = 3 : i64, referenced_path = "top", referenced_symbol = @s0.top, sym_name = "s3.top"} {
      obelisk.sv.symbol.instance_body attributes {hierarchical_name = "top", name = "top", node_id = 4 : i64, sym_name = "s4.top", time_precision_fs = 1000000 : i64, time_unit_fs = 1000000 : i64} {
        obelisk.sv.symbol.port attributes {direction = 0 : i32, hierarchical_name = "top.clk", name = "clk", node_id = 5 : i64, semantic_type = !obelisk.integral<1, false, true, 0 : 0, logic>, sym_name = "s5.clk"} {
        }
        obelisk.sv.symbol.variable attributes {hierarchical_name = "top.clk", lifetime = 1 : i32, name = "clk", node_id = 6 : i64, semantic_type = !obelisk.integral<1, false, true, 0 : 0, logic>, sym_name = "s6.clk"} {
        }
        obelisk.sv.symbol.port attributes {direction = 0 : i32, hierarchical_name = "top.a", name = "a", node_id = 7 : i64, semantic_type = !obelisk.integral<1, false, true, 0 : 0, logic>, sym_name = "s7.a"} {
        }
        obelisk.sv.symbol.variable attributes {hierarchical_name = "top.a", lifetime = 1 : i32, name = "a", node_id = 8 : i64, semantic_type = !obelisk.integral<1, false, true, 0 : 0, logic>, sym_name = "s8.a"} {
        }
        obelisk.sv.symbol.port attributes {direction = 0 : i32, hierarchical_name = "top.b", name = "b", node_id = 9 : i64, semantic_type = !obelisk.integral<1, false, true, 0 : 0, logic>, sym_name = "s9.b"} {
        }
        obelisk.sv.symbol.variable attributes {hierarchical_name = "top.b", lifetime = 1 : i32, name = "b", node_id = 10 : i64, semantic_type = !obelisk.integral<1, false, true, 0 : 0, logic>, sym_name = "s10.b"} {
        }
        obelisk.sv.symbol.port attributes {direction = 0 : i32, hierarchical_name = "top.c", name = "c", node_id = 11 : i64, semantic_type = !obelisk.integral<1, false, true, 0 : 0, logic>, sym_name = "s11.c"} {
        }
        obelisk.sv.symbol.variable attributes {hierarchical_name = "top.c", lifetime = 1 : i32, name = "c", node_id = 12 : i64, semantic_type = !obelisk.integral<1, false, true, 0 : 0, logic>, sym_name = "s12.c"} {
        }
        obelisk.sv.symbol.sequence attributes {has_default_instance = false, hierarchical_name = "top.s", name = "s", node_id = 13 : i64, port_count = 2 : i64, port_paths = ["top.s.x", "top.s.y"], port_symbols = [@s1.$root::@s3.top::@s4.top::@s13.s::@s14.x, @s1.$root::@s3.top::@s4.top::@s13.s::@s15.y], sym_name = "s13.s"} {
          obelisk.sv.symbol.assertion_port attributes {has_default_value = false, hierarchical_name = "top.s.x", is_local_variable = false, name = "x", node_id = 14 : i64, semantic_type = !obelisk.integral<1, false, true, 0 : 0, logic>, sym_name = "s14.x"} {
          }
          obelisk.sv.symbol.assertion_port attributes {has_default_value = true, hierarchical_name = "top.s.y", is_local_variable = false, name = "y", node_id = 15 : i64, semantic_type = !obelisk.integral<1, false, true, 0 : 0, logic>, sym_name = "s15.y"} {
          }
        }
        obelisk.sv.symbol.property attributes {has_default_instance = false, hierarchical_name = "top.p", name = "p", node_id = 16 : i64, port_count = 2 : i64, port_paths = ["top.p.x", "top.p.y"], port_symbols = [@s1.$root::@s3.top::@s4.top::@s16.p::@s17.x, @s1.$root::@s3.top::@s4.top::@s16.p::@s18.y], sym_name = "s16.p"} {
          obelisk.sv.symbol.assertion_port attributes {has_default_value = false, hierarchical_name = "top.p.x", is_local_variable = false, name = "x", node_id = 17 : i64, semantic_type = !obelisk.integral<1, false, true, 0 : 0, logic>, sym_name = "s17.x"} {
          }
          obelisk.sv.symbol.assertion_port attributes {has_default_value = true, hierarchical_name = "top.p.y", is_local_variable = false, name = "y", node_id = 18 : i64, semantic_type = !obelisk.integral<1, false, true, 0 : 0, logic>, sym_name = "s18.y"} {
          }
        }
        obelisk.sv.symbol.statement_block attributes {block_kind = 0 : i32, hierarchical_name = "top.ap", name = "ap", node_id = 19 : i64, sym_name = "s19.ap"} {
        }
        obelisk.sv.symbol.procedural_block attributes {hierarchical_name = "top", node_id = 20 : i64, procedure_kind = 2 : i32, sym_name = "s20", time_precision_fs = 1000000 : i64, time_unit_fs = 1000000 : i64} {
          obelisk.sv.statement.block attributes {block_path = "top.ap", block_symbol = @s1.$root::@s3.top::@s4.top::@s19.ap, node_id = 21 : i64} {
            obelisk.sv.statement.concurrent_assertion attributes {assertion_kind = 0 : i32, has_default_disable = false, has_fail_action = false, has_pass_action = true, node_id = 22 : i64} {
              obelisk.sv.assertion.clocking attributes {node_id = 23 : i64} {
                obelisk.sv.timing.signal_event attributes {edge_kind = 1 : i32, has_iff = false, node_id = 24 : i64} {
                  obelisk.sv.expression.named_value attributes {is_signed = false, node_id = 25 : i64, referenced_path = "top.clk", referenced_symbol = @s1.$root::@s3.top::@s4.top::@s6.clk, semantic_type = !obelisk.integral<1, false, true, 0 : 0, logic>} {
                  }
                }
                obelisk.sv.assertion.simple attributes {has_repetition = false, is_null = false, node_id = 26 : i64, repetition_is_unbounded = false} {
                  obelisk.sv.expression.assertion_instance attributes {argument_count = 2 : i64, argument_formal_paths = ["top.p.x", "top.p.y"], argument_formal_symbols = [@s1.$root::@s3.top::@s4.top::@s16.p::@s17.x, @s1.$root::@s3.top::@s4.top::@s16.p::@s18.y], argument_kinds = array<i64: 0, 0>, has_expanded_body = true, is_recursive_property = false, is_signed = false, local_variable_count = 0 : i64, local_variable_has_initializer = array<i64>, local_variable_paths = [], local_variable_symbols = [], node_id = 27 : i64, referenced_path = "top.p", referenced_symbol = @s1.$root::@s3.top::@s4.top::@s16.p, semantic_type = !obelisk.property} {
                    obelisk.sv.assertion.binary attributes {node_id = 28 : i64, operator_kind = 12 : i32} {
                      obelisk.sv.assertion.simple attributes {has_repetition = false, is_null = false, node_id = 29 : i64, repetition_is_unbounded = false} {
                        obelisk.sv.expression.named_value attributes {is_signed = false, node_id = 30 : i64, referenced_path = "top.a", referenced_symbol = @s1.$root::@s3.top::@s4.top::@s8.a, semantic_type = !obelisk.integral<1, false, true, 0 : 0, logic>} {
                        }
                      }
                      obelisk.sv.assertion.simple attributes {has_repetition = false, is_null = false, node_id = 31 : i64, repetition_is_unbounded = false} {
                        obelisk.sv.expression.assertion_instance attributes {argument_count = 2 : i64, argument_formal_paths = ["top.s.x", "top.s.y"], argument_formal_symbols = [@s1.$root::@s3.top::@s4.top::@s13.s::@s14.x, @s1.$root::@s3.top::@s4.top::@s13.s::@s15.y], argument_kinds = array<i64: 0, 0>, has_expanded_body = true, is_recursive_property = false, is_signed = false, local_variable_count = 0 : i64, local_variable_has_initializer = array<i64>, local_variable_paths = [], local_variable_symbols = [], node_id = 32 : i64, referenced_path = "top.s", referenced_symbol = @s1.$root::@s3.top::@s4.top::@s13.s, semantic_type = !obelisk.sequence} {
                          obelisk.sv.assertion.sequence_concat attributes {delays = [{is_unbounded = false, max = 0 : i64, min = 0 : i64}, {is_unbounded = false, max = 1 : i64, min = 1 : i64}], node_id = 33 : i64} {
                            obelisk.sv.assertion.simple attributes {has_repetition = false, is_null = false, node_id = 34 : i64, repetition_is_unbounded = false} {
                              obelisk.sv.expression.named_value attributes {is_signed = false, node_id = 35 : i64, referenced_path = "top.c", referenced_symbol = @s1.$root::@s3.top::@s4.top::@s12.c, semantic_type = !obelisk.integral<1, false, true, 0 : 0, logic>} {
                              }
                            }
                            obelisk.sv.assertion.simple attributes {has_repetition = false, is_null = false, node_id = 36 : i64, repetition_is_unbounded = false} {
                              obelisk.sv.expression.named_value attributes {is_signed = false, node_id = 37 : i64, referenced_path = "top.b", referenced_symbol = @s1.$root::@s3.top::@s4.top::@s10.b, semantic_type = !obelisk.integral<1, false, true, 0 : 0, logic>} {
                              }
                            }
                          }
                          obelisk.sv.expression.named_value attributes {is_signed = false, node_id = 38 : i64, referenced_path = "top.c", referenced_symbol = @s1.$root::@s3.top::@s4.top::@s12.c, semantic_type = !obelisk.integral<1, false, true, 0 : 0, logic>} {
                          }
                          obelisk.sv.expression.named_value attributes {is_signed = false, node_id = 39 : i64, referenced_path = "top.b", referenced_symbol = @s1.$root::@s3.top::@s4.top::@s10.b, semantic_type = !obelisk.integral<1, false, true, 0 : 0, logic>} {
                          }
                        }
                      }
                    }
                    obelisk.sv.expression.named_value attributes {is_signed = false, node_id = 40 : i64, referenced_path = "top.a", referenced_symbol = @s1.$root::@s3.top::@s4.top::@s8.a, semantic_type = !obelisk.integral<1, false, true, 0 : 0, logic>} {
                    }
                    obelisk.sv.expression.named_value attributes {is_signed = false, node_id = 41 : i64, referenced_path = "top.c", referenced_symbol = @s1.$root::@s3.top::@s4.top::@s12.c, semantic_type = !obelisk.integral<1, false, true, 0 : 0, logic>} {
                    }
                  }
                }
              }
              obelisk.sv.statement.empty attributes {node_id = 42 : i64} {
              }
            }
          }
        }
      }
    }
  }
}

// The expanded property body substitutes p(a)'s explicit a/default c, then
// the nested s(c)'s explicit c/default b. Retained actual/default children are
// semantic inventory and must not be evaluated again.
// CHECK-LABEL: obelisk_sim.func private @unit_0(
// CHECK-SAME: %[[CLK:[^:]+]]: !obelisk_sim.ref<!obelisk_sim.logic<1>>
// CHECK-SAME: %[[A:[^:]+]]: !obelisk_sim.ref<!obelisk_sim.logic<1>>
// CHECK-SAME: %[[B:[^:]+]]: !obelisk_sim.ref<!obelisk_sim.logic<1>>
// CHECK-SAME: %[[C:[^:]+]]: !obelisk_sim.ref<!obelisk_sim.logic<1>>
// CHECK: obelisk_sim.assert.sampled_read {{.*}} from %[[C]]
// CHECK: obelisk_sim.assert.sampled_read {{.*}} from %[[B]]
// CHECK: obelisk_sim.assert.sampled_read {{.*}} from %[[A]]
// CHECK-NOT: obelisk_sim.assert.sampled_read
