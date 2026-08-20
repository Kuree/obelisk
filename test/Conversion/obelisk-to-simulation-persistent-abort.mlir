// RUN: obelisk-opt %s '--lower-obelisk-to-sim=opt-level=0' | FileCheck %s
// RUN: obelisk-opt %s '--lower-obelisk-to-sim=opt-level=3' -o /dev/null
// RUN: obelisk-opt %s '--lower-obelisk-to-sim=opt-level=3' '--encode-obelisk-sim-to-bytecode=vpi=off' -o /dev/null
// RUN: obelisk-opt %s '--lower-obelisk-to-sim=opt-level=3' --convert-obelisk-sim-processes-to-llvm-coroutines -o /dev/null
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
        obelisk.sv.symbol.port attributes {direction = 0 : i32, hierarchical_name = "top.abort_cond", name = "abort_cond", node_id = 7 : i64, semantic_type = !obelisk.integral<1, false, true, 0 : 0, logic>, sym_name = "s7.abort_cond"} {
        }
        obelisk.sv.symbol.variable attributes {hierarchical_name = "top.abort_cond", lifetime = 1 : i32, name = "abort_cond", node_id = 8 : i64, semantic_type = !obelisk.integral<1, false, true, 0 : 0, logic>, sym_name = "s8.abort_cond"} {
        }
        obelisk.sv.symbol.port attributes {direction = 0 : i32, hierarchical_name = "top.a", name = "a", node_id = 9 : i64, semantic_type = !obelisk.integral<1, false, true, 0 : 0, logic>, sym_name = "s9.a"} {
        }
        obelisk.sv.symbol.variable attributes {hierarchical_name = "top.a", lifetime = 1 : i32, name = "a", node_id = 10 : i64, semantic_type = !obelisk.integral<1, false, true, 0 : 0, logic>, sym_name = "s10.a"} {
        }
        obelisk.sv.symbol.port attributes {direction = 0 : i32, hierarchical_name = "top.b", name = "b", node_id = 11 : i64, semantic_type = !obelisk.integral<1, false, true, 0 : 0, logic>, sym_name = "s11.b"} {
        }
        obelisk.sv.symbol.variable attributes {hierarchical_name = "top.b", lifetime = 1 : i32, name = "b", node_id = 12 : i64, semantic_type = !obelisk.integral<1, false, true, 0 : 0, logic>, sym_name = "s12.b"} {
        }
        obelisk.sv.symbol.port attributes {direction = 1 : i32, hierarchical_name = "top.hit", name = "hit", node_id = 13 : i64, semantic_type = !obelisk.integral<32, true, false, 31 : 0, int>, sym_name = "s13.hit"} {
        }
        obelisk.sv.symbol.variable attributes {hierarchical_name = "top.hit", lifetime = 1 : i32, name = "hit", node_id = 14 : i64, semantic_type = !obelisk.integral<32, true, false, 31 : 0, int>, sym_name = "s14.hit"} {
        }
        obelisk.sv.symbol.statement_block attributes {block_kind = 0 : i32, hierarchical_name = "top.a0", name = "a0", node_id = 15 : i64, sym_name = "s15.a0"} {
        }
        obelisk.sv.symbol.procedural_block attributes {hierarchical_name = "top", node_id = 16 : i64, procedure_kind = 2 : i32, sym_name = "s16", time_precision_fs = 1000000 : i64, time_unit_fs = 1000000 : i64} {
          obelisk.sv.statement.block attributes {block_path = "top.a0", block_symbol = @s1.$root::@s3.top::@s4.top::@s15.a0, node_id = 17 : i64} {
            obelisk.sv.statement.concurrent_assertion attributes {assertion_kind = 0 : i32, has_default_disable = false, has_fail_action = true, has_pass_action = true, node_id = 18 : i64} {
              obelisk.sv.assertion.clocking attributes {node_id = 19 : i64} {
                obelisk.sv.timing.signal_event attributes {edge_kind = 1 : i32, has_iff = false, node_id = 20 : i64} {
                  obelisk.sv.expression.named_value attributes {is_signed = false, node_id = 21 : i64, referenced_path = "top.clk", referenced_symbol = @s1.$root::@s3.top::@s4.top::@s6.clk, semantic_type = !obelisk.integral<1, false, true, 0 : 0, logic>} {
                  }
                }
                obelisk.sv.assertion.abort attributes {action = 0 : i32, is_synchronous = false, node_id = 22 : i64} {
                  obelisk.sv.expression.named_value attributes {is_signed = false, node_id = 23 : i64, referenced_path = "top.abort_cond", referenced_symbol = @s1.$root::@s3.top::@s4.top::@s8.abort_cond, semantic_type = !obelisk.integral<1, false, true, 0 : 0, logic>} {
                  }
                  obelisk.sv.assertion.sequence_concat attributes {delays = [{is_unbounded = false, max = 0 : i64, min = 0 : i64}, {is_unbounded = true, min = 2 : i64}], node_id = 24 : i64} {
                    obelisk.sv.assertion.simple attributes {has_repetition = false, is_null = false, node_id = 25 : i64, repetition_is_unbounded = false} {
                      obelisk.sv.expression.named_value attributes {is_signed = false, node_id = 26 : i64, referenced_path = "top.a", referenced_symbol = @s1.$root::@s3.top::@s4.top::@s10.a, semantic_type = !obelisk.integral<1, false, true, 0 : 0, logic>} {
                      }
                    }
                    obelisk.sv.assertion.simple attributes {has_repetition = false, is_null = false, node_id = 27 : i64, repetition_is_unbounded = false} {
                      obelisk.sv.expression.named_value attributes {is_signed = false, node_id = 28 : i64, referenced_path = "top.b", referenced_symbol = @s1.$root::@s3.top::@s4.top::@s12.b, semantic_type = !obelisk.integral<1, false, true, 0 : 0, logic>} {
                      }
                    }
                  }
                }
              }
              obelisk.sv.statement.expression_statement attributes {node_id = 29 : i64} {
                obelisk.sv.expression.unary_op attributes {is_signed = true, node_id = 30 : i64, operator_kind = 12 : i32, semantic_type = !obelisk.integral<32, true, false, 31 : 0, int>} {
                  obelisk.sv.expression.named_value attributes {is_signed = true, node_id = 31 : i64, referenced_path = "top.hit", referenced_symbol = @s1.$root::@s3.top::@s4.top::@s14.hit, semantic_type = !obelisk.integral<32, true, false, 31 : 0, int>} {
                  }
                }
              }
              obelisk.sv.statement.expression_statement attributes {node_id = 32 : i64} {
                obelisk.sv.expression.unary_op attributes {is_signed = true, node_id = 33 : i64, operator_kind = 13 : i32, semantic_type = !obelisk.integral<32, true, false, 31 : 0, int>} {
                  obelisk.sv.expression.named_value attributes {is_signed = true, node_id = 34 : i64, referenced_path = "top.hit", referenced_symbol = @s1.$root::@s3.top::@s4.top::@s14.hit, semantic_type = !obelisk.integral<32, true, false, 31 : 0, int>} {
                  }
                }
              }
            }
          }
        }
        obelisk.sv.symbol.statement_block attributes {block_kind = 0 : i32, hierarchical_name = "top.a1", name = "a1", node_id = 35 : i64, sym_name = "s17.a1"} {
        }
        obelisk.sv.symbol.procedural_block attributes {hierarchical_name = "top", node_id = 36 : i64, procedure_kind = 2 : i32, sym_name = "s18", time_precision_fs = 1000000 : i64, time_unit_fs = 1000000 : i64} {
          obelisk.sv.statement.block attributes {block_path = "top.a1", block_symbol = @s1.$root::@s3.top::@s4.top::@s17.a1, node_id = 37 : i64} {
            obelisk.sv.statement.concurrent_assertion attributes {assertion_kind = 0 : i32, has_default_disable = false, has_fail_action = true, has_pass_action = true, node_id = 38 : i64} {
              obelisk.sv.assertion.clocking attributes {node_id = 39 : i64} {
                obelisk.sv.timing.signal_event attributes {edge_kind = 1 : i32, has_iff = false, node_id = 40 : i64} {
                  obelisk.sv.expression.named_value attributes {is_signed = false, node_id = 41 : i64, referenced_path = "top.clk", referenced_symbol = @s1.$root::@s3.top::@s4.top::@s6.clk, semantic_type = !obelisk.integral<1, false, true, 0 : 0, logic>} {
                  }
                }
                obelisk.sv.assertion.abort attributes {action = 1 : i32, is_synchronous = true, node_id = 42 : i64} {
                  obelisk.sv.expression.named_value attributes {is_signed = false, node_id = 43 : i64, referenced_path = "top.abort_cond", referenced_symbol = @s1.$root::@s3.top::@s4.top::@s8.abort_cond, semantic_type = !obelisk.integral<1, false, true, 0 : 0, logic>} {
                  }
                  obelisk.sv.assertion.unary attributes {has_range = true, node_id = 44 : i64, operator_kind = 6 : i32, range_is_unbounded = true, range_min = 1 : i64} {
                    obelisk.sv.assertion.simple attributes {has_repetition = false, is_null = false, node_id = 45 : i64, repetition_is_unbounded = false} {
                      obelisk.sv.expression.named_value attributes {is_signed = false, node_id = 46 : i64, referenced_path = "top.a", referenced_symbol = @s1.$root::@s3.top::@s4.top::@s10.a, semantic_type = !obelisk.integral<1, false, true, 0 : 0, logic>} {
                      }
                    }
                  }
                }
              }
              obelisk.sv.statement.expression_statement attributes {node_id = 47 : i64} {
                obelisk.sv.expression.unary_op attributes {is_signed = true, node_id = 48 : i64, operator_kind = 12 : i32, semantic_type = !obelisk.integral<32, true, false, 31 : 0, int>} {
                  obelisk.sv.expression.named_value attributes {is_signed = true, node_id = 49 : i64, referenced_path = "top.hit", referenced_symbol = @s1.$root::@s3.top::@s4.top::@s14.hit, semantic_type = !obelisk.integral<32, true, false, 31 : 0, int>} {
                  }
                }
              }
              obelisk.sv.statement.expression_statement attributes {node_id = 50 : i64} {
                obelisk.sv.expression.unary_op attributes {is_signed = true, node_id = 51 : i64, operator_kind = 13 : i32, semantic_type = !obelisk.integral<32, true, false, 31 : 0, int>} {
                  obelisk.sv.expression.named_value attributes {is_signed = true, node_id = 52 : i64, referenced_path = "top.hit", referenced_symbol = @s1.$root::@s3.top::@s4.top::@s14.hit, semantic_type = !obelisk.integral<32, true, false, 31 : 0, int>} {
                  }
                }
              }
            }
          }
        }
        obelisk.sv.symbol.statement_block attributes {block_kind = 0 : i32, hierarchical_name = "top.a2", name = "a2", node_id = 53 : i64, sym_name = "s19.a2"} {
        }
        obelisk.sv.symbol.procedural_block attributes {hierarchical_name = "top", node_id = 54 : i64, procedure_kind = 2 : i32, sym_name = "s20", time_precision_fs = 1000000 : i64, time_unit_fs = 1000000 : i64} {
          obelisk.sv.statement.block attributes {block_path = "top.a2", block_symbol = @s1.$root::@s3.top::@s4.top::@s19.a2, node_id = 55 : i64} {
            obelisk.sv.statement.concurrent_assertion attributes {assertion_kind = 0 : i32, has_default_disable = false, has_fail_action = true, has_pass_action = true, node_id = 56 : i64} {
              obelisk.sv.assertion.clocking attributes {node_id = 57 : i64} {
                obelisk.sv.timing.signal_event attributes {edge_kind = 1 : i32, has_iff = false, node_id = 58 : i64} {
                  obelisk.sv.expression.named_value attributes {is_signed = false, node_id = 59 : i64, referenced_path = "top.clk", referenced_symbol = @s1.$root::@s3.top::@s4.top::@s6.clk, semantic_type = !obelisk.integral<1, false, true, 0 : 0, logic>} {
                  }
                }
                obelisk.sv.assertion.abort attributes {action = 1 : i32, is_synchronous = false, node_id = 60 : i64} {
                  obelisk.sv.expression.named_value attributes {is_signed = false, node_id = 61 : i64, referenced_path = "top.abort_cond", referenced_symbol = @s1.$root::@s3.top::@s4.top::@s8.abort_cond, semantic_type = !obelisk.integral<1, false, true, 0 : 0, logic>} {
                  }
                  obelisk.sv.assertion.binary attributes {node_id = 62 : i64, operator_kind = 7 : i32} {
                    obelisk.sv.assertion.simple attributes {has_repetition = false, is_null = false, node_id = 63 : i64, repetition_is_unbounded = false} {
                      obelisk.sv.expression.named_value attributes {is_signed = false, node_id = 64 : i64, referenced_path = "top.a", referenced_symbol = @s1.$root::@s3.top::@s4.top::@s10.a, semantic_type = !obelisk.integral<1, false, true, 0 : 0, logic>} {
                      }
                    }
                    obelisk.sv.assertion.simple attributes {has_repetition = false, is_null = false, node_id = 65 : i64, repetition_is_unbounded = false} {
                      obelisk.sv.expression.named_value attributes {is_signed = false, node_id = 66 : i64, referenced_path = "top.b", referenced_symbol = @s1.$root::@s3.top::@s4.top::@s12.b, semantic_type = !obelisk.integral<1, false, true, 0 : 0, logic>} {
                      }
                    }
                  }
                }
              }
              obelisk.sv.statement.expression_statement attributes {node_id = 67 : i64} {
                obelisk.sv.expression.unary_op attributes {is_signed = true, node_id = 68 : i64, operator_kind = 12 : i32, semantic_type = !obelisk.integral<32, true, false, 31 : 0, int>} {
                  obelisk.sv.expression.named_value attributes {is_signed = true, node_id = 69 : i64, referenced_path = "top.hit", referenced_symbol = @s1.$root::@s3.top::@s4.top::@s14.hit, semantic_type = !obelisk.integral<32, true, false, 31 : 0, int>} {
                  }
                }
              }
              obelisk.sv.statement.expression_statement attributes {node_id = 70 : i64} {
                obelisk.sv.expression.unary_op attributes {is_signed = true, node_id = 71 : i64, operator_kind = 13 : i32, semantic_type = !obelisk.integral<32, true, false, 31 : 0, int>} {
                  obelisk.sv.expression.named_value attributes {is_signed = true, node_id = 72 : i64, referenced_path = "top.hit", referenced_symbol = @s1.$root::@s3.top::@s4.top::@s14.hit, semantic_type = !obelisk.integral<32, true, false, 31 : 0, int>} {
                  }
                }
              }
            }
          }
        }
        obelisk.sv.symbol.statement_block attributes {block_kind = 0 : i32, hierarchical_name = "top.a3", name = "a3", node_id = 73 : i64, sym_name = "s21.a3"} {
        }
        obelisk.sv.symbol.procedural_block attributes {hierarchical_name = "top", node_id = 74 : i64, procedure_kind = 2 : i32, sym_name = "s22", time_precision_fs = 1000000 : i64, time_unit_fs = 1000000 : i64} {
          obelisk.sv.statement.block attributes {block_path = "top.a3", block_symbol = @s1.$root::@s3.top::@s4.top::@s21.a3, node_id = 75 : i64} {
            obelisk.sv.statement.concurrent_assertion attributes {assertion_kind = 2 : i32, has_default_disable = false, has_fail_action = true, has_pass_action = true, node_id = 76 : i64} {
              obelisk.sv.assertion.clocking attributes {node_id = 77 : i64} {
                obelisk.sv.timing.signal_event attributes {edge_kind = 1 : i32, has_iff = false, node_id = 78 : i64} {
                  obelisk.sv.expression.named_value attributes {is_signed = false, node_id = 79 : i64, referenced_path = "top.clk", referenced_symbol = @s1.$root::@s3.top::@s4.top::@s6.clk, semantic_type = !obelisk.integral<1, false, true, 0 : 0, logic>} {
                  }
                }
                obelisk.sv.assertion.abort attributes {action = 0 : i32, is_synchronous = true, node_id = 80 : i64} {
                  obelisk.sv.expression.named_value attributes {is_signed = false, node_id = 81 : i64, referenced_path = "top.abort_cond", referenced_symbol = @s1.$root::@s3.top::@s4.top::@s8.abort_cond, semantic_type = !obelisk.integral<1, false, true, 0 : 0, logic>} {
                  }
                  obelisk.sv.assertion.sequence_concat attributes {delays = [{is_unbounded = false, max = 0 : i64, min = 0 : i64}, {is_unbounded = false, max = 1 : i64, min = 1 : i64}], node_id = 82 : i64} {
                    obelisk.sv.assertion.simple attributes {has_repetition = true, is_null = false, node_id = 83 : i64, repetition_is_unbounded = true, repetition_kind = 2 : i32, repetition_min = 2 : i64} {
                      obelisk.sv.expression.named_value attributes {is_signed = false, node_id = 84 : i64, referenced_path = "top.a", referenced_symbol = @s1.$root::@s3.top::@s4.top::@s10.a, semantic_type = !obelisk.integral<1, false, true, 0 : 0, logic>} {
                      }
                    }
                    obelisk.sv.assertion.simple attributes {has_repetition = false, is_null = false, node_id = 85 : i64, repetition_is_unbounded = false} {
                      obelisk.sv.expression.named_value attributes {is_signed = false, node_id = 86 : i64, referenced_path = "top.b", referenced_symbol = @s1.$root::@s3.top::@s4.top::@s12.b, semantic_type = !obelisk.integral<1, false, true, 0 : 0, logic>} {
                      }
                    }
                  }
                }
              }
              obelisk.sv.statement.expression_statement attributes {node_id = 87 : i64} {
                obelisk.sv.expression.unary_op attributes {is_signed = true, node_id = 88 : i64, operator_kind = 12 : i32, semantic_type = !obelisk.integral<32, true, false, 31 : 0, int>} {
                  obelisk.sv.expression.named_value attributes {is_signed = true, node_id = 89 : i64, referenced_path = "top.hit", referenced_symbol = @s1.$root::@s3.top::@s4.top::@s14.hit, semantic_type = !obelisk.integral<32, true, false, 31 : 0, int>} {
                  }
                }
              }
              obelisk.sv.statement.expression_statement attributes {node_id = 90 : i64} {
                obelisk.sv.expression.unary_op attributes {is_signed = true, node_id = 91 : i64, operator_kind = 13 : i32, semantic_type = !obelisk.integral<32, true, false, 31 : 0, int>} {
                  obelisk.sv.expression.named_value attributes {is_signed = true, node_id = 92 : i64, referenced_path = "top.hit", referenced_symbol = @s1.$root::@s3.top::@s4.top::@s14.hit, semantic_type = !obelisk.integral<32, true, false, 31 : 0, int>} {
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

// Async accept_on over a final ##[2:$] delay sums the eligible count and the
// warm-up bitset, clears both synchronously, then dispatches one vacuous pass
// action per aborted attempt. The bitset popcount stays compiler-generated
// arith/cf SSA so the same dispatcher is executable in design bytecode.
// CHECK-LABEL: obelisk_sim.func private @unit_0.$concurrent_abort_count.18.accept(
// CHECK-SAME: obelisk_sim.concurrent_abort_counted
// CHECK: [[DELAY_ZERO:%.*]] = arith.constant 0 : i64
// CHECK: [[COUNT:%.*]] = obelisk_sim.ref.load %arg1
// CHECK: [[COUNT_TOTAL:%.*]] = arith.addi %arg4, [[COUNT]] : i64
// CHECK: obelisk_sim.ref.store [[DELAY_ZERO]] to %arg1
// CHECK: [[BITS:%.*]] = obelisk_sim.ref.load %arg2
// CHECK: obelisk_sim.ref.store [[DELAY_ZERO]] to %arg2
// CHECK: cf.br [[BIT_LOOP:\^bb[0-9]+]]([[BITS]], [[COUNT_TOTAL]] : i64, i64)
// CHECK: [[BIT_LOOP]]([[REMAIN:%.*]]: i64, [[ACCUM:%.*]]: i64):
// CHECK: cf.cond_br {{.*}}, [[BIT_BODY:\^bb[0-9]+]], [[BIT_DONE:\^bb[0-9]+]]([[ACCUM]] : i64)
// CHECK: [[BIT_BODY]]:
// CHECK: [[BIT_ONE:%.*]] = arith.constant {{.*}} 1 : i64
// CHECK: [[DEC:%.*]] = arith.subi [[REMAIN]], [[BIT_ONE]] : i64
// CHECK: [[NEXT_BITS:%.*]] = arith.andi [[REMAIN]], [[DEC]] : i64
// CHECK: [[TOTAL_ONE:%.*]] = arith.constant {{.*}} 1 : i64
// CHECK: [[NEXT_TOTAL:%.*]] = arith.addi [[ACCUM]], [[TOTAL_ONE]] : i64
// CHECK: cf.br [[BIT_LOOP]]([[NEXT_BITS]], [[NEXT_TOTAL]] : i64, i64)
// CHECK: [[BIT_DONE]]([[POPCOUNT_TOTAL:%.*]]: i64):
// CHECK: arith.cmpi ne, [[POPCOUNT_TOTAL]],
// CHECK: obelisk_sim.spawn @unit_0.fork.18.0.0
// CHECK-LABEL: obelisk_sim.func private @unit_0.$concurrent_abort.18(
// CHECK-SAME: obelisk_sim.concurrent_abort
// CHECK-SAME: obelisk_sim.concurrent_abort_counted
// CHECK-SAME: obelisk_sim.priority_signal_resume
// CHECK: obelisk_sim.observer.bind
// CHECK-SAME: values(%arg1, %arg2 : !obelisk_sim.ref<!obelisk_sim.logic<1>>, !obelisk_sim.event) captures 1
// CHECK: obelisk_sim.suspend.observe
// CHECK-SAME: obelisk_sim.concurrent_abort_level_true
// CHECK: [[ASYNC_ZERO:%.*]] = arith.constant {{.*}} 0 : i64
// CHECK: obelisk_sim.call @unit_0.$concurrent_abort_count.18.accept
// CHECK-SAME: [[ASYNC_ZERO]])
// CHECK-LABEL: obelisk_sim.func private @unit_0(
// CHECK-SAME: obelisk_sim.asynchronous_property_abort
// CHECK-SAME: obelisk_sim.persistent_delay_monitor
// CHECK-SAME: obelisk_sim.property_abort_action = "accept"
// CHECK: [[PREPONED_DELAY:%.*]] = obelisk_sim.context.event %arg0[2305843009213693952]
// CHECK: obelisk_sim.spawn @unit_0.$concurrent_abort.18
// CHECK-SAME: [[PREPONED_DELAY]]
// CHECK: obelisk_sim.suspend.edge
// The clocked already-true path uses the same Preponed sampled condition.
// CHECK: obelisk_sim.assert.sampled_read %arg0 from %arg2
// CHECK: [[CLOCK_ONE:%.*]] = arith.constant {{.*}} 1 : i64
// CHECK: obelisk_sim.call @unit_0.$concurrent_abort_count.18.accept
// CHECK-SAME: [[CLOCK_ONE]])
// CHECK: obelisk_sim.assert.sampled_read

// sync_reject_on over s_eventually reads the abort predicate from the
// Preponed snapshot and clears both aggregate counters before failing them.
// CHECK-LABEL: obelisk_sim.func private @unit_1.$concurrent_abort_count.38.reject(
// CHECK-SAME: obelisk_sim.concurrent_abort_counted
// CHECK: [[UNARY_ZERO:%.*]] = arith.constant 0 : i64
// CHECK: obelisk_sim.ref.load %arg1
// CHECK: obelisk_sim.ref.store [[UNARY_ZERO]] to %arg1
// CHECK: obelisk_sim.ref.load %arg2
// CHECK: obelisk_sim.ref.store [[UNARY_ZERO]] to %arg2
// CHECK: obelisk_sim.spawn @unit_1.fork.38.1.1
// CHECK-LABEL: obelisk_sim.func private @unit_1(
// CHECK-SAME: obelisk_sim.persistent_unary_kind = "s_eventually"
// CHECK-SAME: obelisk_sim.property_abort_action = "reject"
// CHECK-SAME: obelisk_sim.synchronous_property_abort
// CHECK: obelisk_sim.suspend.edge
// CHECK: obelisk_sim.assert.sampled_read %arg0 from %arg2
// CHECK: obelisk_sim.call @unit_1.$concurrent_abort_count.38.reject

// Async reject_on over strong until shares the same dispatcher between its
// priority Reactive observer and its clocked already-true path.
// CHECK-LABEL: obelisk_sim.func private @unit_2.$concurrent_abort_count.56.reject(
// CHECK-SAME: obelisk_sim.concurrent_abort_counted
// CHECK: [[UNTIL_ZERO:%.*]] = arith.constant 0 : i64
// CHECK: obelisk_sim.ref.load
// CHECK: obelisk_sim.ref.store [[UNTIL_ZERO]]
// CHECK: obelisk_sim.spawn @unit_2.fork.56.1.1
// CHECK-LABEL: obelisk_sim.func private @unit_2.$concurrent_abort.56(
// CHECK: obelisk_sim.observer.bind
// CHECK-SAME: values(%arg1, %arg2 : !obelisk_sim.ref<!obelisk_sim.logic<1>>, !obelisk_sim.event) captures 1
// CHECK: obelisk_sim.call @unit_2.$concurrent_abort_count.56.reject
// CHECK-LABEL: obelisk_sim.func private @unit_2(
// CHECK-SAME: obelisk_sim.asynchronous_property_abort
// CHECK-SAME: obelisk_sim.persistent_until_kind = "s_until"
// CHECK: [[PREPONED_UNTIL:%.*]] = obelisk_sim.context.event %arg0[2305843009213693952]
// CHECK: obelisk_sim.spawn @unit_2.$concurrent_abort.56
// CHECK-SAME: [[PREPONED_UNTIL]]
// CHECK: obelisk_sim.suspend.edge
// CHECK: obelisk_sim.assert.sampled_read %arg0 from %arg2
// CHECK: obelisk_sim.call @unit_2.$concurrent_abort_count.56.reject

// The four-state goto DFA is aborted as four aggregate counts. An accepted
// abort is vacuous, but every successful cover-property evaluation still
// executes the pass action. The dispatcher adds the current attempt, clears
// all four states, and invokes the callback once per affected attempt.
// CHECK-LABEL: obelisk_sim.func private @unit_3.$concurrent_abort_count.76.accept(
// CHECK-SAME: obelisk_sim.concurrent_abort_counted
// CHECK: [[COVER_ZERO:%.*]] = arith.constant 0 : i64
// CHECK: [[CURRENT:%.*]] = arith.constant 1 : i64
// CHECK: [[COUNT0:%.*]] = obelisk_sim.ref.load %arg1
// CHECK: [[WITH_CURRENT:%.*]] = arith.addi [[COUNT0]], [[CURRENT]]
// CHECK: obelisk_sim.ref.store [[COVER_ZERO]] to %arg1
// CHECK: [[COUNT1:%.*]] = obelisk_sim.ref.load %arg2
// CHECK: [[SUM1:%.*]] = arith.addi [[WITH_CURRENT]], [[COUNT1]]
// CHECK: obelisk_sim.ref.store [[COVER_ZERO]] to %arg2
// CHECK: [[COUNT2:%.*]] = obelisk_sim.ref.load %arg3
// CHECK: [[SUM2:%.*]] = arith.addi [[SUM1]], [[COUNT2]]
// CHECK: obelisk_sim.ref.store [[COVER_ZERO]] to %arg3
// CHECK: [[COUNT3:%.*]] = obelisk_sim.ref.load %arg4
// CHECK: arith.addi [[SUM2]], [[COUNT3]]
// CHECK: obelisk_sim.ref.store [[COVER_ZERO]] to %arg4
// CHECK: obelisk_sim.spawn @unit_3.fork.76.0.0
// CHECK-LABEL: obelisk_sim.func private @unit_3(
// CHECK-SAME: obelisk_sim.persistent_repetition_kind = "goto"
// CHECK-SAME: obelisk_sim.persistent_repetition_states = 4 : i64
// CHECK-SAME: obelisk_sim.property_abort_action = "accept"
// CHECK-SAME: obelisk_sim.synchronous_property_abort
// CHECK: obelisk_sim.call @unit_3.$concurrent_abort_count.76.accept

// Only the two asynchronous forms need observer evaluators; both evaluators
// read the Preponed snapshot.
// CHECK-LABEL: obelisk_sim.func private @observer_
// CHECK-SAME: obelisk_sim.concurrent_abort_observer
// CHECK: obelisk_sim.assert.sampled_read %arg0 from %arg1
// CHECK-LABEL: obelisk_sim.func private @observer_
// CHECK-SAME: obelisk_sim.concurrent_abort_observer
// CHECK: obelisk_sim.assert.sampled_read %arg0 from %arg1
