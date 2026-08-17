// RUN: obelisk-opt %s '--lower-obelisk-to-sim=opt-level=0' | FileCheck %s

// IEEE 1800-2017 9.2.2.2.1 excludes a variable written by a called function
// from the caller's always_comb sensitivity set. The helper deliberately also
// reads the written variable; only source may wake the process.
// CHECK-LABEL: obelisk_sim.func private @unit_1(
// CHECK-SAME: %[[SOURCE:[a-zA-Z0-9_]+]]: !obelisk_sim.ref<!obelisk_sim.logic<1>>
// CHECK-SAME: %[[SCRATCH:[a-zA-Z0-9_]+]]: !obelisk_sim.ref<!obelisk_sim.logic<1>>
// CHECK: obelisk_sim.call @unit_0
// CHECK: obelisk_sim.suspend.change %[[SOURCE]]
// CHECK-NOT: obelisk_sim.suspend{{.*}}%[[SCRATCH]]

module {
  obelisk.sv.symbol.definition attributes {
    definition_kind = 0 : i32, hierarchical_name = "top", name = "top",
    node_id = 0 : i64, sym_name = "s0.top"
  } {
  }
  obelisk.sv.symbol.root attributes {
    hierarchical_name = "\\$root ", name = "$root", node_id = 1 : i64,
    sym_name = "s1.$root"
  } {
    obelisk.sv.symbol.compilation_unit attributes {
      hierarchical_name = "$unit", node_id = 2 : i64, sym_name = "s2"
    } {
    }
    obelisk.sv.symbol.instance attributes {
      hierarchical_name = "top", is_uninstantiated = false, name = "top",
      node_id = 3 : i64, referenced_path = "top",
      referenced_symbol = @s0.top, sym_name = "s3.top"
    } {
      obelisk.sv.symbol.instance_body attributes {
        hierarchical_name = "top", name = "top", node_id = 4 : i64,
        sym_name = "s4.top", time_precision_fs = 1000000 : i64,
        time_unit_fs = 1000000 : i64
      } {
        obelisk.sv.symbol.variable attributes {
          hierarchical_name = "top.source", lifetime = 1 : i32,
          name = "source", node_id = 5 : i64,
          semantic_type = !obelisk.integral<1, false, true, 0 : 0, logic>,
          sym_name = "s5.source"
        } {
        }
        obelisk.sv.symbol.variable attributes {
          hierarchical_name = "top.scratch", lifetime = 1 : i32,
          name = "scratch", node_id = 6 : i64,
          semantic_type = !obelisk.integral<1, false, true, 0 : 0, logic>,
          sym_name = "s6.scratch"
        } {
        }
        obelisk.sv.symbol.subroutine attributes {
          hierarchical_name = "top.update", name = "update",
          node_id = 7 : i64,
          semantic_type = !obelisk.subroutine<() -> !obelisk.void, false>,
          subroutine_kind = 0 : i32, sym_name = "s7.update",
          time_precision_fs = 1000000 : i64,
          time_unit_fs = 1000000 : i64
        } {
          obelisk.sv.statement.expression_statement attributes {
            node_id = 8 : i64
          } {
            obelisk.sv.expression.named_value attributes {
              is_signed = false, node_id = 9 : i64,
              referenced_path = "top.scratch",
              referenced_symbol = @s1.$root::@s3.top::@s4.top::@s6.scratch,
              semantic_type = !obelisk.integral<1, false, true, 0 : 0, logic>
            } {
            }
          }
          obelisk.sv.statement.expression_statement attributes {
            node_id = 10 : i64
          } {
            obelisk.sv.expression.assignment attributes {
              assignment_kind = 0 : i32, is_signed = false,
              node_id = 11 : i64,
              semantic_type = !obelisk.integral<1, false, true, 0 : 0, logic>
            } {
              obelisk.sv.expression.named_value attributes {
                is_signed = false, node_id = 12 : i64,
                referenced_path = "top.scratch",
                referenced_symbol = @s1.$root::@s3.top::@s4.top::@s6.scratch,
                semantic_type = !obelisk.integral<1, false, true, 0 : 0, logic>
              } {
              }
              obelisk.sv.expression.named_value attributes {
                is_signed = false, node_id = 13 : i64,
                referenced_path = "top.source",
                referenced_symbol = @s1.$root::@s3.top::@s4.top::@s5.source,
                semantic_type = !obelisk.integral<1, false, true, 0 : 0, logic>
              } {
              }
            }
          }
        }
        obelisk.sv.symbol.procedural_block attributes {
          hierarchical_name = "top", node_id = 14 : i64,
          procedure_kind = 3 : i32, sym_name = "s8",
          time_precision_fs = 1000000 : i64,
          time_unit_fs = 1000000 : i64
        } {
          obelisk.sv.statement.expression_statement attributes {
            node_id = 15 : i64
          } {
            obelisk.sv.expression.call attributes {
              argument_count = 0 : i64, callee_name = "update",
              constraint_restrictions = [], has_inline_constraints = false,
              has_iterator_expression = false,
              has_output_arguments = false, has_this_class = false,
              is_super_class = false, is_system_call = false,
              node_id = 16 : i64, referenced_path = "top.update",
              referenced_symbol = @s1.$root::@s3.top::@s4.top::@s7.update,
              semantic_type = !obelisk.void, subroutine_kind = 0 : i32
            } {
            }
          }
        }
      }
    }
  }
}
