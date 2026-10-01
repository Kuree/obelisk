// RUN: obelisk-opt %s '--lower-obelisk-to-sim=opt-level=0' | FileCheck %s
// RUN: sed 's/!descending = !obelisk.ranged_unpacked_array<1 : 0/!descending = !obelisk.ranged_unpacked_array<2 : 0/' %s | not obelisk-opt '--lower-obelisk-to-sim=opt-level=0' 2>&1 | FileCheck %s --check-prefix=SHAPE
// RUN: sed 's/!descending = !obelisk.ranged_unpacked_array<1 : 0 x !vif>/!descending = !obelisk.ranged_unpacked_array<1 : 0 x i32>/' %s | not obelisk-opt '--lower-obelisk-to-sim=opt-level=0' 2>&1 | FileCheck %s --check-prefix=ELEMENT

// IEEE 1800-2017 25.9 permits a real interface instance to initialize a
// compatible virtual-interface variable. 7.6 applies that conversion by
// position to fixed unpacked arrays. Bind the exact elaborated scope selected
// by each declared source index; array declaration order is not necessarily
// the order in which Slang emits the instance children.

!vif = !obelisk.virtual_interface<@s2.$root::@s5.top::@s6.descending::@s7, "">
!descending = !obelisk.ranged_unpacked_array<1 : 0 x !vif>
!ascending = !obelisk.ranged_unpacked_array<0 : 1 x !vif>
!nested = !obelisk.ranged_unpacked_array<1 : 0 x !ascending>

