// RUN: obelisk-opt %s '--lower-obelisk-to-sim=opt-level=0' | FileCheck %s

module {
  obelisk.sv.symbol.definition @s0.supported_static_local attributes {definition_kind = 0 : i32, hierarchical_name = "supported_static_local", name = "supported_static_local", node_id = 0 : i64} {
  }
  obelisk.sv.symbol.root @s1.$root attributes {hierarchical_name = "\\$root ", name = "$root", node_id = 1 : i64} {
    obelisk.sv.symbol.compilation_unit @s2 attributes {hierarchical_name = "$unit", node_id = 2 : i64} {
    }
    obelisk.sv.symbol.instance @s3.supported_static_local attributes {hierarchical_name = "supported_static_local", is_uninstantiated = false, name = "supported_static_local", node_id = 3 : i64, referenced_path = "supported_static_local", referenced_symbol = @s0.supported_static_local} {
      obelisk.sv.symbol.instance_body @s4.supported_static_local attributes {hierarchical_name = "supported_static_local", name = "supported_static_local", node_id = 4 : i64} {
        obelisk.sv.symbol.variable @s12.source attributes {hierarchical_name = "supported_static_local.source", lifetime = 1 : i32, name = "source", node_id = 12 : i64, semantic_type = !obelisk.integral<1, false, true, 0 : 0, logic>} {
          obelisk.sv.expression.conversion attributes {node_id = 13 : i64, semantic_type = !obelisk.integral<1, false, true, 0 : 0, logic>} {
            obelisk.sv.expression.integer_literal attributes {constant_value = "1'b1", node_id = 14 : i64, semantic_type = !obelisk.ranged_packed_array<0 : 0 x !obelisk.integral<1, false, false, 0 : 0, bit>>} {
            }
          }
        }
        obelisk.sv.symbol.statement_block @s5 attributes {block_kind = 0 : i32, hierarchical_name = "supported_static_local", node_id = 5 : i64} {
          obelisk.sv.symbol.variable @s6.value attributes {hierarchical_name = "supported_static_local.value", lifetime = 1 : i32, name = "value", node_id = 6 : i64, semantic_type = !obelisk.integral<1, false, true, 0 : 0, logic>} {
            obelisk.sv.expression.conversion attributes {node_id = 7 : i64, semantic_type = !obelisk.integral<1, false, true, 0 : 0, logic>} {
              obelisk.sv.expression.named_value attributes {node_id = 8 : i64, referenced_path = "supported_static_local.source", referenced_symbol = @s1.$root::@s3.supported_static_local::@s4.supported_static_local::@s12.source, semantic_type = !obelisk.integral<1, false, true, 0 : 0, logic>} {
              }
            }
          }
        }
        obelisk.sv.symbol.procedural_block @s7 attributes {hierarchical_name = "supported_static_local", node_id = 9 : i64, procedure_kind = 0 : i32, time_precision_fs = 1000000 : i64, time_unit_fs = 1000000 : i64} {
          obelisk.sv.statement.block attributes {node_id = 10 : i64} {
            obelisk.sv.statement.variable_declaration attributes {node_id = 11 : i64, referenced_path = "supported_static_local.value", referenced_symbol = @s1.$root::@s3.supported_static_local::@s4.supported_static_local::@s5::@s6.value} {
            }
          }
        }
      }
    }
  }
}

// A static initializer is called once by the root before the initial process;
// its procedural declaration must not evaluate the initializer again.
// CHECK-LABEL: simulation.func @__obelisk_root
// CHECK: simulation.call @unit_0
// CHECK: simulation.call @unit_1
// CHECK: simulation.spawn @unit_2
// CHECK-LABEL: simulation.func private @unit_0
// CHECK: simulation.ref.store
// CHECK-LABEL: simulation.func private @unit_1
// CHECK: simulation.ref.load
// CHECK: simulation.ref.store
// CHECK-LABEL: simulation.func private @unit_2
// CHECK-NOT: simulation.static.once
// CHECK-NOT: simulation.ref.store
// CHECK-NOT: obelisk.sv.
