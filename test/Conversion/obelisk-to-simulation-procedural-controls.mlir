// RUN: obelisk-opt %s '--lower-obelisk-to-sim=opt-level=0' | FileCheck %s

module {
  obelisk.sv.symbol.definition attributes {definition_kind = 0 : i32, hierarchical_name = "simulation_procedural_controls", name = "simulation_procedural_controls", node_id = 0 : i64, sym_name = "s0.simulation_procedural_controls"} {
  }
  obelisk.sv.symbol.root attributes {hierarchical_name = "\\$root ", name = "$root", node_id = 1 : i64, sym_name = "s1.$root"} {
    obelisk.sv.symbol.compilation_unit attributes {hierarchical_name = "$unit", node_id = 2 : i64, sym_name = "s2"} {
    }
    obelisk.sv.symbol.instance attributes {hierarchical_name = "simulation_procedural_controls", is_uninstantiated = false, name = "simulation_procedural_controls", node_id = 3 : i64, referenced_path = "simulation_procedural_controls", referenced_symbol = @s0.simulation_procedural_controls, sym_name = "s3.simulation_procedural_controls"} {
      obelisk.sv.symbol.instance_body attributes {hierarchical_name = "simulation_procedural_controls", name = "simulation_procedural_controls", node_id = 4 : i64, sym_name = "s4.simulation_procedural_controls"} {
        obelisk.sv.symbol.variable attributes {hierarchical_name = "simulation_procedural_controls.clk", lifetime = 1 : i32, name = "clk", node_id = 5 : i64, semantic_type = !obelisk.integral<1, false, true, 0 : 0, logic>, sym_name = "s5.clk"} {
        }
        obelisk.sv.symbol.variable attributes {hierarchical_name = "simulation_procedural_controls.enable", lifetime = 1 : i32, name = "enable", node_id = 6 : i64, semantic_type = !obelisk.integral<1, false, true, 0 : 0, logic>, sym_name = "s6.enable"} {
        }
        obelisk.sv.symbol.variable attributes {hierarchical_name = "simulation_procedural_controls.lhs", lifetime = 1 : i32, name = "lhs", node_id = 7 : i64, semantic_type = !obelisk.integral<1, false, true, 0 : 0, logic>, sym_name = "s7.lhs"} {
        }
        obelisk.sv.symbol.variable attributes {hierarchical_name = "simulation_procedural_controls.rhs", lifetime = 1 : i32, name = "rhs", node_id = 8 : i64, semantic_type = !obelisk.integral<1, false, true, 0 : 0, logic>, sym_name = "s8.rhs"} {
        }
        obelisk.sv.symbol.variable attributes {hierarchical_name = "simulation_procedural_controls.count", lifetime = 1 : i32, name = "count", node_id = 9 : i64, semantic_type = !obelisk.integral<32, true, false, 31 : 0, int>, sym_name = "s9.count"} {
        }
        obelisk.sv.symbol.procedural_block attributes {hierarchical_name = "simulation_procedural_controls", node_id = 10 : i64, procedure_kind = 0 : i32, sym_name = "s10", time_precision_fs = 1000 : i64, time_unit_fs = 1000000 : i64} {
          obelisk.sv.statement.block attributes {node_id = 11 : i64} {
            obelisk.sv.statement.list attributes {node_id = 12 : i64} {
              obelisk.sv.statement.timed attributes {node_id = 13 : i64} {
                obelisk.sv.timing.one_step_delay attributes {node_id = 14 : i64} {
                }
                obelisk.sv.statement.empty attributes {node_id = 15 : i64} {
                }
              }
              obelisk.sv.statement.wait attributes {node_id = 16 : i64} {
                obelisk.sv.expression.named_value attributes {node_id = 17 : i64, referenced_path = "simulation_procedural_controls.enable", referenced_symbol = @s1.$root::@s3.simulation_procedural_controls::@s4.simulation_procedural_controls::@s6.enable, semantic_type = !obelisk.integral<1, false, true, 0 : 0, logic>} {
                }
                obelisk.sv.statement.expression_statement attributes {node_id = 18 : i64} {
                  obelisk.sv.expression.assignment attributes {assignment_kind = 0 : i32, node_id = 19 : i64, semantic_type = !obelisk.integral<1, false, true, 0 : 0, logic>} {
                    obelisk.sv.expression.named_value attributes {node_id = 20 : i64, referenced_path = "simulation_procedural_controls.lhs", referenced_symbol = @s1.$root::@s3.simulation_procedural_controls::@s4.simulation_procedural_controls::@s7.lhs, semantic_type = !obelisk.integral<1, false, true, 0 : 0, logic>} {
                    }
                    obelisk.sv.expression.named_value attributes {node_id = 21 : i64, referenced_path = "simulation_procedural_controls.rhs", referenced_symbol = @s1.$root::@s3.simulation_procedural_controls::@s4.simulation_procedural_controls::@s8.rhs, semantic_type = !obelisk.integral<1, false, true, 0 : 0, logic>} {
                    }
                  }
                }
              }
              obelisk.sv.statement.repeat_loop attributes {node_id = 22 : i64} {
                obelisk.sv.expression.named_value attributes {node_id = 23 : i64, referenced_path = "simulation_procedural_controls.count", referenced_symbol = @s1.$root::@s3.simulation_procedural_controls::@s4.simulation_procedural_controls::@s9.count, semantic_type = !obelisk.integral<32, true, false, 31 : 0, int>} {
                }
                obelisk.sv.statement.timed attributes {node_id = 24 : i64} {
                  obelisk.sv.timing.signal_event attributes {edge_kind = 1 : i32, has_iff = false, node_id = 25 : i64} {
                    obelisk.sv.expression.named_value attributes {node_id = 26 : i64, referenced_path = "simulation_procedural_controls.clk", referenced_symbol = @s1.$root::@s3.simulation_procedural_controls::@s4.simulation_procedural_controls::@s5.clk, semantic_type = !obelisk.integral<1, false, true, 0 : 0, logic>} {
                    }
                  }
                  obelisk.sv.statement.expression_statement attributes {node_id = 27 : i64} {
                    obelisk.sv.expression.assignment attributes {assignment_kind = 0 : i32, node_id = 28 : i64, semantic_type = !obelisk.integral<1, false, true, 0 : 0, logic>} {
                      obelisk.sv.expression.named_value attributes {node_id = 29 : i64, referenced_path = "simulation_procedural_controls.lhs", referenced_symbol = @s1.$root::@s3.simulation_procedural_controls::@s4.simulation_procedural_controls::@s7.lhs, semantic_type = !obelisk.integral<1, false, true, 0 : 0, logic>} {
                      }
                      obelisk.sv.expression.named_value attributes {node_id = 30 : i64, referenced_path = "simulation_procedural_controls.rhs", referenced_symbol = @s1.$root::@s3.simulation_procedural_controls::@s4.simulation_procedural_controls::@s8.rhs, semantic_type = !obelisk.integral<1, false, true, 0 : 0, logic>} {
                      }
                    }
                  }
                }
              }
              obelisk.sv.statement.repeat_loop attributes {node_id = 31 : i64} {
                obelisk.sv.expression.integer_literal attributes {constant_value = "3", node_id = 32 : i64, semantic_type = !obelisk.integral<32, true, false, 31 : 0, int>} {
                }
                obelisk.sv.statement.block attributes {node_id = 33 : i64} {
                  obelisk.sv.statement.list attributes {node_id = 34 : i64} {
                    obelisk.sv.statement.expression_statement attributes {node_id = 35 : i64} {
                      obelisk.sv.expression.unary_op attributes {node_id = 36 : i64, operator_kind = 12 : i32, semantic_type = !obelisk.integral<32, true, false, 31 : 0, int>} {
                        obelisk.sv.expression.named_value attributes {node_id = 37 : i64, referenced_path = "simulation_procedural_controls.count", referenced_symbol = @s1.$root::@s3.simulation_procedural_controls::@s4.simulation_procedural_controls::@s9.count, semantic_type = !obelisk.integral<32, true, false, 31 : 0, int>} {
                        }
                      }
                    }
                    obelisk.sv.statement.continue attributes {node_id = 38 : i64} {
                    }
                    obelisk.sv.statement.expression_statement attributes {node_id = 39 : i64} {
                      obelisk.sv.expression.assignment attributes {assignment_kind = 0 : i32, node_id = 40 : i64, semantic_type = !obelisk.integral<32, true, false, 31 : 0, int>} {
                        obelisk.sv.expression.named_value attributes {node_id = 41 : i64, referenced_path = "simulation_procedural_controls.count", referenced_symbol = @s1.$root::@s3.simulation_procedural_controls::@s4.simulation_procedural_controls::@s9.count, semantic_type = !obelisk.integral<32, true, false, 31 : 0, int>} {
                        }
                        obelisk.sv.expression.binary_op attributes {node_id = 42 : i64, operator_kind = 0 : i32, semantic_type = !obelisk.integral<32, true, false, 31 : 0, int>} {
                          obelisk.sv.expression.named_value attributes {node_id = 43 : i64, referenced_path = "simulation_procedural_controls.count", referenced_symbol = @s1.$root::@s3.simulation_procedural_controls::@s4.simulation_procedural_controls::@s9.count, semantic_type = !obelisk.integral<32, true, false, 31 : 0, int>} {
                          }
                          obelisk.sv.expression.integer_literal attributes {constant_value = "100", node_id = 44 : i64, semantic_type = !obelisk.integral<32, true, false, 31 : 0, int>} {
                          }
                        }
                      }
                    }
                  }
                }
              }
              obelisk.sv.statement.timed attributes {node_id = 45 : i64} {
                obelisk.sv.timing.signal_event attributes {edge_kind = 1 : i32, has_iff = true, node_id = 46 : i64} {
                  obelisk.sv.expression.named_value attributes {node_id = 47 : i64, referenced_path = "simulation_procedural_controls.clk", referenced_symbol = @s1.$root::@s3.simulation_procedural_controls::@s4.simulation_procedural_controls::@s5.clk, semantic_type = !obelisk.integral<1, false, true, 0 : 0, logic>} {
                  }
                  obelisk.sv.expression.named_value attributes {node_id = 48 : i64, referenced_path = "simulation_procedural_controls.enable", referenced_symbol = @s1.$root::@s3.simulation_procedural_controls::@s4.simulation_procedural_controls::@s6.enable, semantic_type = !obelisk.integral<1, false, true, 0 : 0, logic>} {
                  }
                }
                obelisk.sv.statement.expression_statement attributes {node_id = 49 : i64} {
                  obelisk.sv.expression.assignment attributes {assignment_kind = 0 : i32, node_id = 50 : i64, semantic_type = !obelisk.integral<1, false, true, 0 : 0, logic>} {
                    obelisk.sv.expression.named_value attributes {node_id = 51 : i64, referenced_path = "simulation_procedural_controls.lhs", referenced_symbol = @s1.$root::@s3.simulation_procedural_controls::@s4.simulation_procedural_controls::@s7.lhs, semantic_type = !obelisk.integral<1, false, true, 0 : 0, logic>} {
                    }
                    obelisk.sv.expression.named_value attributes {node_id = 52 : i64, referenced_path = "simulation_procedural_controls.rhs", referenced_symbol = @s1.$root::@s3.simulation_procedural_controls::@s4.simulation_procedural_controls::@s8.rhs, semantic_type = !obelisk.integral<1, false, true, 0 : 0, logic>} {
                    }
                  }
                }
              }
              obelisk.sv.statement.timed attributes {node_id = 53 : i64} {
                obelisk.sv.timing.implicit_event attributes {node_id = 54 : i64} {
                }
                obelisk.sv.statement.expression_statement attributes {node_id = 55 : i64} {
                  obelisk.sv.expression.assignment attributes {assignment_kind = 0 : i32, node_id = 56 : i64, semantic_type = !obelisk.integral<1, false, true, 0 : 0, logic>} {
                    obelisk.sv.expression.named_value attributes {node_id = 57 : i64, referenced_path = "simulation_procedural_controls.lhs", referenced_symbol = @s1.$root::@s3.simulation_procedural_controls::@s4.simulation_procedural_controls::@s7.lhs, semantic_type = !obelisk.integral<1, false, true, 0 : 0, logic>} {
                    }
                    obelisk.sv.expression.named_value attributes {node_id = 58 : i64, referenced_path = "simulation_procedural_controls.rhs", referenced_symbol = @s1.$root::@s3.simulation_procedural_controls::@s4.simulation_procedural_controls::@s8.rhs, semantic_type = !obelisk.integral<1, false, true, 0 : 0, logic>} {
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

// #1step is one design-precision tick.
// CHECK: obelisk_sim.time.constant 1
// CHECK: obelisk_sim.suspend.delay

// `wait` first tests its condition and uses a level-sensitive suspension when
// the initial test is false.
// CHECK: obelisk_sim.logic.is_true
// CHECK: cf.cond_br
// CHECK: obelisk_sim.suspend.level

// A dynamic repeated event carries its encounter-time count across resumes.
// CHECK: arith.cmpi sgt
// CHECK: obelisk_sim.suspend.edge posedge
// CHECK: arith.subi
// A repeat-loop continue verifies with the current count passed to the
// decrement block.
// CHECK: cf.br ^{{.*}}(%{{.*}} : i64)

// `iff` is sampled atomically with the primary event occurrence.
// CHECK: obelisk_sim.suspend.edge_iff posedge

// @* derives sensitivity from reads in its controlled statement.
// CHECK: obelisk_sim.suspend.change
// CHECK-NOT: obelisk.sv.