module {
  obelisk.sv.symbol.definition @s0.bind_if attributes {definition_kind = 1 : i32, hierarchical_name = "bind_if", name = "bind_if", node_id = 0 : i64} {}
  obelisk.sv.symbol.definition @s1.top attributes {definition_kind = 0 : i32, hierarchical_name = "top", name = "top", node_id = 1 : i64} {}
  obelisk.sv.symbol.root @s2.$root attributes {hierarchical_name = "\\$root ", name = "$root", node_id = 2 : i64} {
    obelisk.sv.symbol.compilation_unit @s3 attributes {hierarchical_name = "$unit", node_id = 3 : i64} {}
    obelisk.sv.symbol.instance @s4.top attributes {hierarchical_name = "top", is_uninstantiated = false, name = "top", node_id = 4 : i64, referenced_path = "top", referenced_symbol = @s1.top} {
      obelisk.sv.symbol.instance_body @s5.top attributes {hierarchical_name = "top", name = "top", node_id = 5 : i64, time_precision_fs = 1000000 : i64, time_unit_fs = 1000000 : i64} {
        obelisk.sv.symbol.instance_array @s6.descending attributes {hierarchical_name = "top.descending", name = "descending", node_id = 6 : i64} {
          obelisk.sv.symbol.instance @s7 attributes {hierarchical_name = "top.descending[0]", is_uninstantiated = false, node_id = 7 : i64, referenced_path = "bind_if", referenced_symbol = @s0.bind_if} {
            obelisk.sv.symbol.instance_body @s8.bind_if attributes {hierarchical_name = "top.descending[0]", name = "bind_if", node_id = 8 : i64, virtual_interface_identity = @s2.$root::@s5.top::@s6.descending::@s7} {}
          }
          obelisk.sv.symbol.instance @s9 attributes {hierarchical_name = "top.descending[1]", is_uninstantiated = false, node_id = 9 : i64, referenced_path = "bind_if", referenced_symbol = @s0.bind_if} {
            obelisk.sv.symbol.instance_body @s10.bind_if attributes {hierarchical_name = "top.descending[1]", name = "bind_if", node_id = 10 : i64, virtual_interface_identity = @s2.$root::@s5.top::@s6.descending::@s7} {}
          }
        }
        obelisk.sv.symbol.instance_array @s11.ascending attributes {hierarchical_name = "top.ascending", name = "ascending", node_id = 11 : i64} {
          obelisk.sv.symbol.instance @s12 attributes {hierarchical_name = "top.ascending[0]", is_uninstantiated = false, node_id = 12 : i64, referenced_path = "bind_if", referenced_symbol = @s0.bind_if} {
            obelisk.sv.symbol.instance_body @s13.bind_if attributes {hierarchical_name = "top.ascending[0]", name = "bind_if", node_id = 13 : i64, virtual_interface_identity = @s2.$root::@s5.top::@s6.descending::@s7} {}
          }
          obelisk.sv.symbol.instance @s14 attributes {hierarchical_name = "top.ascending[1]", is_uninstantiated = false, node_id = 14 : i64, referenced_path = "bind_if", referenced_symbol = @s0.bind_if} {
            obelisk.sv.symbol.instance_body @s15.bind_if attributes {hierarchical_name = "top.ascending[1]", name = "bind_if", node_id = 15 : i64, virtual_interface_identity = @s2.$root::@s5.top::@s6.descending::@s7} {}
          }
        }
        obelisk.sv.symbol.instance_array @s16.nested attributes {hierarchical_name = "top.nested", name = "nested", node_id = 16 : i64} {
          obelisk.sv.symbol.instance_array @s17 attributes {hierarchical_name = "top.nested", node_id = 17 : i64} {
            obelisk.sv.symbol.instance @s18 attributes {hierarchical_name = "top.nested[0][0]", is_uninstantiated = false, node_id = 18 : i64, referenced_path = "bind_if", referenced_symbol = @s0.bind_if} {
              obelisk.sv.symbol.instance_body @s19.bind_if attributes {hierarchical_name = "top.nested[0][0]", name = "bind_if", node_id = 19 : i64, virtual_interface_identity = @s2.$root::@s5.top::@s6.descending::@s7} {}
            }
            obelisk.sv.symbol.instance @s20 attributes {hierarchical_name = "top.nested[0][1]", is_uninstantiated = false, node_id = 20 : i64, referenced_path = "bind_if", referenced_symbol = @s0.bind_if} {
              obelisk.sv.symbol.instance_body @s21.bind_if attributes {hierarchical_name = "top.nested[0][1]", name = "bind_if", node_id = 21 : i64, virtual_interface_identity = @s2.$root::@s5.top::@s6.descending::@s7} {}
            }
          }
          obelisk.sv.symbol.instance_array @s22 attributes {hierarchical_name = "top.nested", node_id = 22 : i64} {
            obelisk.sv.symbol.instance @s23 attributes {hierarchical_name = "top.nested[1][0]", is_uninstantiated = false, node_id = 23 : i64, referenced_path = "bind_if", referenced_symbol = @s0.bind_if} {
              obelisk.sv.symbol.instance_body @s24.bind_if attributes {hierarchical_name = "top.nested[1][0]", name = "bind_if", node_id = 24 : i64, virtual_interface_identity = @s2.$root::@s5.top::@s6.descending::@s7} {}
            }
            obelisk.sv.symbol.instance @s25 attributes {hierarchical_name = "top.nested[1][1]", is_uninstantiated = false, node_id = 25 : i64, referenced_path = "bind_if", referenced_symbol = @s0.bind_if} {
              obelisk.sv.symbol.instance_body @s26.bind_if attributes {hierarchical_name = "top.nested[1][1]", name = "bind_if", node_id = 26 : i64, virtual_interface_identity = @s2.$root::@s5.top::@s6.descending::@s7} {}
            }
          }
        }
        obelisk.sv.symbol.variable @s27.descending_handles attributes {hierarchical_name = "top.descending_handles", lifetime = 1 : i32, name = "descending_handles", node_id = 27 : i64, semantic_type = !descending} {}
        obelisk.sv.symbol.variable @s28.ascending_handles attributes {hierarchical_name = "top.ascending_handles", lifetime = 1 : i32, name = "ascending_handles", node_id = 28 : i64, semantic_type = !ascending} {}
        obelisk.sv.symbol.variable @s29.nested_handles attributes {hierarchical_name = "top.nested_handles", lifetime = 1 : i32, name = "nested_handles", node_id = 29 : i64, semantic_type = !nested} {}
        obelisk.sv.symbol.procedural_block @s30 attributes {hierarchical_name = "top", node_id = 30 : i64, procedure_kind = 0 : i32, time_precision_fs = 1000000 : i64, time_unit_fs = 1000000 : i64} {
          obelisk.sv.statement.block attributes {node_id = 31 : i64} {
            obelisk.sv.statement.list attributes {node_id = 32 : i64} {
              obelisk.sv.statement.expression_statement attributes {node_id = 33 : i64} {
                obelisk.sv.expression.assignment attributes {assignment_kind = 0 : i32, node_id = 34 : i64, semantic_type = !descending} {
                  obelisk.sv.expression.named_value attributes {node_id = 35 : i64, referenced_path = "top.descending_handles", referenced_symbol = @s2.$root::@s4.top::@s5.top::@s27.descending_handles, semantic_type = !descending} {}
                  obelisk.sv.expression.arbitrary_symbol attributes {node_id = 36 : i64, referenced_path = "top.descending", referenced_symbol = @s2.$root::@s4.top::@s5.top::@s6.descending, semantic_type = !descending} {}
                }
              }
              obelisk.sv.statement.expression_statement attributes {node_id = 37 : i64} {
                obelisk.sv.expression.assignment attributes {assignment_kind = 0 : i32, node_id = 38 : i64, semantic_type = !ascending} {
                  obelisk.sv.expression.named_value attributes {node_id = 39 : i64, referenced_path = "top.ascending_handles", referenced_symbol = @s2.$root::@s4.top::@s5.top::@s28.ascending_handles, semantic_type = !ascending} {}
                  obelisk.sv.expression.arbitrary_symbol attributes {node_id = 40 : i64, referenced_path = "top.ascending", referenced_symbol = @s2.$root::@s4.top::@s5.top::@s11.ascending, semantic_type = !ascending} {}
                }
              }
              obelisk.sv.statement.expression_statement attributes {node_id = 41 : i64} {
                obelisk.sv.expression.assignment attributes {assignment_kind = 0 : i32, node_id = 42 : i64, semantic_type = !nested} {
                  obelisk.sv.expression.named_value attributes {node_id = 43 : i64, referenced_path = "top.nested_handles", referenced_symbol = @s2.$root::@s4.top::@s5.top::@s29.nested_handles, semantic_type = !nested} {}
                  obelisk.sv.expression.arbitrary_symbol attributes {node_id = 44 : i64, referenced_path = "top.nested", referenced_symbol = @s2.$root::@s4.top::@s5.top::@s16.nested, semantic_type = !nested} {}
                }
              }
            }
          }
        }
      }
    }
  }
}

