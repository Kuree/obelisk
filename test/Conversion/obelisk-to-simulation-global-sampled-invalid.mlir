// RUN: %split-file %s %t
// RUN: not obelisk-opt %t/missing-clock.mlir '--lower-obelisk-to-sim=opt-level=0' 2>&1 | FileCheck %s --check-prefix=MISSING
// RUN: not obelisk-opt %t/future-procedural.mlir '--lower-obelisk-to-sim=opt-level=0' 2>&1 | FileCheck %s --check-prefix=PROCEDURAL

// MISSING: error: global sampled values currently require one direct global clock signal without iff
// PROCEDURAL: error: $future_gclk requires the detached global-future assertion resolver

//--- missing-clock.mlir

module {
  obelisk.sv.symbol.definition attributes {definition_kind = 0 : i32, hierarchical_name = "bad", name = "bad", node_id = 0 : i64, sym_name = "s0.bad"} {
  }
  obelisk.sv.symbol.root attributes {hierarchical_name = "\\$root ", name = "$root", node_id = 1 : i64, sym_name = "s1.$root"} {
    obelisk.sv.symbol.compilation_unit attributes {hierarchical_name = "$unit", node_id = 2 : i64, sym_name = "s2"} {
    }
    obelisk.sv.symbol.instance attributes {hierarchical_name = "bad", is_uninstantiated = false, name = "bad", node_id = 3 : i64, referenced_path = "bad", referenced_symbol = @s0.bad, sym_name = "s3.bad"} {
      obelisk.sv.symbol.instance_body attributes {hierarchical_name = "bad", name = "bad", node_id = 4 : i64, sym_name = "s4.bad", time_precision_fs = 1000000 : i64, time_unit_fs = 1000000 : i64} {
        obelisk.sv.symbol.variable attributes {hierarchical_name = "bad.a", lifetime = 1 : i32, name = "a", node_id = 5 : i64, semantic_type = !obelisk.integral<1, false, true, 0 : 0, logic>, sym_name = "s5.a"} {
        }
        obelisk.sv.symbol.variable attributes {hierarchical_name = "bad.b", lifetime = 1 : i32, name = "b", node_id = 6 : i64, semantic_type = !obelisk.integral<1, false, true, 0 : 0, logic>, sym_name = "s6.b"} {
        }
        obelisk.sv.symbol.procedural_block attributes {hierarchical_name = "bad", node_id = 7 : i64, procedure_kind = 0 : i32, sym_name = "s7", time_precision_fs = 1000000 : i64, time_unit_fs = 1000000 : i64} {
          obelisk.sv.statement.expression_statement attributes {node_id = 8 : i64} {
            obelisk.sv.expression.assignment attributes {assignment_kind = 0 : i32, is_signed = false, node_id = 9 : i64, semantic_type = !obelisk.integral<1, false, true, 0 : 0, logic>} {
              obelisk.sv.expression.named_value attributes {is_signed = false, node_id = 10 : i64, referenced_path = "bad.b", referenced_symbol = @s1.$root::@s3.bad::@s4.bad::@s6.b, semantic_type = !obelisk.integral<1, false, true, 0 : 0, logic>} {
              }
              obelisk.sv.expression.call attributes {argument_count = 1 : i64, callee_name = "$past_gclk", constraint_restrictions = [], defaulted_arguments = array<i64: 0>, has_inline_constraints = false, has_iterator_expression = false, has_output_arguments = false, has_this_class = false, is_signed = false, is_super_class = false, is_system_call = true, node_id = 11 : i64, semantic_type = !obelisk.integral<1, false, true, 0 : 0, logic>, subroutine_kind = 0 : i32, system_library_cell = "work.bad", system_scope_path = "bad", system_scope_symbol = @s1.$root::@s3.bad::@s4.bad} {
                obelisk.sv.expression.named_value attributes {is_signed = false, node_id = 12 : i64, referenced_path = "bad.a", referenced_symbol = @s1.$root::@s3.bad::@s4.bad::@s5.a, semantic_type = !obelisk.integral<1, false, true, 0 : 0, logic>} {
                }
              }
            }
          }
        }
      }
    }
  }
}

