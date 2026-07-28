// RUN: obelisk-opt %s '--lower-obelisk-to-sim=opt-level=0' | FileCheck %s

module {
  obelisk.sv.symbol.definition attributes {
    definition_kind = 0 : i32,
    hierarchical_name = "top",
    name = "top",
    node_id = 0 : i64,
    sym_name = "s0.top"
  } {
  }
  obelisk.sv.symbol.root attributes {
    hierarchical_name = "\\$root ",
    name = "$root",
    node_id = 1 : i64,
    sym_name = "s1.$root"
  } {
    obelisk.sv.symbol.compilation_unit attributes {
      hierarchical_name = "$unit",
      node_id = 2 : i64,
      sym_name = "s2"
    } {
    }
    obelisk.sv.symbol.instance attributes {
      hierarchical_name = "top",
      is_uninstantiated = false,
      name = "top",
      node_id = 3 : i64,
      referenced_path = "top",
      referenced_symbol = @s0.top,
      sym_name = "s3.top"
    } {
      obelisk.sv.symbol.instance_body attributes {
        hierarchical_name = "top",
        name = "top",
        node_id = 4 : i64,
        sym_name = "s4.top"
      } {
        obelisk.sv.symbol.procedural_block attributes {
          hierarchical_name = "top",
          node_id = 5 : i64,
          procedure_kind = 0 : i32,
          sym_name = "s5",
          time_precision_fs = 1000000000 : i64,
          time_unit_fs = 1000000000000 : i64
        } {
          obelisk.sv.statement.expression_statement attributes {
            node_id = 6 : i64
          } {
            obelisk.sv.expression.call attributes {
              argument_count = 0 : i64,
              callee_name = "$printtimescale",
              constraint_restrictions = [],
              has_inline_constraints = false,
              has_iterator_expression = false,
              has_output_arguments = false,
              has_this_class = false,
              is_super_class = false,
              is_system_call = true,
              node_id = 7 : i64,
              semantic_type = !obelisk.void,
              subroutine_kind = 1 : i32,
              system_scope_path = "top",
              system_scope_symbol = @s1.$root::@s3.top::@s4.top
            } {
            }
          }
        }
      }
    }
  }
}

// CHECK: obelisk_sim.scope.decl 0
// CHECK-SAME: dpi_precision_femtoseconds = 1000000000
// CHECK-SAME: dpi_unit_femtoseconds = 1000000000
// CHECK: obelisk_sim.scope.decl 1
// CHECK-SAME: hierarchy "top"
// CHECK-SAME: dpi_precision_femtoseconds = 1000000000
// CHECK-SAME: dpi_unit_femtoseconds = 1000000000000
// CHECK-LABEL: obelisk_sim.func private @unit_0(
// CHECK: %[[TEXT:.*]] = obelisk_sim.bytes.constant "Time scale of (top) is 1ms / 1us"
// CHECK: obelisk_sim.display {{.*}}(%[[TEXT]])
