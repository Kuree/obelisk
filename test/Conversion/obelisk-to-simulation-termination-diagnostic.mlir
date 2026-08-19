// RUN: obelisk-opt %s '--lower-obelisk-to-sim=opt-level=0' | FileCheck %s

// IEEE 1800-2017 20.2, Table 20-1: $finish and $stop take a verbosity of 0, 1,
// or 2 and default to 1. Zero prints nothing; the others print the simulation
// time and the location of the call.

module {
  obelisk.sv.symbol.definition attributes {definition_kind = 0 : i32, hierarchical_name = "termination_diagnostic", name = "termination_diagnostic", node_id = 0 : i64, original_source_range = !obelisk.source_range<"termination-diagnostic.sv", 1, 1, "termination-diagnostic.sv", 12, 10, "">, source_end_column = 10 : i64, source_end_line = 12 : i64, source_file = "termination-diagnostic.sv", source_range = !obelisk.source_range<"termination-diagnostic.sv", 1, 1, "termination-diagnostic.sv", 12, 10, "">, sym_name = "s0.termination_diagnostic"} {
  }
  obelisk.sv.symbol.root attributes {hierarchical_name = "\\$root ", name = "$root", node_id = 1 : i64, sym_name = "s1.$root"} {
    obelisk.sv.symbol.compilation_unit attributes {hierarchical_name = "$unit", node_id = 2 : i64, original_source_range = !obelisk.source_range<"termination-diagnostic.sv", 1, 1, "termination-diagnostic.sv", 13, 1, "">, source_end_column = 1 : i64, source_end_line = 13 : i64, source_file = "termination-diagnostic.sv", source_range = !obelisk.source_range<"termination-diagnostic.sv", 1, 1, "termination-diagnostic.sv", 13, 1, "">, sym_name = "s2"} {
    }
    obelisk.sv.symbol.instance attributes {hierarchical_name = "termination_diagnostic", is_uninstantiated = false, name = "termination_diagnostic", node_id = 3 : i64, original_source_range = !obelisk.source_range<"termination-diagnostic.sv", 1, 8, "termination-diagnostic.sv", 1, 8, "">, referenced_path = "termination_diagnostic", referenced_symbol = @s0.termination_diagnostic, source_end_column = 8 : i64, source_end_line = 1 : i64, source_file = "termination-diagnostic.sv", source_range = !obelisk.source_range<"termination-diagnostic.sv", 1, 8, "termination-diagnostic.sv", 1, 8, "">, sym_name = "s3.termination_diagnostic"} {
      obelisk.sv.symbol.instance_body attributes {hierarchical_name = "termination_diagnostic", name = "termination_diagnostic", node_id = 4 : i64, original_source_range = !obelisk.source_range<"termination-diagnostic.sv", 1, 1, "termination-diagnostic.sv", 12, 10, "">, source_end_column = 10 : i64, source_end_line = 12 : i64, source_file = "termination-diagnostic.sv", source_range = !obelisk.source_range<"termination-diagnostic.sv", 1, 1, "termination-diagnostic.sv", 12, 10, "">, sym_name = "s4.termination_diagnostic", time_precision_fs = 1000000 : i64, time_unit_fs = 1000000 : i64} {
        obelisk.sv.symbol.variable attributes {hierarchical_name = "termination_diagnostic.verbosity", lifetime = 1 : i32, name = "verbosity", node_id = 5 : i64, original_source_range = !obelisk.source_range<"termination-diagnostic.sv", 2, 7, "termination-diagnostic.sv", 2, 16, "">, semantic_type = !obelisk.integral<32, true, false, 31 : 0, int>, source_end_column = 16 : i64, source_end_line = 2 : i64, source_file = "termination-diagnostic.sv", source_range = !obelisk.source_range<"termination-diagnostic.sv", 2, 7, "termination-diagnostic.sv", 2, 16, "">, sym_name = "s5.verbosity"} {
        }
        obelisk.sv.symbol.procedural_block attributes {hierarchical_name = "termination_diagnostic", node_id = 6 : i64, original_source_range = !obelisk.source_range<"termination-diagnostic.sv", 3, 3, "termination-diagnostic.sv", 5, 6, "">, procedure_kind = 0 : i32, source_end_column = 6 : i64, source_end_line = 5 : i64, source_file = "termination-diagnostic.sv", source_range = !obelisk.source_range<"termination-diagnostic.sv", 3, 3, "termination-diagnostic.sv", 5, 6, "">, sym_name = "s6", time_precision_fs = 1000000 : i64, time_unit_fs = 1000000 : i64} {
          obelisk.sv.statement.block attributes {node_id = 7 : i64, original_source_range = !obelisk.source_range<"termination-diagnostic.sv", 4, 5, "termination-diagnostic.sv", 4, 16, "">, source_end_column = 16 : i64, source_end_line = 4 : i64, source_file = "termination-diagnostic.sv", source_range = !obelisk.source_range<"termination-diagnostic.sv", 4, 5, "termination-diagnostic.sv", 4, 16, "">} {
            obelisk.sv.statement.expression_statement attributes {node_id = 8 : i64, original_source_range = !obelisk.source_range<"termination-diagnostic.sv", 4, 5, "termination-diagnostic.sv", 4, 16, "">, source_end_column = 16 : i64, source_end_line = 4 : i64, source_file = "termination-diagnostic.sv", source_range = !obelisk.source_range<"termination-diagnostic.sv", 4, 5, "termination-diagnostic.sv", 4, 16, "">} {
              obelisk.sv.expression.call attributes {argument_count = 1 : i64, callee_name = "$finish", constraint_restrictions = [], defaulted_arguments = array<i64: 0>, has_inline_constraints = false, has_iterator_expression = false, has_output_arguments = false, has_this_class = false, is_signed = false, is_super_class = false, is_system_call = true, node_id = 9 : i64, original_source_range = !obelisk.source_range<"termination-diagnostic.sv", 4, 5, "termination-diagnostic.sv", 4, 15, "">, semantic_type = !obelisk.void, source_end_column = 15 : i64, source_end_line = 4 : i64, source_file = "termination-diagnostic.sv", source_range = !obelisk.source_range<"termination-diagnostic.sv", 4, 5, "termination-diagnostic.sv", 4, 15, "">, subroutine_kind = 1 : i32, system_library_cell = "work.termination_diagnostic", system_scope_path = "termination_diagnostic", system_scope_symbol = @s1.$root::@s3.termination_diagnostic::@s4.termination_diagnostic} {
                obelisk.sv.expression.integer_literal attributes {constant_value = "0", is_declared_unsized = true, is_signed = true, node_id = 10 : i64, original_source_range = !obelisk.source_range<"termination-diagnostic.sv", 4, 13, "termination-diagnostic.sv", 4, 14, "">, semantic_type = !obelisk.integral<32, true, false, 31 : 0, int>, source_end_column = 14 : i64, source_end_line = 4 : i64, source_file = "termination-diagnostic.sv", source_range = !obelisk.source_range<"termination-diagnostic.sv", 4, 13, "termination-diagnostic.sv", 4, 14, "">} {
                }
              }
            }
          }
        }
        obelisk.sv.symbol.procedural_block attributes {hierarchical_name = "termination_diagnostic", node_id = 11 : i64, original_source_range = !obelisk.source_range<"termination-diagnostic.sv", 6, 3, "termination-diagnostic.sv", 8, 6, "">, procedure_kind = 0 : i32, source_end_column = 6 : i64, source_end_line = 8 : i64, source_file = "termination-diagnostic.sv", source_range = !obelisk.source_range<"termination-diagnostic.sv", 6, 3, "termination-diagnostic.sv", 8, 6, "">, sym_name = "s7", time_precision_fs = 1000000 : i64, time_unit_fs = 1000000 : i64} {
          obelisk.sv.statement.block attributes {node_id = 12 : i64, original_source_range = !obelisk.source_range<"termination-diagnostic.sv", 7, 5, "termination-diagnostic.sv", 7, 11, "">, source_end_column = 11 : i64, source_end_line = 7 : i64, source_file = "termination-diagnostic.sv", source_range = !obelisk.source_range<"termination-diagnostic.sv", 7, 5, "termination-diagnostic.sv", 7, 11, "">} {
            obelisk.sv.statement.expression_statement attributes {node_id = 13 : i64, original_source_range = !obelisk.source_range<"termination-diagnostic.sv", 7, 5, "termination-diagnostic.sv", 7, 11, "">, source_end_column = 11 : i64, source_end_line = 7 : i64, source_file = "termination-diagnostic.sv", source_range = !obelisk.source_range<"termination-diagnostic.sv", 7, 5, "termination-diagnostic.sv", 7, 11, "">} {
              obelisk.sv.expression.call attributes {argument_count = 0 : i64, callee_name = "$stop", constraint_restrictions = [], defaulted_arguments = array<i64>, has_inline_constraints = false, has_iterator_expression = false, has_output_arguments = false, has_this_class = false, is_signed = false, is_super_class = false, is_system_call = true, node_id = 14 : i64, original_source_range = !obelisk.source_range<"termination-diagnostic.sv", 7, 5, "termination-diagnostic.sv", 7, 10, "">, semantic_type = !obelisk.void, source_end_column = 10 : i64, source_end_line = 7 : i64, source_file = "termination-diagnostic.sv", source_range = !obelisk.source_range<"termination-diagnostic.sv", 7, 5, "termination-diagnostic.sv", 7, 10, "">, subroutine_kind = 1 : i32, system_library_cell = "work.termination_diagnostic", system_scope_path = "termination_diagnostic", system_scope_symbol = @s1.$root::@s3.termination_diagnostic::@s4.termination_diagnostic} {
              }
            }
          }
        }
        obelisk.sv.symbol.procedural_block attributes {hierarchical_name = "termination_diagnostic", node_id = 15 : i64, original_source_range = !obelisk.source_range<"termination-diagnostic.sv", 9, 3, "termination-diagnostic.sv", 11, 6, "">, procedure_kind = 0 : i32, source_end_column = 6 : i64, source_end_line = 11 : i64, source_file = "termination-diagnostic.sv", source_range = !obelisk.source_range<"termination-diagnostic.sv", 9, 3, "termination-diagnostic.sv", 11, 6, "">, sym_name = "s8", time_precision_fs = 1000000 : i64, time_unit_fs = 1000000 : i64} {
          obelisk.sv.statement.block attributes {node_id = 16 : i64, original_source_range = !obelisk.source_range<"termination-diagnostic.sv", 10, 5, "termination-diagnostic.sv", 10, 22, "">, source_end_column = 22 : i64, source_end_line = 10 : i64, source_file = "termination-diagnostic.sv", source_range = !obelisk.source_range<"termination-diagnostic.sv", 10, 5, "termination-diagnostic.sv", 10, 22, "">} {
            obelisk.sv.statement.expression_statement attributes {node_id = 17 : i64, original_source_range = !obelisk.source_range<"termination-diagnostic.sv", 10, 5, "termination-diagnostic.sv", 10, 22, "">, source_end_column = 22 : i64, source_end_line = 10 : i64, source_file = "termination-diagnostic.sv", source_range = !obelisk.source_range<"termination-diagnostic.sv", 10, 5, "termination-diagnostic.sv", 10, 22, "">} {
              obelisk.sv.expression.call attributes {argument_count = 1 : i64, callee_name = "$stop", constraint_restrictions = [], defaulted_arguments = array<i64: 0>, has_inline_constraints = false, has_iterator_expression = false, has_output_arguments = false, has_this_class = false, is_signed = false, is_super_class = false, is_system_call = true, node_id = 18 : i64, original_source_range = !obelisk.source_range<"termination-diagnostic.sv", 10, 5, "termination-diagnostic.sv", 10, 21, "">, semantic_type = !obelisk.void, source_end_column = 21 : i64, source_end_line = 10 : i64, source_file = "termination-diagnostic.sv", source_range = !obelisk.source_range<"termination-diagnostic.sv", 10, 5, "termination-diagnostic.sv", 10, 21, "">, subroutine_kind = 1 : i32, system_library_cell = "work.termination_diagnostic", system_scope_path = "termination_diagnostic", system_scope_symbol = @s1.$root::@s3.termination_diagnostic::@s4.termination_diagnostic} {
                obelisk.sv.expression.named_value attributes {is_signed = true, node_id = 19 : i64, original_source_range = !obelisk.source_range<"termination-diagnostic.sv", 10, 11, "termination-diagnostic.sv", 10, 20, "">, referenced_path = "termination_diagnostic.verbosity", referenced_symbol = @s1.$root::@s3.termination_diagnostic::@s4.termination_diagnostic::@s5.verbosity, semantic_type = !obelisk.integral<32, true, false, 31 : 0, int>, source_end_column = 20 : i64, source_end_line = 10 : i64, source_file = "termination-diagnostic.sv", source_range = !obelisk.source_range<"termination-diagnostic.sv", 10, 11, "termination-diagnostic.sv", 10, 20, "">} {
                }
              }
            }
          }
        }
      }
    }
  }
}