//--- future-procedural.mlir

module {
  obelisk.sv.symbol.definition attributes {definition_kind = 0 : i32, hierarchical_name = "bad", name = "bad", node_id = 0 : i64, sym_name = "s0.bad"} {
  }
  obelisk.sv.symbol.root attributes {hierarchical_name = "\\$root ", name = "$root", node_id = 1 : i64, sym_name = "s1.$root"} {
    obelisk.sv.symbol.compilation_unit attributes {hierarchical_name = "$unit", node_id = 2 : i64, sym_name = "s2"} {
    }
    obelisk.sv.symbol.instance attributes {hierarchical_name = "bad", is_uninstantiated = false, name = "bad", node_id = 3 : i64, referenced_path = "bad", referenced_symbol = @s0.bad, sym_name = "s3.bad"} {
      obelisk.sv.symbol.instance_body attributes {hierarchical_name = "bad", name = "bad", node_id = 4 : i64, sym_name = "s4.bad", time_precision_fs = 1000000 : i64, time_unit_fs = 1000000 : i64} {
        obelisk.sv.symbol.variable attributes {hierarchical_name = "bad.gclk", lifetime = 1 : i32, name = "gclk", node_id = 5 : i64, semantic_type = !obelisk.integral<1, false, true, 0 : 0, logic>, sym_name = "s5.gclk"} {
        }
        obelisk.sv.symbol.variable attributes {hierarchical_name = "bad.a", lifetime = 1 : i32, name = "a", node_id = 6 : i64, semantic_type = !obelisk.integral<1, false, true, 0 : 0, logic>, sym_name = "s6.a"} {
        }
        obelisk.sv.symbol.variable attributes {hierarchical_name = "bad.b", lifetime = 1 : i32, name = "b", node_id = 7 : i64, semantic_type = !obelisk.integral<1, false, true, 0 : 0, logic>, sym_name = "s7.b"} {
        }
        obelisk.sv.symbol.procedural_block attributes {hierarchical_name = "bad", node_id = 8 : i64, procedure_kind = 0 : i32, sym_name = "s8", time_precision_fs = 1000000 : i64, time_unit_fs = 1000000 : i64} {
          obelisk.sv.statement.expression_statement attributes {node_id = 9 : i64} {
            obelisk.sv.expression.assignment attributes {assignment_kind = 0 : i32, is_signed = false, node_id = 10 : i64, semantic_type = !obelisk.integral<1, false, true, 0 : 0, logic>} {
              obelisk.sv.expression.named_value attributes {is_signed = false, node_id = 11 : i64, referenced_path = "bad.b", referenced_symbol = @s1.$root::@s3.bad::@s4.bad::@s7.b, semantic_type = !obelisk.integral<1, false, true, 0 : 0, logic>} {
              }
              obelisk.sv.expression.call attributes {argument_count = 1 : i64, callee_name = "$future_gclk", clocking_block_event, clocking_event_edge = 1 : i32, clocking_event_path = "bad.gclk", clocking_event_symbol = @s1.$root::@s3.bad::@s4.bad::@s5.gclk, constraint_restrictions = [], defaulted_arguments = array<i64: 0>, has_inline_constraints = false, has_iterator_expression = false, has_output_arguments = false, has_this_class = false, is_signed = false, is_super_class = false, is_system_call = true, node_id = 12 : i64, semantic_type = !obelisk.integral<1, false, true, 0 : 0, logic>, subroutine_kind = 0 : i32, system_library_cell = "work.bad", system_scope_path = "bad", system_scope_symbol = @s1.$root::@s3.bad::@s4.bad} {
                obelisk.sv.expression.named_value attributes {is_signed = false, node_id = 13 : i64, referenced_path = "bad.a", referenced_symbol = @s1.$root::@s3.bad::@s4.bad::@s6.a, semantic_type = !obelisk.integral<1, false, true, 0 : 0, logic>} {
                }
              }
            }
          }
        }
      }
    }
  }
}
