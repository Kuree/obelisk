// RUN: obelisk-opt %s '--lower-obelisk-to-sim=opt-level=0' > %t
// RUN: FileCheck %s < %t
// RUN: FileCheck %s --check-prefix=COUNT < %t
// RUN: obelisk-opt %s '--lower-obelisk-to-sim=opt-level=3' '--encode-obelisk-sim-to-bytecode=vpi=read' -o /dev/null

// Statement occurrences retain their original initial owner when a fork branch
// is outlined. Nested forks bind to each exact branch statement. The always
// procedure has its own body, and a dead fork has no invented code-unit binding.

module attributes {llvm.data_layout = "e-m:e-p:64:64-i64:64-n8:16:32:64-S128", llvm.target_triple = "x86_64-unknown-linux-gnu"} {
  obelisk.sv.symbol.definition @s0.supported_fork attributes {definition_kind = 0 : i32, hierarchical_name = "supported_fork", name = "supported_fork", node_id = 0 : i64} {
  }
  obelisk.sv.symbol.root @s1.$root attributes {hierarchical_name = "\\$root ", name = "$root", node_id = 1 : i64} {
    obelisk.sv.symbol.compilation_unit @s2 attributes {hierarchical_name = "$unit", node_id = 2 : i64} {
    }
    obelisk.sv.symbol.instance @s3.supported_fork attributes {hierarchical_name = "supported_fork", is_uninstantiated = false, name = "supported_fork", node_id = 3 : i64, referenced_path = "supported_fork", referenced_symbol = @s0.supported_fork} {
      obelisk.sv.symbol.instance_body @s4.supported_fork attributes {hierarchical_name = "supported_fork", name = "supported_fork", node_id = 4 : i64} {
        obelisk.sv.symbol.statement_block @s5.named_branch attributes {block_kind = 0 : i32, hierarchical_name = "supported_fork.named_branch", name = "named_branch", node_id = 5 : i64} {
        }
        obelisk.sv.symbol.procedural_block @s6 attributes {hierarchical_name = "supported_fork", node_id = 6 : i64, procedure_kind = 0 : i32, time_precision_fs = 1000000 : i64, time_unit_fs = 1000000 : i64} {
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
        obelisk.sv.symbol.procedural_block @always attributes {hierarchical_name = "supported_fork", node_id = 35 : i64, procedure_kind = 2 : i32, time_precision_fs = 1000000 : i64, time_unit_fs = 1000000 : i64} {
          obelisk.sv.statement.timed attributes {node_id = 36 : i64} {
            obelisk.sv.timing.delay attributes {node_id = 37 : i64} {
              obelisk.sv.expression.integer_literal attributes {constant_value = "1", node_id = 38 : i64, semantic_type = !obelisk.integral<32, true, false, 31 : 0, int>} {}
            }
            obelisk.sv.statement.empty attributes {node_id = 39 : i64} {}
          }
        }
        obelisk.sv.symbol.procedural_block @dead attributes {hierarchical_name = "supported_fork", node_id = 40 : i64, procedure_kind = 0 : i32, time_precision_fs = 1000000 : i64, time_unit_fs = 1000000 : i64} {
          obelisk.sv.statement.conditional attributes {check_kind = 0 : i32, condition_count = 1 : i64, condition_pattern_flags = array<i64: 0>, has_else = false, node_id = 41 : i64} {
            obelisk.sv.expression.integer_literal attributes {constant_value = "0", node_id = 42 : i64, semantic_type = !obelisk.integral<32, true, false, 31 : 0, int>} {}
            obelisk.sv.statement.block attributes {block_kind = 3 : i32, node_id = 43 : i64} {
              obelisk.sv.statement.empty attributes {node_id = 44 : i64} {}
            }
          }
        }
      }
    }
  }
}

// CHECK: simulation.code_unit.decl [[INITIAL:[0-9]+]] in [[SCOPE:[0-9]+]] initial hierarchy "supported_fork.$code_unit_6"
// CHECK: simulation.code_unit.decl [[ALWAYS:[0-9]+]] in [[SCOPE]] always
// CHECK: simulation.code_unit.decl [[DEAD:[0-9]+]] in [[SCOPE]] initial hierarchy "supported_fork.$code_unit_40"
// CHECK: simulation.statement.decl [[BODY:[0-9]+]] in [[INITIAL]] scope [[SCOPE]] type 4
// CHECK: simulation.statement_site.decl {{[0-9]+}} on [[BODY]] phase 0
// CHECK: simulation.vpi_statement_relation.decl code_unit [[INITIAL]] type 24 selector 104 ordinal 0 modes 1 to [[BODY]]
// CHECK: simulation.vpi_thread_origin_binding [[INITIAL]] origin [[BODY]]
// CHECK: simulation.statement.decl [[FORK:[0-9]+]] in [[INITIAL]] scope [[SCOPE]] type 18 parent [[BODY]]
// CHECK: simulation.statement.decl [[LEFT:[0-9]+]] in [[INITIAL]] scope [[SCOPE]] type 4 parent [[FORK]]
// CHECK: simulation.vpi_statement_relation.decl statement [[FORK]] type 18 selector 104 ordinal 0 modes 2 to [[LEFT]]
// CHECK: simulation.statement.decl [[NESTED:[0-9]+]] in [[INITIAL]] scope [[SCOPE]] type 18 parent [[LEFT]]
// CHECK: simulation.statement.decl [[INNER:[0-9]+]] in [[INITIAL]] scope [[SCOPE]] type 4 parent [[NESTED]]
// CHECK: simulation.statement.decl [[RIGHT:[0-9]+]] in [[INITIAL]] scope [[SCOPE]] type 4 parent [[FORK]]
// CHECK: simulation.vpi_statement_relation.decl statement [[FORK]] type 18 selector 104 ordinal 1 modes 2 to [[RIGHT]]
// CHECK: simulation.statement.decl [[NAMED:[0-9]+]] in [[INITIAL]] scope [[SCOPE]] type 33 parent {{[0-9]+}} name "named_branch" {is_scope}
// CHECK: simulation.statement.decl [[DELAY:[0-9]+]] in [[ALWAYS]] scope [[SCOPE]] type 11
// CHECK: simulation.vpi_statement_relation.decl code_unit [[ALWAYS]] type 1 selector 104 ordinal 0 modes 1 to [[DELAY]]
// CHECK: simulation.vpi_thread_origin_binding [[ALWAYS]] origin [[DELAY]]
// CHECK: simulation.statement.decl [[IF:[0-9]+]] in [[DEAD]] scope [[SCOPE]] type 22
// CHECK: simulation.vpi_thread_origin_binding [[DEAD]] origin [[IF]]
// CHECK: simulation.statement.decl [[DEADFORK:[0-9]+]] in [[DEAD]] scope [[SCOPE]] type 18 parent [[IF]]
// CHECK: simulation.statement.decl {{[0-9]+}} in [[DEAD]] scope [[SCOPE]] type 38 parent [[DEADFORK]]
// CHECK: simulation.code_unit.decl [[LEFTUNIT:[0-9]+]] in [[SCOPE]] fork {{.*}} {internal}
// CHECK: simulation.code_unit.decl [[INNERUNIT:[0-9]+]] in [[SCOPE]] fork {{.*}} {internal}
// CHECK: simulation.vpi_thread_origin_binding [[INNERUNIT]] origin [[INNER]]
// CHECK: simulation.vpi_thread_origin_binding [[LEFTUNIT]] origin [[LEFT]]
// CHECK: simulation.code_unit.decl [[RIGHTUNIT:[0-9]+]] in [[SCOPE]] fork {{.*}} {internal}
// CHECK: simulation.vpi_thread_origin_binding [[RIGHTUNIT]] origin [[RIGHT]]
// CHECK: simulation.code_unit.decl [[NAMEDUNIT:[0-9]+]] in [[SCOPE]] fork {{.*}} {internal}
// CHECK: simulation.vpi_thread_origin_binding [[NAMEDUNIT]] origin [[NAMED]]

// Three procedure origins and four actual outlined branch origins. The dead
// branch remains lexical inventory only and creates no executable binding.
// COUNT-COUNT-7: simulation.vpi_thread_origin_binding
// COUNT-NOT: simulation.vpi_thread_origin_binding
