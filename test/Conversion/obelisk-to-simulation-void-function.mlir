// RUN: obelisk-opt %s '--lower-obelisk-to-sim=opt-level=0' | FileCheck %s

module {
  obelisk.sv.symbol.definition attributes {
    definition_kind = 0 : i32,
    hierarchical_name = "void_function",
    name = "void_function",
    node_id = 0 : i64,
    sym_name = "s0.void_function"
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
      hierarchical_name = "void_function",
      is_uninstantiated = false,
      name = "void_function",
      node_id = 3 : i64,
      referenced_path = "void_function",
      referenced_symbol = @s0.void_function,
      sym_name = "s3.void_function"
    } {
      obelisk.sv.symbol.instance_body attributes {
        hierarchical_name = "void_function",
        name = "void_function",
        node_id = 4 : i64,
        sym_name = "s4.void_function"
      } {
        obelisk.sv.symbol.subroutine attributes {
          hierarchical_name = "void_function.run",
          name = "run",
          node_id = 5 : i64,
          semantic_type = !obelisk.subroutine<() -> !obelisk.void, false>,
          subroutine_kind = 0 : i32,
          sym_name = "s5.run",
          time_precision_fs = 1000000 : i64,
          time_unit_fs = 1000000 : i64
        } {
        }
        obelisk.sv.symbol.procedural_block attributes {
          hierarchical_name = "void_function",
          node_id = 6 : i64,
          procedure_kind = 0 : i32,
          sym_name = "s6",
          time_precision_fs = 1000000 : i64,
          time_unit_fs = 1000000 : i64
        } {
          obelisk.sv.statement.expression_statement attributes {
            node_id = 7 : i64
          } {
            obelisk.sv.expression.call attributes {
              argument_count = 0 : i64,
              callee_name = "run",
              constraint_restrictions = [],
              has_inline_constraints = false,
              has_iterator_expression = false,
              has_output_arguments = false,
              has_this_class = false,
              is_super_class = false,
              is_system_call = false,
              node_id = 8 : i64,
              referenced_path = "void_function.run",
              referenced_symbol = @s1.$root::@s3.void_function::@s4.void_function::@s5.run,
              semantic_type = !obelisk.void,
              subroutine_kind = 0 : i32
            } {
            }
          }
        }
      }
    }
  }
}

// CHECK-LABEL: obelisk_sim.func private @unit_0(
// CHECK-SAME: obelisk_sim.void_function
// CHECK: obelisk_sim.return
// CHECK-LABEL: obelisk_sim.func private @unit_1(
// CHECK: obelisk_sim.call @unit_0(
// CHECK-SAME: -> ()
// CHECK-NOT: !obelisk.void