// CHECK: %[[D1:.*]] = simulation.virtual_interface.bind 3
// CHECK: %[[D0:.*]] = simulation.virtual_interface.bind 2
// CHECK: %[[DESC:.*]] = simulation.aggregate.construct %[[D1]], %[[D0]]
// CHECK: %[[A0:.*]] = simulation.virtual_interface.bind 4
// CHECK: %[[A1:.*]] = simulation.virtual_interface.bind 5
// CHECK: %[[ASC:.*]] = simulation.aggregate.construct %[[A0]], %[[A1]]
// CHECK: %[[N10:.*]] = simulation.virtual_interface.bind 8
// CHECK: %[[N11:.*]] = simulation.virtual_interface.bind 9
// CHECK: %[[N1:.*]] = simulation.aggregate.construct %[[N10]], %[[N11]]
// CHECK: %[[N00:.*]] = simulation.virtual_interface.bind 6
// CHECK: %[[N01:.*]] = simulation.virtual_interface.bind 7
// CHECK: %[[N0:.*]] = simulation.aggregate.construct %[[N00]], %[[N01]]
// CHECK: simulation.aggregate.construct %[[N1]], %[[N0]]
// CHECK-NOT: simulation.container
// CHECK-NOT: obelisk.sv.
// SHAPE: interface reference has no executable elaborated scope: top.descending[2]
// ELEMENT: interface reference has no executable value type: 'i32'
