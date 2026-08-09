// RUN: obelisk-opt %s '--lower-obelisk-to-sim=opt-level=0' | FileCheck %s

module {
  obelisk.sv.symbol.definition attributes {definition_kind = 0 : i32, hierarchical_name = "supported_fork", name = "supported_fork", node_id = 0 : i64, sym_name = "s0.supported_fork"} {
  }
  obelisk.sv.symbol.root attributes {hierarchical_name = "\\$root ", name = "$root", node_id = 1 : i64, sym_name = "s1.$root"} {
    obelisk.sv.symbol.compilation_unit attributes {hierarchical_name = "$unit", node_id = 2 : i64, sym_name = "s2"} {
    }
    obelisk.sv.symbol.instance attributes {hierarchical_name = "supported_fork", is_uninstantiated = false, name = "supported_fork", node_id = 3 : i64, referenced_path = "supported_fork", referenced_symbol = @s0.supported_fork, sym_name = "s3.supported_fork"} {
      obelisk.sv.symbol.instance_body attributes {hierarchical_name = "supported_fork", name = "supported_fork", node_id = 4 : i64, sym_name = "s4.supported_fork"} {
        obelisk.sv.symbol.statement_block attributes {block_kind = 0 : i32, hierarchical_name = "supported_fork.named_branch", name = "named_branch", node_id = 5 : i64, sym_name = "s5.named_branch"} {
        }
        obelisk.sv.symbol.procedural_block attributes {hierarchical_name = "supported_fork", node_id = 6 : i64, procedure_kind = 0 : i32, sym_name = "s6", time_precision_fs = 1000000 : i64, time_unit_fs = 1000000 : i64} {
          obelisk.sv.statement.block attributes {node_id = 7 : i64} {
            obelisk.sv.statement.list attributes {node_id = 8 : i64} {
              obelisk.sv.statement.block attributes {block_kind = 2 : i32, node_id = 9 : i64} {
                obelisk.sv.statement.list attributes {node_id = 10 : i64} {
                  obelisk.sv.statement.block attributes {node_id = 11 : i64} {
                    obelisk.sv.statement.block attributes {block_kind = 1 : i32, node_id = 28 : i64} {
                      obelisk.sv.statement.list attributes {node_id = 29 : i64} {
                        obelisk.sv.statement.block attributes {node_id = 30 : i64} {
                          obelisk.sv.statement.timed attributes {node_id = 31 : i64} {
                            obelisk.sv.timing.delay attributes {node_id = 32 : i64} {
                              obelisk.sv.expression.integer_literal attributes {constant_value = "3", node_id = 33 : i64, semantic_type = !obelisk.integral<32, true, false, 31 : 0, int>} {
                              }
                            }
                            obelisk.sv.statement.empty attributes {node_id = 34 : i64} {
                            }
                          }
                        }
                      }
                    }
                    obelisk.sv.statement.timed attributes {node_id = 12 : i64} {
                      obelisk.sv.timing.delay attributes {node_id = 13 : i64} {
                        obelisk.sv.expression.integer_literal attributes {constant_value = "1", node_id = 14 : i64, semantic_type = !obelisk.integral<32, true, false, 31 : 0, int>} {
                        }
                      }
                      obelisk.sv.statement.empty attributes {node_id = 15 : i64} {
                      }
                    }
                  }
                  obelisk.sv.statement.block attributes {node_id = 16 : i64} {
                    obelisk.sv.statement.timed attributes {node_id = 17 : i64} {
                      obelisk.sv.timing.delay attributes {node_id = 18 : i64} {
                        obelisk.sv.expression.integer_literal attributes {constant_value = "2", node_id = 19 : i64, semantic_type = !obelisk.integral<32, true, false, 31 : 0, int>} {
                        }
                      }
                      obelisk.sv.statement.empty attributes {node_id = 20 : i64} {
                      }
                    }
                  }
                }
              }
              obelisk.sv.statement.block attributes {block_kind = 1 : i32, node_id = 21 : i64} {
                obelisk.sv.statement.list attributes {node_id = 22 : i64} {
                }
              }
              obelisk.sv.statement.block attributes {block_kind = 3 : i32, node_id = 23 : i64} {
                obelisk.sv.statement.block attributes {block_path = "supported_fork.named_branch", block_symbol = @s1.$root::@s3.supported_fork::@s4.supported_fork::@s5.named_branch, node_id = 24 : i64} {
                  obelisk.sv.statement.list attributes {node_id = 25 : i64} {
                  }
                }
              }
              obelisk.sv.statement.wait_fork attributes {node_id = 26 : i64} {
              }
              obelisk.sv.statement.disable_fork attributes {node_id = 27 : i64} {
              }
            }
          }
        }
      }
    }
  }
}

// CHECK-COUNT-3: obelisk_sim.code_unit.decl {{[0-9]+}} in {{[0-9]+}} fork hierarchy "supported_fork.{{.*}}.$fork.
// CHECK: obelisk_sim.func private @{{.*}} attributes {{.*}}entry_kind = 13 : i32
// CHECK: obelisk_sim.control.enter
// CHECK: obelisk_sim.control.leave
// CHECK: obelisk_sim.spawn @
// CHECK: obelisk_sim.suspend.join any
// CHECK: obelisk_sim.suspend.children
// CHECK: obelisk_sim.children.disable
// CHECK-NOT: obelisk.sv.
