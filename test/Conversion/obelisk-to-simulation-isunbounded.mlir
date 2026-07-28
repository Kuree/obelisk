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
        obelisk.sv.symbol.parameter attributes {
          constant_value = "$",
          hierarchical_name = "top.unbounded",
          name = "unbounded",
          node_id = 5 : i64,
          semantic_type = !obelisk.integral<32, true, false, 31 : 0, int>,
          sym_name = "s5.unbounded"
        } {
          obelisk.sv.expression.conversion attributes {
            node_id = 6 : i64,
            semantic_type = !obelisk.integral<32, true, false, 31 : 0, int>
          } {
            obelisk.sv.expression.unbounded_literal attributes {
              node_id = 7 : i64,
              semantic_type = !obelisk.unbounded
            } {
            }
          }
        }
        obelisk.sv.symbol.procedural_block attributes {
          hierarchical_name = "top",
          node_id = 8 : i64,
          procedure_kind = 0 : i32,
          sym_name = "s6",
          time_precision_fs = 1000000 : i64,
          time_unit_fs = 1000000 : i64
        } {
          obelisk.sv.statement.expression_statement attributes {
            node_id = 9 : i64
          } {
            obelisk.sv.expression.call attributes {
              argument_count = 1 : i64,
              callee_name = "$display",
              constraint_restrictions = [],
              has_inline_constraints = false,
              has_iterator_expression = false,
              has_output_arguments = false,
              has_this_class = false,
              is_super_class = false,
              is_system_call = true,
              node_id = 10 : i64,
              semantic_type = !obelisk.void,
              subroutine_kind = 1 : i32
            } {
              obelisk.sv.expression.call attributes {
                argument_count = 1 : i64,
                callee_name = "$isunbounded",
                constraint_restrictions = [],
                has_inline_constraints = false,
                has_iterator_expression = false,
                has_output_arguments = false,
                has_this_class = false,
                is_super_class = false,
                is_system_call = true,
                node_id = 11 : i64,
                semantic_type = !obelisk.integral<1, false, false, 0 : 0, bit>,
                subroutine_kind = 0 : i32
              } {
                obelisk.sv.expression.integer_literal attributes {
                  constant_value = "1",
                  node_id = 12 : i64,
                  semantic_type = !obelisk.integral<32, true, false, 31 : 0, int>
                } {
                }
              }
            }
          }
          obelisk.sv.statement.expression_statement attributes {
            node_id = 13 : i64
          } {
            obelisk.sv.expression.call attributes {
              argument_count = 1 : i64,
              callee_name = "$display",
              constraint_restrictions = [],
              has_inline_constraints = false,
              has_iterator_expression = false,
              has_output_arguments = false,
              has_this_class = false,
              is_super_class = false,
              is_system_call = true,
              node_id = 14 : i64,
              semantic_type = !obelisk.void,
              subroutine_kind = 1 : i32
            } {
              obelisk.sv.expression.call attributes {
                argument_count = 1 : i64,
                callee_name = "$isunbounded",
                constraint_restrictions = [],
                has_inline_constraints = false,
                has_iterator_expression = false,
                has_output_arguments = false,
                has_this_class = false,
                is_super_class = false,
                is_system_call = true,
                node_id = 15 : i64,
                semantic_type = !obelisk.integral<1, false, false, 0 : 0, bit>,
                subroutine_kind = 0 : i32
              } {
                obelisk.sv.expression.named_value attributes {
                  node_id = 16 : i64,
                  referenced_path = "top.unbounded",
                  referenced_symbol = @s1.$root::@s3.top::@s4.top::@s5.unbounded,
                  semantic_type = !obelisk.unbounded
                } {
                }
              }
            }
          }
        }
      }
    }
  }
}

// CHECK-LABEL: obelisk_sim.func private @unit_0(
// CHECK-DAG: %[[FALSE:.*]] = arith.constant false
// CHECK-DAG: %[[TRUE:.*]] = arith.constant true
// CHECK: obelisk_sim.display {{.*}}(%[[FALSE]])
// CHECK: obelisk_sim.display {{.*}}(%[[TRUE]])
// CHECK-NOT: !obelisk.unbounded
