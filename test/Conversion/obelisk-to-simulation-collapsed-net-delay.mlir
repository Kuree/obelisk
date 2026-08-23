// RUN: obelisk-opt %s '--lower-obelisk-to-sim=opt-level=0' | FileCheck %s
// RUN: obelisk-opt %s '--lower-obelisk-to-sim=opt-level=3' | FileCheck %s --check-prefix=O3

module {
  obelisk.sv.symbol.definition attributes {definition_kind = 0 : i32, hierarchical_name = "collapsed_delay_top", name = "collapsed_delay_top", node_id = 0 : i64, sym_name = "s0.top"} {
  }
  obelisk.sv.symbol.definition attributes {definition_kind = 0 : i32, hierarchical_name = "collapsed_delay_child", name = "collapsed_delay_child", node_id = 1 : i64, sym_name = "s1.child"} {
  }
  obelisk.sv.symbol.root attributes {hierarchical_name = "\\$root ", name = "$root", node_id = 2 : i64, sym_name = "s2.$root"} {
    obelisk.sv.symbol.compilation_unit attributes {hierarchical_name = "$unit", node_id = 3 : i64, sym_name = "s3"} {
    }
    obelisk.sv.symbol.instance attributes {hierarchical_name = "collapsed_delay_top", is_uninstantiated = false, name = "collapsed_delay_top", node_id = 4 : i64, referenced_path = "collapsed_delay_top", referenced_symbol = @s0.top, sym_name = "s4.top"} {
      obelisk.sv.symbol.instance_body attributes {hierarchical_name = "collapsed_delay_top", name = "collapsed_delay_top", node_id = 5 : i64, sym_name = "s5.top", time_precision_fs = 1000000 : i64, time_unit_fs = 1000000 : i64} {
        obelisk.sv.symbol.net attributes {delay_fs = array<i64: 5000000>, hierarchical_name = "collapsed_delay_top.external", is_implicit = false, name = "external", net_kind = 12 : i32, node_id = 6 : i64, semantic_type = !obelisk.integral<1, false, true, 0 : 0, logic>, sym_name = "s6.external"} {
          obelisk.sv.timing.delay attributes {node_id = 7 : i64} {
            obelisk.sv.expression.integer_literal attributes {constant_value = "5", node_id = 8 : i64, semantic_type = !obelisk.integral<32, true, false, 31 : 0, int>} {
            }
          }
        }
        obelisk.sv.symbol.instance attributes {hierarchical_name = "collapsed_delay_top.child", is_uninstantiated = false, name = "child", node_id = 9 : i64, referenced_path = "collapsed_delay_child", referenced_symbol = @s1.child, sym_name = "s7.child"} {
          obelisk.sv.port.connection attributes {actual_is_constant = false, direction = 2 : i32, formal_name = "formal", formal_ordinal = 0 : i64, formal_path = "collapsed_delay_top.child.formal", formal_symbol = @s2.$root::@s4.top::@s5.top::@s7.child::@s8.child::@s9.formal, formal_type = !obelisk.integral<1, false, true, 0 : 0, logic>, internal_path = "collapsed_delay_top.child.formal", internal_symbol = @s2.$root::@s4.top::@s5.top::@s7.child::@s8.child::@s10.formal, is_ansi = true, is_net = true, node_id = 10 : i64, provenance = 0 : i32} {
          } {
            obelisk.sv.expression.named_value attributes {node_id = 11 : i64, referenced_path = "collapsed_delay_top.external", referenced_symbol = @s2.$root::@s4.top::@s5.top::@s6.external, semantic_type = !obelisk.integral<1, false, true, 0 : 0, logic>} {
            }
          }
          obelisk.sv.symbol.instance_body attributes {hierarchical_name = "collapsed_delay_top.child", name = "collapsed_delay_child", node_id = 12 : i64, sym_name = "s8.child", time_precision_fs = 1000000 : i64, time_unit_fs = 1000000 : i64} {
            obelisk.sv.symbol.port attributes {direction = 2 : i32, hierarchical_name = "collapsed_delay_top.child.formal", name = "formal", node_id = 13 : i64, semantic_type = !obelisk.integral<1, false, true, 0 : 0, logic>, sym_name = "s9.formal"} {
            }
            obelisk.sv.symbol.net attributes {hierarchical_name = "collapsed_delay_top.child.formal", is_implicit = false, name = "formal", net_kind = 1 : i32, node_id = 14 : i64, semantic_type = !obelisk.integral<1, false, true, 0 : 0, logic>, sym_name = "s10.formal"} {
            }
            obelisk.sv.symbol.continuous_assign attributes {hierarchical_name = "collapsed_delay_top.child", node_id = 15 : i64, sym_name = "s11.assign", time_precision_fs = 1000000 : i64, time_unit_fs = 1000000 : i64} {
              obelisk.sv.expression.assignment attributes {assignment_kind = 0 : i32, node_id = 16 : i64, semantic_type = !obelisk.integral<1, false, true, 0 : 0, logic>} {
                obelisk.sv.expression.named_value attributes {node_id = 17 : i64, referenced_path = "collapsed_delay_top.child.formal", referenced_symbol = @s2.$root::@s4.top::@s5.top::@s7.child::@s8.child::@s10.formal, semantic_type = !obelisk.integral<1, false, true, 0 : 0, logic>} {
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

// LRM 23.3.3.7/Table 23-1: an external uwire dominates an internal wire. Its
// delay therefore applies to both logical declarations in the simulated net.
// CHECK-DAG: obelisk_sim.net.decl 0 {{.*}} hierarchy "collapsed_delay_top.external" {{.*}}propagation_delays = array<i64: 5, 5, 5>{{.*}}resolution_kind = 2 : i32
// CHECK-DAG: obelisk_sim.net.decl 1 {{.*}} hierarchy "collapsed_delay_top.child.formal" {{.*}}propagation_delays = array<i64: 5, 5, 5>
// CHECK: obelisk_sim.net.connect.decl 0 {{.*}} 0[0] to 1[0] width 1 reversed = false provenance "ordered" rhs_dominates = false
// CHECK: obelisk_sim.func private @unit_0
// CHECK-SAME: obelisk_sim.delayed_net
// CHECK: obelisk_sim.driver.drive_delayed_net

// O3: obelisk_sim.net.connect.decl
// O3-SAME: rhs_dominates = false
// O3-LABEL: obelisk_sim.func private @unit_0
// O3: obelisk_sim.driver.drive_delayed_net
