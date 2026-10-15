// RUN: obelisk-opt %s '--lower-obelisk-to-sim=opt-level=0' | FileCheck %s
// RUN: obelisk-opt %s '--test-obelisk-edit-attributes=op=obelisk.sv.symbol.root remove=declared_time_precision_fs' \
// RUN:   '--lower-obelisk-to-sim=opt-level=0' | FileCheck %s --check-prefix=SCOPES

//   timeprecision 1ps;
//   module top;
//     timeunit 1ns/1ns;
//     initial #1step $display("%t", $realtime);
//   endmodule
//
// IEEE 1800-2023 3.14.3: the global time precision is the minimum of every
// timeprecision statement in the design, and one step is that precision. The
// compilation-unit declaration gives no elaborated scope its time scale, so
// it reaches lowering only as the precision the root declares. Without it the
// module's own 1ns precision is the finest.

module attributes {coverage.language_version = 2023 : i32} {
  obelisk.sv.symbol.definition @s0.top attributes {definition_kind = 0 : i32, hierarchical_name = "top", name = "top", node_id = 0 : i64} {
  }
  obelisk.sv.symbol.root @s1.$root attributes {declared_time_precision_fs = 1000 : i64, hierarchical_name = "\\$root ", name = "$root", node_id = 1 : i64} {
    obelisk.sv.symbol.compilation_unit @s2 attributes {hierarchical_name = "$unit", node_id = 2 : i64, simulation.vpi_definition_name = "$unit"} {
    }
    obelisk.sv.symbol.instance @s3.top attributes {hierarchical_name = "top", is_uninstantiated = false, name = "top", node_id = 3 : i64, referenced_path = "top", referenced_symbol = @s0.top} {
      obelisk.sv.symbol.instance_body @s4.top attributes {hierarchical_name = "top", name = "top", node_id = 4 : i64, simulation.vpi_automatic = false, simulation.vpi_cell_instance = false, simulation.vpi_definition_name = "top", simulation.vpi_top = true, time_precision_fs = 1000000 : i64, time_unit_fs = 1000000 : i64, vpi_scope_kind = 32 : i32} {
        obelisk.sv.symbol.procedural_block @s5 attributes {hierarchical_name = "top", node_id = 5 : i64, procedure_kind = 0 : i32, time_precision_fs = 1000000 : i64, time_unit_fs = 1000000 : i64} {
          obelisk.sv.statement.timed attributes {node_id = 6 : i64} {
            obelisk.sv.timing.one_step_delay attributes {node_id = 7 : i64} {
            }
            obelisk.sv.statement.expression_statement attributes {node_id = 8 : i64} {
              obelisk.sv.expression.call attributes {argument_count = 2 : i64, callee_name = "$display", constraint_restrictions = [], defaulted_arguments = array<i64: 0, 0>, has_inline_constraints = false, has_iterator_expression = false, has_output_arguments = false, has_this_class = false, is_signed = false, is_super_class = false, is_system_call = true, node_id = 9 : i64, semantic_type = !obelisk.void, subroutine_kind = 1 : i32, system_library_cell = "work.top", system_scope_path = "top", system_scope_symbol = @s1.$root::@s3.top::@s4.top} {
                obelisk.sv.expression.string_literal attributes {constant_value = "%t", is_signed = false, node_id = 10 : i64, semantic_type = !obelisk.ranged_packed_array<15 : 0 x !obelisk.integral<1, false, false, 0 : 0, bit>>} {
                }
                obelisk.sv.expression.call attributes {argument_count = 0 : i64, callee_name = "$realtime", constraint_restrictions = [], defaulted_arguments = array<i64>, has_inline_constraints = false, has_iterator_expression = false, has_output_arguments = false, has_this_class = false, is_signed = false, is_super_class = false, is_system_call = true, node_id = 11 : i64, semantic_type = !obelisk.realtime, subroutine_kind = 0 : i32, system_library_cell = "work.top", system_scope_path = "top", system_scope_symbol = @s1.$root::@s3.top::@s4.top} {
                }
              }
            }
          }
        }
      }
    }
  }
}


// CHECK: simulation.design @design attributes {{.*}}time_precision_fs = 1000 : i64
// CHECK: %[[STEP:.*]] = simulation.time.constant 1
// CHECK: simulation.suspend.delay %[[STEP]]

// SCOPES: simulation.design @design attributes {{.*}}time_precision_fs = 1000000 : i64
