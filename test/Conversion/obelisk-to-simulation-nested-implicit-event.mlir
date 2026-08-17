// RUN: obelisk-opt %s '--lower-obelisk-to-sim=opt-level=0' | FileCheck %s

// IEEE 1800-2017 9.4.2.2: the implicit event list of `@*` covers every net and
// variable read by the statement it controls, including the reads of nested
// statements that follow a nested event control. The outer `@*` here must
// therefore wait on both `b` and `c`, while the nested `@*` waits on `c` alone.

module {
  obelisk.sv.symbol.definition attributes {definition_kind = 0 : i32, hierarchical_name = "nested_implicit_event", name = "nested_implicit_event", node_id = 0 : i64, sym_name = "s0.nested_implicit_event"} {
  }
  obelisk.sv.symbol.root attributes {hierarchical_name = "\\$root ", name = "$root", node_id = 1 : i64, sym_name = "s1.$root"} {
    obelisk.sv.symbol.compilation_unit attributes {hierarchical_name = "$unit", node_id = 2 : i64, sym_name = "s2"} {
    }
    obelisk.sv.symbol.instance attributes {hierarchical_name = "nested_implicit_event", is_uninstantiated = false, name = "nested_implicit_event", node_id = 3 : i64, referenced_path = "nested_implicit_event", referenced_symbol = @s0.nested_implicit_event, sym_name = "s3.nested_implicit_event"} {
      obelisk.sv.symbol.instance_body attributes {hierarchical_name = "nested_implicit_event", name = "nested_implicit_event", node_id = 4 : i64, sym_name = "s4.nested_implicit_event", time_precision_fs = 1000000 : i64, time_unit_fs = 1000000 : i64} {
        obelisk.sv.symbol.variable attributes {hierarchical_name = "nested_implicit_event.a", lifetime = 1 : i32, name = "a", node_id = 5 : i64, semantic_type = !obelisk.integral<1, false, true, 0 : 0, logic>, sym_name = "s5.a"} {
        }
        obelisk.sv.symbol.variable attributes {hierarchical_name = "nested_implicit_event.b", lifetime = 1 : i32, name = "b", node_id = 6 : i64, semantic_type = !obelisk.integral<1, false, true, 0 : 0, logic>, sym_name = "s6.b"} {
        }
        obelisk.sv.symbol.variable attributes {hierarchical_name = "nested_implicit_event.c", lifetime = 1 : i32, name = "c", node_id = 7 : i64, semantic_type = !obelisk.integral<1, false, true, 0 : 0, logic>, sym_name = "s7.c"} {
        }
        obelisk.sv.symbol.procedural_block attributes {hierarchical_name = "nested_implicit_event", node_id = 8 : i64, procedure_kind = 2 : i32, sym_name = "s8", time_precision_fs = 1000000 : i64, time_unit_fs = 1000000 : i64} {
          obelisk.sv.statement.timed attributes {node_id = 9 : i64} {
            obelisk.sv.timing.implicit_event attributes {node_id = 10 : i64} {
            }
            obelisk.sv.statement.block attributes {node_id = 11 : i64} {
              obelisk.sv.statement.list attributes {node_id = 12 : i64} {
                obelisk.sv.statement.expression_statement attributes {node_id = 13 : i64} {
                  obelisk.sv.expression.assignment attributes {assignment_kind = 0 : i32, is_signed = false, node_id = 14 : i64, semantic_type = !obelisk.integral<1, false, true, 0 : 0, logic>} {
                    obelisk.sv.expression.named_value attributes {is_signed = false, node_id = 15 : i64, referenced_path = "nested_implicit_event.a", referenced_symbol = @s1.$root::@s3.nested_implicit_event::@s4.nested_implicit_event::@s5.a, semantic_type = !obelisk.integral<1, false, true, 0 : 0, logic>} {
                    }
                    obelisk.sv.expression.named_value attributes {is_signed = false, node_id = 16 : i64, referenced_path = "nested_implicit_event.b", referenced_symbol = @s1.$root::@s3.nested_implicit_event::@s4.nested_implicit_event::@s6.b, semantic_type = !obelisk.integral<1, false, true, 0 : 0, logic>} {
                    }
                  }
                }
                obelisk.sv.statement.timed attributes {node_id = 17 : i64} {
                  obelisk.sv.timing.implicit_event attributes {node_id = 18 : i64} {
                  }
                  obelisk.sv.statement.expression_statement attributes {node_id = 19 : i64} {
                    obelisk.sv.expression.assignment attributes {assignment_kind = 0 : i32, is_signed = false, node_id = 20 : i64, semantic_type = !obelisk.integral<1, false, true, 0 : 0, logic>} {
                      obelisk.sv.expression.named_value attributes {is_signed = false, node_id = 21 : i64, referenced_path = "nested_implicit_event.a", referenced_symbol = @s1.$root::@s3.nested_implicit_event::@s4.nested_implicit_event::@s5.a, semantic_type = !obelisk.integral<1, false, true, 0 : 0, logic>} {
                      }
                      obelisk.sv.expression.named_value attributes {is_signed = false, node_id = 22 : i64, referenced_path = "nested_implicit_event.c", referenced_symbol = @s1.$root::@s3.nested_implicit_event::@s4.nested_implicit_event::@s7.c, semantic_type = !obelisk.integral<1, false, true, 0 : 0, logic>} {
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
}

// CHECK: obelisk_sim.func private @unit_0(
// CHECK: obelisk_sim.suspend.any %[[B:.*]], %[[C:.*]] edges
// CHECK: obelisk_sim.ref.load %[[B]]
// CHECK: obelisk_sim.suspend.change %[[C]]