// A verbosity of 0 selects Table 20-1's "prints nothing" row, so the finish
// request stands alone.
// CHECK-LABEL: obelisk_sim.func private @unit_0
// CHECK-NOT: obelisk_sim.display
// CHECK: obelisk_sim.finish

// The default verbosity of 1 prints the time and the location. The message is
// a byte-string format with the time as its argument: rendering it into an
// !obelisk_sim.string would make the message managed state and cost the whole
// design its native schedule.
// CHECK-LABEL: obelisk_sim.func private @unit_1
// CHECK: obelisk_sim.bytes.constant "$stop: termination-diagnostic.sv:7: simulation time %0t\0A"
// CHECK-NOT: obelisk_sim.string
// CHECK: obelisk_sim.time.now
// CHECK: obelisk_sim.display
// CHECK: obelisk_sim.stop

// A verbosity the compiler cannot fold picks its Table 20-1 row at run time,
// by branching around the diagnostic rather than by emptying a message.
// CHECK-LABEL: obelisk_sim.func private @unit_2
// CHECK: %[[WANTED:.*]] = arith.cmpi ne
// CHECK: cf.cond_br %[[WANTED]], ^[[REPORT:.*]], ^[[MERGE:.*]]
// CHECK: ^[[REPORT]]:
// CHECK: obelisk_sim.display
// CHECK: cf.br ^[[MERGE]]
// CHECK: ^[[MERGE]]:
// CHECK: obelisk_sim.stop
