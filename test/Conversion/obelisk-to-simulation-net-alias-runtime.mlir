// RUN: obelisk-opt %s '--lower-obelisk-to-sim=opt-level=0' | FileCheck %s --check-prefix=TOPOLOGY

// Runtime behavior is checked in ../Runtime/obelisk-to-simulation-net-alias-runtime.test.

// IEEE 1800-2023 10.11: a net alias is one statically shared resolved net.
// The declared alias identity remains available to VPI, but it adds neither a
// second physical net descriptor nor runtime propagation.
module attributes {
  llvm.data_layout = "e-m:e-p:64:64-i64:64-n8:16:32:64-S128",
  llvm.target_triple = "x86_64-unknown-linux-gnu"
} {
  obelisk.sv.symbol.definition attributes {definition_kind = 0 : i32, hierarchical_name = "t", name = "t", node_id = 0 : i64, sym_name = "definition"} {
  }
  obelisk.sv.symbol.root attributes {hierarchical_name = "\\$root ", name = "$root", node_id = 1 : i64, sym_name = "root"} {
    obelisk.sv.symbol.instance attributes {hierarchical_name = "t", is_uninstantiated = false, name = "t", node_id = 2 : i64, referenced_path = "t", referenced_symbol = @definition, sym_name = "instance"} {
      obelisk.sv.symbol.instance_body attributes {hierarchical_name = "t", name = "t", node_id = 3 : i64, sym_name = "body", time_precision_fs = 1000000 : i64, time_unit_fs = 1000000 : i64} {
        obelisk.sv.symbol.net attributes {hierarchical_name = "t.source", is_implicit = false, name = "source", net_kind = 1 : i32, node_id = 4 : i64, semantic_type = !obelisk.integral<1, false, true, 0 : 0, logic>, sym_name = "source"} {
        }
        obelisk.sv.symbol.net attributes {hierarchical_name = "t.alias_name", is_implicit = false, name = "alias_name", net_kind = 1 : i32, node_id = 5 : i64, semantic_type = !obelisk.integral<1, false, true, 0 : 0, logic>, sym_name = "alias_name"} {
        }
        obelisk.sv.symbol.continuous_assign attributes {hierarchical_name = "t", node_id = 6 : i64, sym_name = "driver", time_precision_fs = 1000000 : i64, time_unit_fs = 1000000 : i64} {
          obelisk.sv.expression.assignment attributes {assignment_kind = 0 : i32, is_signed = false, node_id = 7 : i64, semantic_type = !obelisk.integral<1, false, true, 0 : 0, logic>} {
            obelisk.sv.expression.named_value attributes {is_signed = false, node_id = 8 : i64, referenced_path = "t.source", referenced_symbol = @root::@instance::@body::@source, semantic_type = !obelisk.integral<1, false, true, 0 : 0, logic>} {
            }
            obelisk.sv.expression.integer_literal attributes {constant_value = "1'b1", is_signed = false, node_id = 9 : i64, semantic_type = !obelisk.integral<1, false, true, 0 : 0, logic>} {
            }
          }
        }
        obelisk.sv.symbol.net_alias attributes {hierarchical_name = "t", node_id = 10 : i64, sym_name = "alias"} {
          obelisk.sv.expression.named_value attributes {is_signed = false, node_id = 11 : i64, referenced_path = "t.source", referenced_symbol = @root::@instance::@body::@source, semantic_type = !obelisk.integral<1, false, true, 0 : 0, logic>} {
          }
          obelisk.sv.expression.named_value attributes {is_signed = false, node_id = 12 : i64, referenced_path = "t.alias_name", referenced_symbol = @root::@instance::@body::@alias_name, semantic_type = !obelisk.integral<1, false, true, 0 : 0, logic>} {
          }
        }
        obelisk.sv.symbol.procedural_block attributes {hierarchical_name = "t", node_id = 13 : i64, procedure_kind = 0 : i32, sym_name = "initial", time_precision_fs = 1000000 : i64, time_unit_fs = 1000000 : i64} {
          obelisk.sv.statement.list attributes {node_id = 14 : i64} {
            obelisk.sv.statement.timed attributes {node_id = 15 : i64} {
              obelisk.sv.timing.delay attributes {node_id = 16 : i64} {
                obelisk.sv.expression.integer_literal attributes {constant_value = "1", is_declared_unsized = true, is_signed = true, node_id = 17 : i64, semantic_type = !obelisk.integral<32, true, false, 31 : 0, int>} {
                }
              }
              obelisk.sv.statement.empty attributes {node_id = 18 : i64} {
              }
            }
            obelisk.sv.statement.expression_statement attributes {node_id = 19 : i64} {
              obelisk.sv.expression.call attributes {argument_count = 2 : i64, callee_name = "$display", constraint_restrictions = [], defaulted_arguments = array<i64: 0, 0>, has_inline_constraints = false, has_iterator_expression = false, has_output_arguments = false, has_this_class = false, is_signed = false, is_super_class = false, is_system_call = true, node_id = 20 : i64, semantic_type = !obelisk.void, subroutine_kind = 1 : i32, system_library_cell = "work.t", system_scope_path = "t", system_scope_symbol = @root::@instance::@body} {
                obelisk.sv.expression.string_literal attributes {constant_value = "alias=%0d", is_signed = false, node_id = 21 : i64, semantic_type = !obelisk.ranged_packed_array<71 : 0 x !obelisk.integral<1, false, false, 0 : 0, bit>>} {
                }
                obelisk.sv.expression.named_value attributes {is_signed = false, node_id = 22 : i64, referenced_path = "t.alias_name", referenced_symbol = @root::@instance::@body::@alias_name, semantic_type = !obelisk.integral<1, false, true, 0 : 0, logic>} {
                }
              }
            }
          }
        }
      }
    }
  }
}

// TOPOLOGY: obelisk_sim.net.decl [[NET:[0-9]+]] {{.*}} hierarchy "t.source"
// TOPOLOGY: obelisk_sim.vpi_net_identity.decl {{[0-9]+}} backed_by [[NET]] {{.*}} hierarchy "t.alias_name"
// TOPOLOGY-NOT: obelisk_sim.net.decl {{[0-9]+}} {{.*}} hierarchy "t.alias_name"
