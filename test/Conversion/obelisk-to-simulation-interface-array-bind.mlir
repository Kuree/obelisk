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
  obelisk.sv.symbol.definition attributes {definition_kind = 1 : i32, hierarchical_name = "bind_if", name = "bind_if", node_id = 0 : i64, sym_name = "s0.bind_if"} {}
  obelisk.sv.symbol.definition attributes {definition_kind = 0 : i32, hierarchical_name = "top", name = "top", node_id = 1 : i64, sym_name = "s1.top"} {}
  obelisk.sv.symbol.root attributes {hierarchical_name = "\\$root ", name = "$root", node_id = 2 : i64, sym_name = "s2.$root"} {
    obelisk.sv.symbol.compilation_unit attributes {hierarchical_name = "$unit", node_id = 3 : i64, sym_name = "s3"} {}
    obelisk.sv.symbol.instance attributes {hierarchical_name = "top", is_uninstantiated = false, name = "top", node_id = 4 : i64, referenced_path = "top", referenced_symbol = @s1.top, sym_name = "s4.top"} {
      obelisk.sv.symbol.instance_body attributes {hierarchical_name = "top", name = "top", node_id = 5 : i64, sym_name = "s5.top", time_precision_fs = 1000000 : i64, time_unit_fs = 1000000 : i64} {
        obelisk.sv.symbol.instance_array attributes {hierarchical_name = "top.descending", name = "descending", node_id = 6 : i64, sym_name = "s6.descending"} {
          obelisk.sv.symbol.instance attributes {hierarchical_name = "top.descending[0]", is_uninstantiated = false, node_id = 7 : i64, referenced_path = "bind_if", referenced_symbol = @s0.bind_if, sym_name = "s7"} {
            obelisk.sv.symbol.instance_body attributes {hierarchical_name = "top.descending[0]", name = "bind_if", node_id = 8 : i64, sym_name = "s8.bind_if", virtual_interface_identity = @s2.$root::@s5.top::@s6.descending::@s7} {}
          }
          obelisk.sv.symbol.instance attributes {hierarchical_name = "top.descending[1]", is_uninstantiated = false, node_id = 9 : i64, referenced_path = "bind_if", referenced_symbol = @s0.bind_if, sym_name = "s9"} {
            obelisk.sv.symbol.instance_body attributes {hierarchical_name = "top.descending[1]", name = "bind_if", node_id = 10 : i64, sym_name = "s10.bind_if", virtual_interface_identity = @s2.$root::@s5.top::@s6.descending::@s7} {}
          }
        }
        obelisk.sv.symbol.instance_array attributes {hierarchical_name = "top.ascending", name = "ascending", node_id = 11 : i64, sym_name = "s11.ascending"} {
          obelisk.sv.symbol.instance attributes {hierarchical_name = "top.ascending[0]", is_uninstantiated = false, node_id = 12 : i64, referenced_path = "bind_if", referenced_symbol = @s0.bind_if, sym_name = "s12"} {
            obelisk.sv.symbol.instance_body attributes {hierarchical_name = "top.ascending[0]", name = "bind_if", node_id = 13 : i64, sym_name = "s13.bind_if", virtual_interface_identity = @s2.$root::@s5.top::@s6.descending::@s7} {}
          }
          obelisk.sv.symbol.instance attributes {hierarchical_name = "top.ascending[1]", is_uninstantiated = false, node_id = 14 : i64, referenced_path = "bind_if", referenced_symbol = @s0.bind_if, sym_name = "s14"} {
            obelisk.sv.symbol.instance_body attributes {hierarchical_name = "top.ascending[1]", name = "bind_if", node_id = 15 : i64, sym_name = "s15.bind_if", virtual_interface_identity = @s2.$root::@s5.top::@s6.descending::@s7} {}
          }
        }
        obelisk.sv.symbol.instance_array attributes {hierarchical_name = "top.nested", name = "nested", node_id = 16 : i64, sym_name = "s16.nested"} {
          obelisk.sv.symbol.instance_array attributes {hierarchical_name = "top.nested", node_id = 17 : i64, sym_name = "s17"} {
            obelisk.sv.symbol.instance attributes {hierarchical_name = "top.nested[0][0]", is_uninstantiated = false, node_id = 18 : i64, referenced_path = "bind_if", referenced_symbol = @s0.bind_if, sym_name = "s18"} {
              obelisk.sv.symbol.instance_body attributes {hierarchical_name = "top.nested[0][0]", name = "bind_if", node_id = 19 : i64, sym_name = "s19.bind_if", virtual_interface_identity = @s2.$root::@s5.top::@s6.descending::@s7} {}
            }
            obelisk.sv.symbol.instance attributes {hierarchical_name = "top.nested[0][1]", is_uninstantiated = false, node_id = 20 : i64, referenced_path = "bind_if", referenced_symbol = @s0.bind_if, sym_name = "s20"} {
              obelisk.sv.symbol.instance_body attributes {hierarchical_name = "top.nested[0][1]", name = "bind_if", node_id = 21 : i64, sym_name = "s21.bind_if", virtual_interface_identity = @s2.$root::@s5.top::@s6.descending::@s7} {}
            }
          }
          obelisk.sv.symbol.instance_array attributes {hierarchical_name = "top.nested", node_id = 22 : i64, sym_name = "s22"} {
            obelisk.sv.symbol.instance attributes {hierarchical_name = "top.nested[1][0]", is_uninstantiated = false, node_id = 23 : i64, referenced_path = "bind_if", referenced_symbol = @s0.bind_if, sym_name = "s23"} {
              obelisk.sv.symbol.instance_body attributes {hierarchical_name = "top.nested[1][0]", name = "bind_if", node_id = 24 : i64, sym_name = "s24.bind_if", virtual_interface_identity = @s2.$root::@s5.top::@s6.descending::@s7} {}
            }
            obelisk.sv.symbol.instance attributes {hierarchical_name = "top.nested[1][1]", is_uninstantiated = false, node_id = 25 : i64, referenced_path = "bind_if", referenced_symbol = @s0.bind_if, sym_name = "s25"} {
              obelisk.sv.symbol.instance_body attributes {hierarchical_name = "top.nested[1][1]", name = "bind_if", node_id = 26 : i64, sym_name = "s26.bind_if", virtual_interface_identity = @s2.$root::@s5.top::@s6.descending::@s7} {}
            }
          }
        }
        obelisk.sv.symbol.variable attributes {hierarchical_name = "top.descending_handles", lifetime = 1 : i32, name = "descending_handles", node_id = 27 : i64, semantic_type = !descending, sym_name = "s27.descending_handles"} {}
        obelisk.sv.symbol.variable attributes {hierarchical_name = "top.ascending_handles", lifetime = 1 : i32, name = "ascending_handles", node_id = 28 : i64, semantic_type = !ascending, sym_name = "s28.ascending_handles"} {}
        obelisk.sv.symbol.variable attributes {hierarchical_name = "top.nested_handles", lifetime = 1 : i32, name = "nested_handles", node_id = 29 : i64, semantic_type = !nested, sym_name = "s29.nested_handles"} {}
        obelisk.sv.symbol.procedural_block attributes {hierarchical_name = "top", node_id = 30 : i64, procedure_kind = 0 : i32, sym_name = "s30", time_precision_fs = 1000000 : i64, time_unit_fs = 1000000 : i64} {
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
