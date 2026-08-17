// RUN: obelisk-opt %s '--lower-obelisk-to-sim=opt-level=0' | FileCheck %s

// A net port coerced to inout collapses the overlapping low-order bits even
// when the formal and actual widths differ. The conversion-wrapped empty
// argument is Slang's representation of a width-adjusted output lvalue.
module {
  obelisk.sv.symbol.definition attributes {definition_kind = 0 : i32, hierarchical_name = "inout_width_top", name = "inout_width_top", node_id = 0 : i64, sym_name = "s0.inout_width_top"} {
  }
  obelisk.sv.symbol.definition attributes {definition_kind = 0 : i32, hierarchical_name = "inout_width_child", name = "inout_width_child", node_id = 1 : i64, sym_name = "s1.inout_width_child"} {
  }
  obelisk.sv.symbol.root attributes {hierarchical_name = "\\$root ", name = "$root", node_id = 2 : i64, sym_name = "s2.$root"} {
    obelisk.sv.symbol.compilation_unit attributes {hierarchical_name = "$unit", node_id = 3 : i64, sym_name = "s3"} {
    }
    obelisk.sv.symbol.instance attributes {hierarchical_name = "inout_width_top", is_uninstantiated = false, name = "inout_width_top", node_id = 4 : i64, referenced_path = "inout_width_top", referenced_symbol = @s0.inout_width_top, sym_name = "s4.inout_width_top"} {
      obelisk.sv.symbol.instance_body attributes {hierarchical_name = "inout_width_top", name = "inout_width_top", node_id = 5 : i64, sym_name = "s5.inout_width_top"} {
        obelisk.sv.symbol.net attributes {hierarchical_name = "inout_width_top.actual", is_implicit = false, name = "actual", net_kind = 1 : i32, node_id = 6 : i64, semantic_type = !obelisk.ranged_packed_array<0 : 0 x !obelisk.integral<1, false, true, 0 : 0, logic>>, sym_name = "s6.actual"} {
        }
        obelisk.sv.symbol.instance attributes {hierarchical_name = "inout_width_top.child", is_uninstantiated = false, name = "child", node_id = 7 : i64, referenced_path = "inout_width_child", referenced_symbol = @s1.inout_width_child, sym_name = "s7.child"} {
          obelisk.sv.port.connection attributes {actual_is_constant = false, direction = 2 : i32, formal_name = "formal", formal_ordinal = 0 : i64, formal_path = "inout_width_top.child.formal", formal_symbol = @s2.$root::@s4.inout_width_top::@s5.inout_width_top::@s7.child::@s8.inout_width_child::@s9.formal, formal_type = !obelisk.ranged_packed_array<1 : 0 x !obelisk.integral<1, false, true, 0 : 0, logic>>, internal_path = "inout_width_top.child.formal", internal_symbol = @s2.$root::@s4.inout_width_top::@s5.inout_width_top::@s7.child::@s8.inout_width_child::@s10.formal, is_ansi = true, is_net = true, node_id = 8 : i64, provenance = 0 : i32} {
          } {
            obelisk.sv.expression.assignment attributes {assignment_kind = 0 : i32, node_id = 9 : i64, semantic_type = !obelisk.ranged_packed_array<0 : 0 x !obelisk.integral<1, false, true, 0 : 0, logic>>} {
              obelisk.sv.expression.named_value attributes {node_id = 10 : i64, referenced_path = "inout_width_top.actual", referenced_symbol = @s2.$root::@s4.inout_width_top::@s5.inout_width_top::@s6.actual, semantic_type = !obelisk.ranged_packed_array<0 : 0 x !obelisk.integral<1, false, true, 0 : 0, logic>>} {
              }
              obelisk.sv.expression.conversion attributes {node_id = 11 : i64, semantic_type = !obelisk.ranged_packed_array<0 : 0 x !obelisk.integral<1, false, true, 0 : 0, logic>>} {
                obelisk.sv.expression.empty_argument attributes {node_id = 12 : i64, semantic_type = !obelisk.ranged_packed_array<1 : 0 x !obelisk.integral<1, false, true, 0 : 0, logic>>} {
                }
              }
            }
          }
          obelisk.sv.symbol.instance_body attributes {hierarchical_name = "inout_width_top.child", name = "inout_width_child", node_id = 13 : i64, sym_name = "s8.inout_width_child"} {
            obelisk.sv.symbol.port attributes {direction = 1 : i32, hierarchical_name = "inout_width_top.child.formal", name = "formal", node_id = 14 : i64, semantic_type = !obelisk.ranged_packed_array<1 : 0 x !obelisk.integral<1, false, true, 0 : 0, logic>>, sym_name = "s9.formal"} {
            }
            obelisk.sv.symbol.net attributes {hierarchical_name = "inout_width_top.child.formal", is_implicit = false, name = "formal", net_kind = 1 : i32, node_id = 15 : i64, semantic_type = !obelisk.ranged_packed_array<1 : 0 x !obelisk.integral<1, false, true, 0 : 0, logic>>, sym_name = "s10.formal"} {
            }
          }
        }
      }
    }
  }
}

// CHECK-DAG: obelisk_sim.net.decl 0 {{.*}} : !obelisk_sim.packed_array<0 : 0 x !obelisk_sim.logic<1>> {{.*}}hierarchy "inout_width_top.actual"
// CHECK-DAG: obelisk_sim.net.decl 1 {{.*}} : !obelisk_sim.packed_array<1 : 0 x !obelisk_sim.logic<1>> {{.*}}hierarchy "inout_width_top.child.formal"
// CHECK-DAG: obelisk_sim.port.decl 0 {{.*}} source 1 net = true at 0 : !obelisk_sim.packed_array<1 : 0 x !obelisk_sim.logic<1>> inout ordinal 0 hierarchy "inout_width_top.child.formal"
// CHECK: obelisk_sim.net.connect.decl 0 {{.*}} 0[0] to 1[0] width 1 reversed = false provenance "ordered"
// CHECK-NOT: port_output
// CHECK-NOT: obelisk.sv.
