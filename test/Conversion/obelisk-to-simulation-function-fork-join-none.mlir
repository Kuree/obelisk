// RUN: obelisk-opt %s '--lower-obelisk-to-sim=opt-level=0' | FileCheck %s

module {
  obelisk.sv.symbol.definition attributes {definition_kind = 0 : i32, hierarchical_name = "top", name = "top", node_id = 0 : i64, sym_name = "s0.top"} {
  }
  obelisk.sv.symbol.root attributes {hierarchical_name = "\\$root ", name = "$root", node_id = 1 : i64, sym_name = "s1.$root"} {
    obelisk.sv.symbol.compilation_unit attributes {hierarchical_name = "$unit", node_id = 2 : i64, sym_name = "s2"} {
    }
    obelisk.sv.symbol.instance attributes {hierarchical_name = "top", is_uninstantiated = false, name = "top", node_id = 3 : i64, referenced_path = "top", referenced_symbol = @s0.top, sym_name = "s3.top"} {
      obelisk.sv.symbol.instance_body attributes {hierarchical_name = "top", name = "top", node_id = 4 : i64, sym_name = "s4.top", time_precision_fs = 1000000 : i64, time_unit_fs = 1000000 : i64} {
        obelisk.sv.symbol.subroutine attributes {default_lifetime = 0 : i32, hierarchical_name = "top.spawn", name = "spawn", node_id = 5 : i64, semantic_type = !obelisk.subroutine<() -> !obelisk.void, false>, subroutine_kind = 0 : i32, sym_name = "s5.spawn", time_precision_fs = 1000000 : i64, time_unit_fs = 1000000 : i64} {
          obelisk.sv.statement.block attributes {block_kind = 3 : i32, node_id = 6 : i64} {
            obelisk.sv.statement.block attributes {node_id = 7 : i64} {
              obelisk.sv.statement.list attributes {node_id = 8 : i64} {
                obelisk.sv.statement.variable_declaration attributes {node_id = 9 : i64, referenced_path = "top.spawn.local", referenced_symbol = @s1.$root::@s3.top::@s4.top::@s5.spawn::@s6::@s7.local} {
                  obelisk.sv.expression.integer_literal attributes {constant_value = "7", is_declared_unsized = true, is_signed = true, node_id = 10 : i64, semantic_type = !obelisk.integral<32, true, false, 31 : 0, int>} {
                  }
                }
                obelisk.sv.statement.timed attributes {node_id = 11 : i64} {
                  obelisk.sv.timing.delay attributes {node_id = 12 : i64} {
                    obelisk.sv.expression.integer_literal attributes {constant_value = "1", is_declared_unsized = true, is_signed = true, node_id = 13 : i64, semantic_type = !obelisk.integral<32, true, false, 31 : 0, int>} {
                    }
                  }
                  obelisk.sv.statement.empty attributes {node_id = 14 : i64} {
                  }
                }
              }
            }
          }
          obelisk.sv.symbol.statement_block attributes {block_kind = 0 : i32, hierarchical_name = "top.spawn", node_id = 15 : i64, sym_name = "s6"} {
            obelisk.sv.symbol.variable attributes {hierarchical_name = "top.spawn.local", lifetime = 0 : i32, name = "local", node_id = 16 : i64, semantic_type = !obelisk.integral<32, true, false, 31 : 0, int>, sym_name = "s7.local"} {
            }
          }
        }
        obelisk.sv.symbol.procedural_block attributes {hierarchical_name = "top", node_id = 17 : i64, procedure_kind = 0 : i32, sym_name = "s8", time_precision_fs = 1000000 : i64, time_unit_fs = 1000000 : i64} {
          obelisk.sv.statement.expression_statement attributes {node_id = 18 : i64} {
            obelisk.sv.expression.call attributes {argument_count = 0 : i64, callee_name = "spawn", constraint_restrictions = [], defaulted_arguments = array<i64>, has_inline_constraints = false, has_iterator_expression = false, has_output_arguments = false, has_this_class = false, is_signed = false, is_super_class = false, is_system_call = false, node_id = 19 : i64, referenced_path = "top.spawn", referenced_symbol = @s1.$root::@s3.top::@s4.top::@s5.spawn, semantic_type = !obelisk.void, subroutine_kind = 0 : i32} {
            }
          }
        }
      }
    }
  }
}

// The function remains a zero-time function; only its detached branch owns
// the suspension.
// CHECK: obelisk_sim.code_unit.decl {{[0-9]+}} in {{[0-9]+}} function hierarchy "top.spawn"
// CHECK: obelisk_sim.code_unit.decl {{[0-9]+}} in {{[0-9]+}} fork hierarchy "top.spawn.$fork.6.0"
// CHECK-LABEL: obelisk_sim.func private @unit_0.fork.6.0.0(%{{.*}}: !obelisk_sim.context {obelisk_sim.capture_kind = 0 : i32}) attributes
// CHECK-NOT: obelisk_sim.static.once
// CHECK: obelisk_sim.suspend.delay
// CHECK: obelisk_sim.return
// CHECK-LABEL: obelisk_sim.func private @unit_0(
// CHECK: obelisk_sim.spawn @unit_0.fork.6.0.0
// CHECK-NOT: obelisk_sim.suspend.delay
// CHECK: obelisk_sim.return
// CHECK-NOT: obelisk.sv.
