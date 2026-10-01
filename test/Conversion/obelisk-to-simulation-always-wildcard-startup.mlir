// RUN: obelisk-opt %s '--lower-obelisk-to-sim=opt-level=0' | FileCheck %s

module {
  obelisk.sv.symbol.definition @s0.always_wildcard_startup attributes {definition_kind = 0 : i32, hierarchical_name = "always_wildcard_startup", name = "always_wildcard_startup", node_id = 0 : i64} {
  }
  obelisk.sv.symbol.root @s1.$root attributes {hierarchical_name = "\\$root ", name = "$root", node_id = 1 : i64} {
    obelisk.sv.symbol.compilation_unit @s2 attributes {hierarchical_name = "$unit", node_id = 2 : i64} {
    }
    obelisk.sv.symbol.instance @s3.always_wildcard_startup attributes {hierarchical_name = "always_wildcard_startup", is_uninstantiated = false, name = "always_wildcard_startup", node_id = 3 : i64, referenced_path = "always_wildcard_startup", referenced_symbol = @s0.always_wildcard_startup} {
      obelisk.sv.symbol.instance_body @s4.always_wildcard_startup attributes {hierarchical_name = "always_wildcard_startup", name = "always_wildcard_startup", node_id = 4 : i64} {
        obelisk.sv.symbol.variable @s5.source attributes {hierarchical_name = "always_wildcard_startup.source", lifetime = 1 : i32, name = "source", node_id = 5 : i64, semantic_type = !obelisk.integral<32, true, false, 31 : 0, int>} {
        }
        obelisk.sv.symbol.variable @s6.destination attributes {hierarchical_name = "always_wildcard_startup.destination", lifetime = 1 : i32, name = "destination", node_id = 6 : i64, semantic_type = !obelisk.integral<32, true, false, 31 : 0, int>} {
        }
        obelisk.sv.symbol.variable @s7.source_b attributes {hierarchical_name = "always_wildcard_startup.source_b", lifetime = 1 : i32, name = "source_b", node_id = 7 : i64, semantic_type = !obelisk.integral<32, true, false, 31 : 0, int>} {
        }
        obelisk.sv.symbol.variable @s8.destination_sum attributes {hierarchical_name = "always_wildcard_startup.destination_sum", lifetime = 1 : i32, name = "destination_sum", node_id = 8 : i64, semantic_type = !obelisk.integral<32, true, false, 31 : 0, int>} {
        }
        obelisk.sv.symbol.statement_block @s25 attributes {block_kind = 0 : i32, hierarchical_name = "always_wildcard_startup", node_id = 25 : i64} {
          obelisk.sv.symbol.variable @s26.local attributes {hierarchical_name = "always_wildcard_startup.local", name = "local", node_id = 26 : i64, semantic_type = !obelisk.integral<32, true, false, 31 : 0, int>} {
          }
        }
        obelisk.sv.symbol.procedural_block @s9 attributes {hierarchical_name = "always_wildcard_startup", node_id = 9 : i64, procedure_kind = 2 : i32, time_precision_fs = 1000000 : i64, time_unit_fs = 1000000 : i64} {
          obelisk.sv.statement.timed attributes {node_id = 10 : i64} {
            obelisk.sv.timing.implicit_event attributes {node_id = 11 : i64} {
            }
            obelisk.sv.statement.expression_statement attributes {node_id = 12 : i64} {
              obelisk.sv.expression.assignment attributes {assignment_kind = 0 : i32, node_id = 13 : i64, semantic_type = !obelisk.integral<32, true, false, 31 : 0, int>} {
                obelisk.sv.expression.named_value attributes {node_id = 14 : i64, referenced_path = "always_wildcard_startup.destination", referenced_symbol = @s1.$root::@s3.always_wildcard_startup::@s4.always_wildcard_startup::@s6.destination, semantic_type = !obelisk.integral<32, true, false, 31 : 0, int>} {
                }
                obelisk.sv.expression.named_value attributes {node_id = 15 : i64, referenced_path = "always_wildcard_startup.source", referenced_symbol = @s1.$root::@s3.always_wildcard_startup::@s4.always_wildcard_startup::@s5.source, semantic_type = !obelisk.integral<32, true, false, 31 : 0, int>} {
                }
              }
            }
          }
        }
        obelisk.sv.symbol.procedural_block @s16 attributes {hierarchical_name = "always_wildcard_startup", node_id = 16 : i64, procedure_kind = 2 : i32, time_precision_fs = 1000000 : i64, time_unit_fs = 1000000 : i64} {
          obelisk.sv.statement.timed attributes {node_id = 17 : i64} {
            obelisk.sv.timing.implicit_event attributes {node_id = 18 : i64} {
            }
            obelisk.sv.statement.expression_statement attributes {node_id = 19 : i64} {
              obelisk.sv.expression.assignment attributes {assignment_kind = 0 : i32, node_id = 20 : i64, semantic_type = !obelisk.integral<32, true, false, 31 : 0, int>} {
                obelisk.sv.expression.named_value attributes {node_id = 21 : i64, referenced_path = "always_wildcard_startup.destination_sum", referenced_symbol = @s1.$root::@s3.always_wildcard_startup::@s4.always_wildcard_startup::@s8.destination_sum, semantic_type = !obelisk.integral<32, true, false, 31 : 0, int>} {
                }
                obelisk.sv.expression.binary_op attributes {node_id = 22 : i64, operator_kind = 0 : i32, semantic_type = !obelisk.integral<32, true, false, 31 : 0, int>} {
                  obelisk.sv.expression.named_value attributes {node_id = 23 : i64, referenced_path = "always_wildcard_startup.source", referenced_symbol = @s1.$root::@s3.always_wildcard_startup::@s4.always_wildcard_startup::@s5.source, semantic_type = !obelisk.integral<32, true, false, 31 : 0, int>} {
                  }
                  obelisk.sv.expression.named_value attributes {node_id = 24 : i64, referenced_path = "always_wildcard_startup.source_b", referenced_symbol = @s1.$root::@s3.always_wildcard_startup::@s4.always_wildcard_startup::@s7.source_b, semantic_type = !obelisk.integral<32, true, false, 31 : 0, int>} {
                  }
                }
              }
            }
          }
        }
        obelisk.sv.symbol.procedural_block @s27 attributes {hierarchical_name = "always_wildcard_startup", node_id = 27 : i64, procedure_kind = 2 : i32, time_precision_fs = 1000000 : i64, time_unit_fs = 1000000 : i64} {
          obelisk.sv.statement.timed attributes {node_id = 28 : i64} {
            obelisk.sv.timing.implicit_event attributes {node_id = 29 : i64} {
            }
            obelisk.sv.statement.block attributes {node_id = 30 : i64} {
              obelisk.sv.statement.list attributes {node_id = 31 : i64} {
                obelisk.sv.statement.variable_declaration attributes {node_id = 32 : i64, referenced_path = "always_wildcard_startup.local", referenced_symbol = @s1.$root::@s3.always_wildcard_startup::@s4.always_wildcard_startup::@s25::@s26.local} {
                }
                obelisk.sv.statement.expression_statement attributes {node_id = 33 : i64} {
                  obelisk.sv.expression.assignment attributes {assignment_kind = 0 : i32, node_id = 34 : i64, semantic_type = !obelisk.integral<32, true, false, 31 : 0, int>} {
                    obelisk.sv.expression.named_value attributes {node_id = 35 : i64, referenced_path = "always_wildcard_startup.local", referenced_symbol = @s1.$root::@s3.always_wildcard_startup::@s4.always_wildcard_startup::@s25::@s26.local, semantic_type = !obelisk.integral<32, true, false, 31 : 0, int>} {
                    }
                    obelisk.sv.expression.named_value attributes {node_id = 36 : i64, referenced_path = "always_wildcard_startup.source", referenced_symbol = @s1.$root::@s3.always_wildcard_startup::@s4.always_wildcard_startup::@s5.source, semantic_type = !obelisk.integral<32, true, false, 31 : 0, int>} {
                    }
                  }
                }
                obelisk.sv.statement.expression_statement attributes {node_id = 37 : i64} {
                  obelisk.sv.expression.assignment attributes {assignment_kind = 0 : i32, node_id = 38 : i64, semantic_type = !obelisk.integral<32, true, false, 31 : 0, int>} {
                    obelisk.sv.expression.named_value attributes {node_id = 39 : i64, referenced_path = "always_wildcard_startup.destination", referenced_symbol = @s1.$root::@s3.always_wildcard_startup::@s4.always_wildcard_startup::@s6.destination, semantic_type = !obelisk.integral<32, true, false, 31 : 0, int>} {
                    }
                    obelisk.sv.expression.named_value attributes {node_id = 40 : i64, referenced_path = "always_wildcard_startup.local", referenced_symbol = @s1.$root::@s3.always_wildcard_startup::@s4.always_wildcard_startup::@s25::@s26.local, semantic_type = !obelisk.integral<32, true, false, 31 : 0, int>} {
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

// IEEE 1800-2017 9.2.2.2.2 requires always @* to wait for a change before its
// first evaluation, unlike always_comb.  The body then returns to that wait.
// CHECK-LABEL: simulation.func private @{{.*}}({{.*}}) attributes {{.*}}entry_kind = 3 : i32{{.*}}simulation.hierarchical_name = "always_wildcard_startup"
// CHECK: cf.br ^[[WAIT:bb[0-9]+]]
// CHECK: ^[[WAIT]]:
// CHECK: simulation.suspend.change {{.*}} to ^[[BODY:bb[0-9]+]]
// CHECK-SAME: schedule.top_level_wildcard_wait
// CHECK: ^[[BODY]]:
// CHECK: simulation.ref.load
// CHECK: simulation.ref.store
// CHECK: cf.br ^[[WAIT]]
// CHECK-LABEL: simulation.func private @{{.*}}({{.*}}) attributes {{.*}}entry_kind = 3 : i32{{.*}}simulation.hierarchical_name = "always_wildcard_startup"
// CHECK: cf.br ^[[MULTI_WAIT:bb[0-9]+]]
// CHECK: ^[[MULTI_WAIT]]:
// CHECK: simulation.suspend.any {{.*}} to ^[[MULTI_BODY:bb[0-9]+]]
// CHECK-SAME: schedule.top_level_wildcard_wait
// CHECK: ^[[MULTI_BODY]]:
// CHECK: simulation.ref.load
// CHECK: simulation.ref.load
// CHECK: simulation.ref.store
// CHECK: cf.br ^[[MULTI_WAIT]]
// An activation-local variable does not exist at the pre-entry wait. Keep the
// design-lifetime source in the implicit event set without capturing the local.
// CHECK-LABEL: simulation.func private @{{.*}}({{.*}}) attributes {{.*}}entry_kind = 3 : i32{{.*}}simulation.hierarchical_name = "always_wildcard_startup"
// CHECK: cf.br ^[[LOCAL_WAIT:bb[0-9]+]]
// CHECK: ^[[LOCAL_WAIT]]:
// CHECK-NOT: simulation.suspend.any
// CHECK: simulation.suspend.change {{.*}} to ^[[LOCAL_BODY:bb[0-9]+]]
// CHECK-SAME: schedule.top_level_wildcard_wait
// CHECK: ^[[LOCAL_BODY]]:
// CHECK: simulation.ref.load
// CHECK: simulation.ref.store
// CHECK: cf.br ^[[LOCAL_WAIT]]
// CHECK-NOT: obelisk.sv.
