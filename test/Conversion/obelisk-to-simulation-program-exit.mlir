// RUN: obelisk-opt %s '--lower-obelisk-to-sim=opt-level=0' | FileCheck %s

// Source: module m; initial begin $exit; $display("continued"); end endmodule
//
// IEEE 1800-2023 24.7: `$exit` ends only a program whose initial procedure the
// calling thread originates in; any other thread ignores it and continues.
// CHECK-LABEL: simulation.func private @unit_0(
// CHECK: %[[OWNED:.+]] = simulation.program.owned %arg0
// CHECK-NEXT: cf.cond_br %[[OWNED]], ^[[EXIT:bb[0-9]+]], ^[[CONTINUE:bb[0-9]+]]
// CHECK: ^[[EXIT]]:
// CHECK-NEXT: simulation.program.exit %arg0
// CHECK-NEXT: simulation.return
// CHECK: ^[[CONTINUE]]:
// CHECK: simulation.display

module attributes {coverage.language_version = 2023 : i32} {
  obelisk.sv.symbol.definition @s0.m attributes {definition_kind = 0 : i32, hierarchical_name = "m", name = "m", node_id = 0 : i64} {
  }
  obelisk.sv.symbol.root @s1.$root attributes {hierarchical_name = "\\$root ", name = "$root", node_id = 1 : i64} {
    obelisk.sv.symbol.compilation_unit @s2 attributes {hierarchical_name = "$unit", node_id = 2 : i64, simulation.vpi_definition_name = "$unit"} {
    }
    obelisk.sv.symbol.instance @s3.m attributes {hierarchical_name = "m", is_uninstantiated = false, name = "m", node_id = 3 : i64, referenced_path = "m", referenced_symbol = @s0.m} {
      obelisk.sv.symbol.instance_body @s4.m attributes {hierarchical_name = "m", name = "m", node_id = 4 : i64, simulation.vpi_automatic = false, simulation.vpi_cell_instance = false, simulation.vpi_definition_name = "m", simulation.vpi_top = true, time_precision_fs = 1000000 : i64, time_unit_fs = 1000000 : i64, vpi_scope_kind = 32 : i32} {
        obelisk.sv.symbol.procedural_block @s5 attributes {hierarchical_name = "m", node_id = 5 : i64, procedure_kind = 0 : i32, time_precision_fs = 1000000 : i64, time_unit_fs = 1000000 : i64} {
          obelisk.sv.statement.block attributes {node_id = 6 : i64} {
            obelisk.sv.statement.list attributes {node_id = 7 : i64} {
              obelisk.sv.statement.expression_statement attributes {node_id = 8 : i64} {
                obelisk.sv.expression.call attributes {argument_count = 0 : i64, callee_name = "$exit", constraint_restrictions = [], defaulted_arguments = array<i64>, has_inline_constraints = false, has_iterator_expression = false, has_output_arguments = false, has_this_class = false, is_signed = false, is_super_class = false, is_system_call = true, node_id = 9 : i64, semantic_type = !obelisk.void, subroutine_kind = 1 : i32, system_library_cell = "work.m", system_scope_path = "m", system_scope_symbol = @s1.$root::@s3.m::@s4.m} {
                }
              }
              obelisk.sv.statement.expression_statement attributes {node_id = 10 : i64} {
                obelisk.sv.expression.call attributes {argument_count = 1 : i64, callee_name = "$display", constraint_restrictions = [], defaulted_arguments = array<i64: 0>, has_inline_constraints = false, has_iterator_expression = false, has_output_arguments = false, has_this_class = false, is_signed = false, is_super_class = false, is_system_call = true, node_id = 11 : i64, semantic_type = !obelisk.void, subroutine_kind = 1 : i32, system_library_cell = "work.m", system_scope_path = "m", system_scope_symbol = @s1.$root::@s3.m::@s4.m} {
                  obelisk.sv.expression.string_literal attributes {constant_value = "continued", is_signed = false, node_id = 12 : i64, semantic_type = !obelisk.ranged_packed_array<71 : 0 x !obelisk.integral<1, false, false, 0 : 0, bit>>} {
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

