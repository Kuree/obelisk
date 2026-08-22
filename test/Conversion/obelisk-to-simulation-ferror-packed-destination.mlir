// RUN: obelisk-opt %s '--lower-obelisk-to-sim=opt-level=0' | FileCheck %s

// IEEE 1800-2017 21.3.7: $ferror writes a description of the most recent file
// error into `str`, which should be a packed array of at least 640 bits or a
// string type.  A packed destination therefore takes the same bytes a string
// one would, through the string-to-packed conversion of 5.9.

module {
  obelisk.sv.symbol.definition attributes {
    definition_kind = 0 : i32, hierarchical_name = "t", name = "t",
    node_id = 0 : i64, sym_name = "s0.t"
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
      hierarchical_name = "t", is_uninstantiated = false, name = "t",
      node_id = 3 : i64, referenced_path = "t", referenced_symbol = @s0.t,
      sym_name = "s3.t"
    } {
      obelisk.sv.symbol.instance_body attributes {
        hierarchical_name = "t", name = "t", node_id = 4 : i64,
        sym_name = "s4.t", time_precision_fs = 1000000 : i64,
        time_unit_fs = 1000000 : i64
      } {
        obelisk.sv.symbol.variable attributes {
          hierarchical_name = "t.i", lifetime = 1 : i32, name = "i",
          node_id = 5 : i64,
          semantic_type = !obelisk.integral<32, true, true, 31 : 0, integer>,
          sym_name = "s5.i"
        } {
        }
        obelisk.sv.symbol.variable attributes {
          hierarchical_name = "t.letterl", lifetime = 1 : i32,
          name = "letterl", node_id = 6 : i64,
          semantic_type = !obelisk.ranged_packed_array<800 : 1 x !obelisk.integral<1, false, true, 0 : 0, reg>>,
          sym_name = "s6.letterl"
        } {
        }
        obelisk.sv.symbol.procedural_block attributes {
          hierarchical_name = "t", node_id = 7 : i64, procedure_kind = 0 : i32,
          sym_name = "s7", time_precision_fs = 1000000 : i64,
          time_unit_fs = 1000000 : i64
        } {
          obelisk.sv.statement.expression_statement attributes {
            node_id = 8 : i64
          } {
            obelisk.sv.expression.assignment attributes {
              assignment_kind = 0 : i32, is_signed = true, node_id = 9 : i64,
              semantic_type = !obelisk.integral<32, true, true, 31 : 0, integer>
            } {
              obelisk.sv.expression.named_value attributes {
                is_signed = true, node_id = 10 : i64, referenced_path = "t.i",
                referenced_symbol = @s1.$root::@s3.t::@s4.t::@s5.i,
                semantic_type = !obelisk.integral<32, true, true, 31 : 0, integer>
              } {
              }
              obelisk.sv.expression.call attributes {
                argument_count = 2 : i64, callee_name = "$ferror",
                constraint_restrictions = [],
                defaulted_arguments = array<i64: 0, 0>,
                has_inline_constraints = false,
                has_iterator_expression = false, has_output_arguments = true,
                has_this_class = false, is_signed = true,
                is_super_class = false, is_system_call = true,
                node_id = 11 : i64,
                semantic_type = !obelisk.integral<32, true, true, 31 : 0, integer>,
                subroutine_kind = 0 : i32, system_library_cell = "work.t",
                system_scope_path = "t",
                system_scope_symbol = @s1.$root::@s3.t::@s4.t
              } {
                obelisk.sv.expression.integer_literal attributes {
                  constant_value = "0", is_declared_unsized = true,
                  is_signed = true, node_id = 12 : i64,
                  semantic_type = !obelisk.integral<32, true, false, 31 : 0, int>
                } {
                }
                obelisk.sv.expression.assignment attributes {
                  assignment_kind = 0 : i32, is_signed = false,
                  node_id = 13 : i64,
                  semantic_type = !obelisk.ranged_packed_array<800 : 1 x !obelisk.integral<1, false, true, 0 : 0, reg>>
                } {
                  obelisk.sv.expression.named_value attributes {
                    is_signed = false, node_id = 14 : i64,
                    referenced_path = "t.letterl",
                    referenced_symbol = @s1.$root::@s3.t::@s4.t::@s6.letterl,
                    semantic_type = !obelisk.ranged_packed_array<800 : 1 x !obelisk.integral<1, false, true, 0 : 0, reg>>
                  } {
                  }
                  obelisk.sv.expression.empty_argument attributes {
                    is_signed = false, node_id = 15 : i64,
                    semantic_type = !obelisk.ranged_packed_array<800 : 1 x !obelisk.integral<1, false, true, 0 : 0, reg>>
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
}



// CHECK-LABEL: obelisk_sim.func private @unit_0
// CHECK: %[[MSG:.*]], %[[CODE:.*]] = obelisk_sim.file.error_string
// CHECK: %[[BITS:.*]] = obelisk_sim.string.to_packed %[[MSG]] : (!obelisk_sim.string) -> i800
// CHECK: %[[LOGIC:.*]] = obelisk_sim.logic.from_bits %[[BITS]] : i800 -> !obelisk_sim.logic<800>
// CHECK: %[[PACKED:.*]] = obelisk_sim.packed.unflatten %[[LOGIC]]
// CHECK: obelisk_sim.ref.store %[[PACKED]] to %arg2
// CHECK: %[[ERRNO:.*]] = obelisk_sim.logic.from_bits %[[CODE]] : i32 -> !obelisk_sim.logic<32>
// CHECK: obelisk_sim.ref.store %[[ERRNO]] to %arg1
