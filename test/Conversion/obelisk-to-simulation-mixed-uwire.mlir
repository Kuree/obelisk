// RUN: obelisk-opt %s '--lower-obelisk-to-sim=opt-level=0' | FileCheck %s
// RUN: obelisk-opt %s '--lower-obelisk-to-sim=opt-level=0' '--encode-obelisk-sim-to-bytecode=vpi=off' -o /dev/null
// RUN: obelisk-opt %s '--lower-obelisk-to-sim=opt-level=0' 2>&1 | FileCheck %s --check-prefix=NOWARN --implicit-check-not="uwire port connection"

module attributes {
  llvm.data_layout = "e-m:e-p:64:64-i64:64-n8:16:32:64-S128"
} {
  obelisk.sv.symbol.definition @s0.mixed_uwire attributes {definition_kind = 0 : i32, hierarchical_name = "mixed_uwire", name = "mixed_uwire", node_id = 0 : i64} {
  }
  obelisk.sv.symbol.definition @s1.mixed_uwire_child attributes {definition_kind = 0 : i32, hierarchical_name = "mixed_uwire_child", name = "mixed_uwire_child", node_id = 1 : i64} {
  }
  obelisk.sv.symbol.root @s2.$root attributes {hierarchical_name = "\\$root ", name = "$root", node_id = 2 : i64} {
    obelisk.sv.symbol.compilation_unit @s3 attributes {hierarchical_name = "$unit", node_id = 3 : i64} {
    }
    obelisk.sv.symbol.instance @s4.mixed_uwire attributes {hierarchical_name = "mixed_uwire", is_uninstantiated = false, name = "mixed_uwire", node_id = 4 : i64, referenced_path = "mixed_uwire", referenced_symbol = @s0.mixed_uwire} {
      obelisk.sv.symbol.instance_body @s5.mixed_uwire attributes {hierarchical_name = "mixed_uwire", name = "mixed_uwire", node_id = 5 : i64} {
        obelisk.sv.symbol.net @s6.value attributes {hierarchical_name = "mixed_uwire.value", is_implicit = false, name = "value", net_kind = 1 : i32, node_id = 6 : i64, semantic_type = !obelisk.integral<1, false, true, 0 : 0, logic>} {
        }
        obelisk.sv.symbol.instance @s7.child attributes {hierarchical_name = "mixed_uwire.child", is_uninstantiated = false, name = "child", node_id = 7 : i64, referenced_path = "mixed_uwire_child", referenced_symbol = @s1.mixed_uwire_child} {
          obelisk.sv.port.connection attributes {actual_is_constant = false, direction = 1 : i32, formal_name = "value", formal_ordinal = 0 : i64, formal_path = "mixed_uwire.child.value", formal_symbol = @s2.$root::@s4.mixed_uwire::@s5.mixed_uwire::@s7.child::@s8.mixed_uwire_child::@s9.value, formal_type = !obelisk.integral<1, false, true, 0 : 0, logic>, internal_path = "mixed_uwire.child.value", internal_symbol = @s2.$root::@s4.mixed_uwire::@s5.mixed_uwire::@s7.child::@s8.mixed_uwire_child::@s10.value, is_ansi = true, is_net = true, node_id = 8 : i64, provenance = 0 : i32} {
          } {
            obelisk.sv.expression.assignment attributes {assignment_kind = 0 : i32, node_id = 9 : i64, semantic_type = !obelisk.integral<1, false, true, 0 : 0, logic>} {
              obelisk.sv.expression.named_value attributes {node_id = 10 : i64, referenced_path = "mixed_uwire.value", referenced_symbol = @s2.$root::@s4.mixed_uwire::@s5.mixed_uwire::@s6.value, semantic_type = !obelisk.integral<1, false, true, 0 : 0, logic>} {
              }
              obelisk.sv.expression.empty_argument attributes {node_id = 11 : i64, semantic_type = !obelisk.integral<1, false, true, 0 : 0, logic>} {
              }
            }
          }
          obelisk.sv.symbol.instance_body @s8.mixed_uwire_child attributes {hierarchical_name = "mixed_uwire.child", name = "mixed_uwire_child", node_id = 12 : i64} {
            obelisk.sv.symbol.port @s9.value attributes {direction = 1 : i32, hierarchical_name = "mixed_uwire.child.value", name = "value", node_id = 13 : i64, semantic_type = !obelisk.integral<1, false, true, 0 : 0, logic>} {
            }
            obelisk.sv.symbol.net @s10.value attributes {hierarchical_name = "mixed_uwire.child.value", is_implicit = false, name = "value", net_kind = 12 : i32, node_id = 14 : i64, semantic_type = !obelisk.integral<1, false, true, 0 : 0, logic>} {
            }
            obelisk.sv.symbol.continuous_assign @s11 attributes {hierarchical_name = "mixed_uwire.child", node_id = 15 : i64, time_precision_fs = 1000000 : i64, time_unit_fs = 1000000 : i64} {
              obelisk.sv.expression.assignment attributes {assignment_kind = 0 : i32, node_id = 16 : i64, semantic_type = !obelisk.integral<1, false, true, 0 : 0, logic>} {
                obelisk.sv.expression.named_value attributes {node_id = 17 : i64, referenced_path = "mixed_uwire.child.value", referenced_symbol = @s2.$root::@s4.mixed_uwire::@s5.mixed_uwire::@s7.child::@s8.mixed_uwire_child::@s10.value, semantic_type = !obelisk.integral<1, false, true, 0 : 0, logic>} {
                }
                obelisk.sv.expression.conversion attributes {node_id = 18 : i64, semantic_type = !obelisk.integral<1, false, true, 0 : 0, logic>} {
                  obelisk.sv.expression.integer_literal attributes {constant_value = "1'b1", node_id = 19 : i64, semantic_type = !obelisk.ranged_packed_array<0 : 0 x !obelisk.integral<1, false, false, 0 : 0, bit>>} {
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

// IEEE 1800-2017 23.3.3.7/Table 23-1: the internal uwire dominates the
// external wire regardless of canonical endpoint ordering.
// CHECK-DAG: simulation.net.decl 0 {{.*}} hierarchy "mixed_uwire.value"
// CHECK-DAG: simulation.net.decl 1 {{.*}} hierarchy "mixed_uwire.child.value" {{.*}}resolution_kind = 2 : i32
// CHECK: simulation.net.connect.decl 0 {{.*}} 0[0] to 1[0] width 1 reversed = false provenance "ordered" rhs_dominates = true
// NOWARN: simulation.net.connect.decl 0
