// RUN: obelisk-opt %s '--lower-obelisk-to-sim=opt-level=0' 2>&1 | FileCheck %s

module attributes {
  llvm.data_layout = "e-m:e-p:64:64-i64:64-n8:16:32:64-S128"
} {
  obelisk.sv.symbol.definition @s0.trireg_parent attributes {definition_kind = 0 : i32, hierarchical_name = "trireg_parent", name = "trireg_parent", node_id = 0 : i64} {
  }
  obelisk.sv.symbol.definition @s1.trireg_child attributes {definition_kind = 0 : i32, hierarchical_name = "trireg_child", name = "trireg_child", node_id = 1 : i64} {
  }
  obelisk.sv.symbol.root @s2.$root attributes {hierarchical_name = "\\$root ", name = "$root", node_id = 2 : i64} {
    obelisk.sv.symbol.compilation_unit @s3 attributes {hierarchical_name = "$unit", node_id = 3 : i64} {
    }
    obelisk.sv.symbol.instance @s4.trireg_parent attributes {hierarchical_name = "trireg_parent", is_uninstantiated = false, name = "trireg_parent", node_id = 4 : i64, referenced_path = "trireg_parent", referenced_symbol = @s0.trireg_parent} {
      obelisk.sv.symbol.instance_body @s5.trireg_parent attributes {hierarchical_name = "trireg_parent", name = "trireg_parent", node_id = 5 : i64} {
        obelisk.sv.symbol.net @s6.value attributes {hierarchical_name = "trireg_parent.value", is_implicit = false, name = "value", net_kind = 12 : i32, node_id = 6 : i64, semantic_type = !obelisk.integral<1, false, true, 0 : 0, logic>} {
        }
        obelisk.sv.symbol.instance @s7.child attributes {hierarchical_name = "trireg_parent.child", is_uninstantiated = false, name = "child", node_id = 7 : i64, referenced_path = "trireg_child", referenced_symbol = @s1.trireg_child} {
          obelisk.sv.port.connection attributes {actual_is_constant = false, direction = 1 : i32, formal_name = "value", formal_ordinal = 0 : i64, formal_path = "trireg_parent.child.value", formal_symbol = @s2.$root::@s4.trireg_parent::@s5.trireg_parent::@s7.child::@s8.trireg_child::@s9.value, formal_type = !obelisk.integral<1, false, true, 0 : 0, logic>, internal_path = "trireg_parent.child.value", internal_symbol = @s2.$root::@s4.trireg_parent::@s5.trireg_parent::@s7.child::@s8.trireg_child::@s10.value, is_ansi = true, is_net = true, node_id = 8 : i64, provenance = 0 : i32} {
          } {
            obelisk.sv.expression.assignment attributes {assignment_kind = 0 : i32, node_id = 9 : i64, semantic_type = !obelisk.integral<1, false, true, 0 : 0, logic>} {
              obelisk.sv.expression.named_value attributes {node_id = 10 : i64, referenced_path = "trireg_parent.value", referenced_symbol = @s2.$root::@s4.trireg_parent::@s5.trireg_parent::@s6.value, semantic_type = !obelisk.integral<1, false, true, 0 : 0, logic>} {
              }
              obelisk.sv.expression.empty_argument attributes {node_id = 11 : i64, semantic_type = !obelisk.integral<1, false, true, 0 : 0, logic>} {
              }
            }
          }
          obelisk.sv.symbol.instance_body @s8.trireg_child attributes {hierarchical_name = "trireg_parent.child", name = "trireg_child", node_id = 12 : i64} {
            obelisk.sv.symbol.port @s9.value attributes {direction = 1 : i32, hierarchical_name = "trireg_parent.child.value", name = "value", node_id = 13 : i64, semantic_type = !obelisk.integral<1, false, true, 0 : 0, logic>} {
            }
            obelisk.sv.symbol.net @s10.value attributes {hierarchical_name = "trireg_parent.child.value", is_implicit = false, name = "value", net_kind = 9 : i32, node_id = 14 : i64, semantic_type = !obelisk.integral<1, false, true, 0 : 0, logic>} {
            }
          }
        }
      }
    }
  }
}

// IEEE 1800-2017 Table 23-1: an internal trireg connected to an external
// uwire uses the external uwire type and requires a warning.
// CHECK: warning: dissimilar net types require a port-collapse warning
// CHECK-DAG: simulation.net.decl 0 {{.*}} hierarchy "trireg_parent.value" {{.*}}resolution_kind = 2 : i32
// CHECK-DAG: simulation.net.decl 1 {{.*}} hierarchy "trireg_parent.child.value" {{.*}}resolution_kind = 9 : i32
// CHECK: simulation.net.connect.decl 0 {{.*}} 0[0] to 1[0] width 1 reversed = false provenance "ordered" rhs_dominates = false
