// RUN: obelisk-opt %s '--lower-obelisk-to-sim=opt-level=0' | FileCheck %s

// IEEE 1800-2017 7.12.3: an array reduction method applies to any unpacked
// array of integral values and returns a single value of the array's element
// type.  For `logic [3:0] foo[1:0]` that element type is a packed array, not a
// scalar, so the fold runs over the flattened four-state vector and the result
// is handed back in the declared packed-array spelling.

module {
  obelisk.sv.symbol.definition @s0.t attributes {
    definition_kind = 0 : i32, hierarchical_name = "t", name = "t",
    node_id = 0 : i64
  } {
  }
  obelisk.sv.symbol.root @s1.$root attributes {
    hierarchical_name = "\\$root ", name = "$root", node_id = 1 : i64
  } {
    obelisk.sv.symbol.compilation_unit @s2 attributes {
      hierarchical_name = "$unit", node_id = 2 : i64
    } {
    }
    obelisk.sv.symbol.instance @s3.t attributes {
      hierarchical_name = "t", is_uninstantiated = false, name = "t",
      node_id = 3 : i64, referenced_path = "t", referenced_symbol = @s0.t
    } {
      obelisk.sv.symbol.instance_body @s4.t attributes {
        hierarchical_name = "t", name = "t", node_id = 4 : i64,
        time_precision_fs = 1000000 : i64,
        time_unit_fs = 1000000 : i64
      } {
        obelisk.sv.symbol.variable @s5.foo attributes {
          hierarchical_name = "t.foo", lifetime = 1 : i32, name = "foo",
          node_id = 5 : i64,
          semantic_type = !obelisk.ranged_unpacked_array<1 : 0 x !obelisk.ranged_packed_array<3 : 0 x !obelisk.integral<1, false, true, 0 : 0, logic>>>
        } {
        }
        obelisk.sv.symbol.variable @s6.r attributes {
          hierarchical_name = "t.r", lifetime = 1 : i32, name = "r",
          node_id = 6 : i64,
          semantic_type = !obelisk.ranged_packed_array<3 : 0 x !obelisk.integral<1, false, true, 0 : 0, logic>>
        } {
        }
        obelisk.sv.symbol.procedural_block @s7 attributes {
          hierarchical_name = "t", node_id = 7 : i64, procedure_kind = 0 : i32,
          time_precision_fs = 1000000 : i64,
          time_unit_fs = 1000000 : i64
        } {
          obelisk.sv.statement.expression_statement attributes {
            node_id = 8 : i64
          } {
            obelisk.sv.expression.assignment attributes {
              assignment_kind = 0 : i32, is_signed = false, node_id = 9 : i64,
              semantic_type = !obelisk.ranged_packed_array<3 : 0 x !obelisk.integral<1, false, true, 0 : 0, logic>>
            } {
              obelisk.sv.expression.named_value attributes {
                is_signed = false, node_id = 10 : i64, referenced_path = "t.r",
                referenced_symbol = @s1.$root::@s3.t::@s4.t::@s6.r,
                semantic_type = !obelisk.ranged_packed_array<3 : 0 x !obelisk.integral<1, false, true, 0 : 0, logic>>
              } {
              }
              obelisk.sv.expression.call attributes {
                argument_count = 1 : i64, callee_name = "or",
                constraint_restrictions = [],
                defaulted_arguments = array<i64: 0>,
                has_inline_constraints = false,
                has_iterator_expression = false, has_output_arguments = false,
                has_this_class = false, is_signed = false,
                is_super_class = false, is_system_call = true,
                node_id = 11 : i64,
                semantic_type = !obelisk.ranged_packed_array<3 : 0 x !obelisk.integral<1, false, true, 0 : 0, logic>>,
                subroutine_kind = 0 : i32, system_library_cell = "work.t",
                system_scope_path = "t",
                system_scope_symbol = @s1.$root::@s3.t::@s4.t
              } {
                obelisk.sv.expression.named_value attributes {
                  is_signed = false, node_id = 12 : i64,
                  referenced_path = "t.foo",
                  referenced_symbol = @s1.$root::@s3.t::@s4.t::@s5.foo,
                  semantic_type = !obelisk.ranged_unpacked_array<1 : 0 x !obelisk.ranged_packed_array<3 : 0 x !obelisk.integral<1, false, true, 0 : 0, logic>>>
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



// CHECK-LABEL: simulation.func private @unit_0
// CHECK: %[[IDENTITY:.*]] = simulation.logic.constant 0 : i4, 0 : i4 : !simulation.logic<4>
// CHECK: %[[E0:.*]] = simulation.packed.flatten %{{.*}} -> !simulation.logic<4>
// CHECK: %[[ACC0:.*]] = simulation.logic.binary or %[[E0]], %[[IDENTITY]] : !simulation.logic<4>
// CHECK: %[[E1:.*]] = simulation.packed.flatten %{{.*}} -> !simulation.logic<4>
// CHECK: %[[ACC1:.*]] = simulation.logic.binary or %[[ACC0]], %[[E1]] : !simulation.logic<4>
// CHECK: %[[RESULT:.*]] = simulation.packed.unflatten %[[ACC1]] : (!simulation.logic<4>) -> !simulation.packed_array<3 : 0 x !simulation.logic<1>>
// CHECK: simulation.ref.store %[[RESULT]] to %arg2
